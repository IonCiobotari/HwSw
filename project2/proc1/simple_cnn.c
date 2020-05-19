#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "image.h"
#include "simple_cnn.h"
#include "xaxidma.h"
#include "xil_cache.h"
#include "xparameters.h"
#include "xil_mmu.h"

volatile unsigned char *ch_images;  // Images data region
volatile float *fp_weights; // Network weights data region

volatile float *fp_image; // Scaled floating-point image to be processed

volatile float *matA;  // Auxiliary matrix A
volatile float *matB;   // Auxiliary matrix B of 22 feature maps with 5*5 weights each
volatile float *matCpool; // Output of pooling layer 22 images of size 12*12
volatile float *matConn;  // Intermediate output (before adding bias) of fully connected layer (10 elements)
volatile float *matConnB; // Output of fully connected layer (10 elements)
volatile float *matSoftM; // Output of softmax layer (10 elements)

volatile int *semaphore;
volatile int *result;
volatile float *result_percent;
XAxiDma AxiDma;


// Matrix multiplication: C = A * B
void gemm(float *A, float *B, float *C, int rowsA, int colsA, int colsB)
{
  int i, j, k;

  for (i=0; i<rowsA; i++) {
    for (j=0; j<colsB; j++) {
      C[i*colsB+j] = 0.0;
      for (k=0; k<colsA; k++) {
	C[i*colsB+j] += A[i*colsA+k] * B[k*colsB+j];
      }
    }
  }
}

// Matrix multiplication: C = A * transposed(B)
int gemmBT(float *A, float *B, float *C, int rowsA, int colsA, int rowsB)
{
  //int i, j, k;
  //int colsBT, colsB;
	//int i;
  float *TxBufferPtr, *RxBufferPtr;
  int Status;

  Xil_DCacheFlushRange((INTPTR)(B), sizeof(float)*(22*25+22));
  Xil_DCacheFlushRange((INTPTR)(A), sizeof(float)*(576*25));

      TxBufferPtr = (float *)B;
      Status = XAxiDma_SimpleTransfer(&AxiDma,(UINTPTR) TxBufferPtr, sizeof(float)*(22*25+22), XAXIDMA_DMA_TO_DEVICE);
      if (Status != XST_SUCCESS) { return XST_FAILURE; }
      while (XAxiDma_Busy(&AxiDma, XAXIDMA_DMA_TO_DEVICE)) { /* Wait for Tx*/ }


      // receive column of C (row of TC)
      RxBufferPtr = (float *)C;
      Status = XAxiDma_SimpleTransfer(&AxiDma,(UINTPTR) (RxBufferPtr), sizeof(float)*(144*22), XAXIDMA_DEVICE_TO_DMA);
      if (Status != XST_SUCCESS) { return XST_FAILURE; }


      // send full matrix A
      TxBufferPtr = (float *)A;
      Status = XAxiDma_SimpleTransfer(&AxiDma,(UINTPTR) TxBufferPtr, sizeof(float)*(576*25), XAXIDMA_DMA_TO_DEVICE);
      if (Status != XST_SUCCESS) { return XST_FAILURE; }

      while (XAxiDma_Busy(&AxiDma,XAXIDMA_DMA_TO_DEVICE)) { /* Wait Tx */ }
      while (XAxiDma_Busy(&AxiDma,XAXIDMA_DEVICE_TO_DMA)) { /* Wait Rx*/ }
   // }

  Xil_DCacheInvalidateRange((INTPTR)(C), sizeof(float)*(144*22));

  return XST_SUCCESS;
}


// Adds bias to matrix C
// if (transpose_flag == 1) also transposes input matrix
void add_bias(float *C, int rows, int cols, float *bias, float *Cbias, int transpose_flag)
{
  int i, j;

  if (transpose_flag) {
    for (i=0; i<rows; i++) {
      for (j=0; j<cols; j++) {
        Cbias[j*rows+i] = C[i*cols+j] + bias[j] ;
      }
    }
  }
  else {
    for (i=0; i<rows; i++) {
      for (j=0; j<cols; j++) {
	    Cbias[i*cols+j] = C[i*cols+j] + bias[i] ;
      }
    }
  }
}

// Prepares matrix A to calculate convolutions as a matrix multiplication
void prepare_matrixA()
{
  int i, j, k, row, col;

  for (i=0; i<24; i++) {
    for (j=0; j<24; j++) {
      for (k=0; k<25; k++) {
        row = i + k/5;
        col = j + (k%5);
        // Matrix A has (24*24) rows and 25 columns
        matA[(i*24+j)*25+k] = fp_image[row*IMAGE_WIDTH+col];//*SCALE_COEF;
      }
    }
  }
}


// Returns the position of the largest value in the 10-element input vector
// The softmax function normalizes the input-vector to a probability distribution
int forward_softmax_layer()
{
  int i, n=10, best=-1;
  float sum = 0.0, e;
  float largest = -FLT_MAX;

  for(i = 0; i < n; ++i){
    if(matConnB[i] > largest) {
      largest = matConnB[i];
      best = i;
    }
  }

  for(i = 0; i < n; ++i){
    e = exp(matConnB[i] - largest);
    sum += e;
    matSoftM[i] = e;
  }
  for(i = 0; i < n; ++i){
    matSoftM[i] /= sum;
  }
  //print_fp((float *)matSoftM, 10, "Softmax");

  return best;
}


// The convolution layer consists of 22 feature maps.
// Each feature map has a set of 5*5 weights and a single bias.
void forward_convolutional_layer()
{
    // Matrix A is prepared (with 24*24=576 rows and 5*5=25 columns)
    // in order to do the convolutions as a matrix multiplication
    // such that, A(576*25) * BT(25*22) -> C(576*22)
    prepare_matrixA();

    // The 22 maps weights are stored as a 22*5*5 matrix (after the initial 22 bias values)
    matB = fp_weights;//+22;
    
   gemmBT((float *)matA, (float *)matB, (float *)matCpool, 24*24, 25, 22);
}

// This layer fully connects the 3168 inputs (22 12*12images),
// to 10 output neurons (one for each digit)
void forward_connected_layer()
{
    float *matW, *matIN, *mbias, *matOUT, *matOutB;

    // The 10 bias values of this layer are stored after the 22+550 convolutional bias+weigths
    mbias = (float *)fp_weights + 22 + 550;
    // The 10*2880 weights are stored after the 10 bias values
    matW = (float *)fp_weights + 22 + 550 + 10;
    
    matIN = (float *)matCpool;
    matOUT = (float *)matConn;
    matOutB = (float *)matConnB;
    
    // A(10*3168) * B(3168*1) -> C(10*1)
    gemm(matW, matIN, matOUT, 10, 3168, 1);
    // print_fp((float *)matConn, 10, "Connected");
    // print_fp(mbias, 10, "Bias");
      
    add_bias(matOUT, 10, 1, mbias, (float *)matOutB, 0);
    // print_fp((float *)matConnB, 10, "Connected+Bias");
    // Output vector ConnB has 10 values, one for each digit
}

// Digit classification is performed using 4 layers:
// 1. Convolutional layer
// 2. Pooling layer
// 3. Fully-connected layer
// 4. Softmax Layer
int predict_mnist()
{
  int best;
  
  forward_convolutional_layer();
  forward_connected_layer();
  best = forward_softmax_layer();
  return best;
}

void define_memory_regions()
{
  static float *paddress = (float *)MEM_DATA_BASE_ADDRESS;

  // Region Size NIMAGES*IMAGE_HEIGTH*IMAGE_WIDTH+16 = 78416 Bytes (100 images)
  ch_images = (unsigned char *)MEM_IMAGES_BASE_ADDRESS;
  // Region Size TOTAL_WEIGTHS*sizeof(float) = 29330*4 = 117320 Bytes
  fp_weights = (volatile float *)MEM_WEIGTHS_BASE_ADDRESS; 
   
  // Region Size IMAGE_HEIGTH*IMAGE_WIDTH*sizeof(float) = 28*28*4 = 3136 Bytes
  fp_image = paddress;
  paddress += 28*28;
 
  // Aux matrix of (24*24)*(25) elements. Region Size = 14400 * 4 = 57600 Bytes
  matA = paddress;
  paddress += (24*24)*(25);
  // Transpose of matA. Region Size = 14400 * 4
  // Aux matrix of (22)*(25) elements. Region Size = 550 * 4 Bytes;
  matB = paddress;
  paddress += (22)*(25);
  // Transpose of matB. Region Size = 550 * 4 Bytes
  // Aux matrix of (24*24)*(22) elements. Region Size = 12672 * 4 Bytes;
  // Transpose of matC. Region Size = 11520 * 4 Bytes
  // Aux matrix of (22)*(24*24) elements. Region Size = 11520 * 4 Bytes
  // Aux matrix of (22)*(12*12) elements. Region Size = 3168 * 4 Bytes
  matCpool = paddress;
  paddress += (22)*(12*12);
  // Aux matrix of 10 elements. Region Size = 10 * 4 Bytes;
  matConn = paddress;
  paddress += 10;
  // Aux matrix of 10 elements. Region Size = 10 * 4 Bytes
  matConnB = paddress;
  paddress += 10;
  // Aux matrix of 10 elements. Region Size = 10 * 4 Bytes
  matSoftM = paddress;

  semaphore = (int *)SEMAPHORE_ADDRESS;
  result = (int *)(semaphore +1);
  result_percent = (float *)(result +1);

  Xil_SetTlbAttributes(SEMAPHORE_ADDRESS,0x14de2);
  Xil_SetTlbAttributes(SEMAPHORE_ADDRESS+1,0x14de2);
  Xil_SetTlbAttributes(SEMAPHORE_ADDRESS+2,0x14de2);


  // printf("%p, %d\n", (void *)paddress+10, (paddress+10)-(float *)MEM_DATA_BASE_ADDRESS);
  // Total data region size is 71898 * 4 = 287,592 Bytes

}

// Inicialization of DMA
int init_XAxiDma_SimplePollMode(u16 DeviceId)
{
  XAxiDma_Config *CfgPtr;
  int Status;

  /* Initialize the XAxiDma device.	 */
  CfgPtr = XAxiDma_LookupConfig(DeviceId);
  if (!CfgPtr) {
    printf("No config found for %d\r\n", DeviceId);
    return XST_FAILURE;
  }

  Status = XAxiDma_CfgInitialize(&AxiDma, CfgPtr);
  if (Status != XST_SUCCESS) {
    printf("Initialization failed %d\r\n", Status);
    return XST_FAILURE;
  }

  if(XAxiDma_HasSg(&AxiDma)){
    printf("Device configured as SG mode \r\n");
    return XST_FAILURE;
  }

  /* Disable interrupts, we use polling mode	 */
  XAxiDma_IntrDisable(&AxiDma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
  XAxiDma_IntrDisable(&AxiDma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

  return XST_SUCCESS;
}

/********************************************************************************
 * *****************************************************************************
 * *****************************************************************************
 * *****************************************************************************/

int main(int argc, char **argv){

	*semaphore = -1;
  unsigned int image_to_classify = 1; //default
  int prediction;
  int Status;

  Status = init_XAxiDma_SimplePollMode(DMA_DEV_ID);
   if (Status != XST_SUCCESS) {
     printf("init_XAxiDma_SimplePollMode: Failed\r\n");
     return XST_FAILURE;
   }

  define_memory_regions();

  while(*semaphore != P0_START){}
  *semaphore = P1_START;

  for (image_to_classify = IMAGE_TO_CLASSIFY+1;
		  image_to_classify < (IMAGE_TO_CLASSIFY+NUMBER_OF_IMAGES_TO_CLASSIFY);
		  image_to_classify+=2) {

    // The pixels of the input image are scaled to the [0,1[ interval
    image_scale2float((unsigned char *)ch_images, image_to_classify, (float *)fp_image);

    prediction = predict_mnist();
    *result = prediction;
    *result_percent = matSoftM[prediction]*100;
    *semaphore = P1_END;
    while(*semaphore != P0_END){}

  }
}
