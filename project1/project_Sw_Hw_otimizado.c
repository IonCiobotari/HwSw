/*
 * project.c
 *
 *  Created on: 11 Apr 2019
 *      Author: Ion Ciobotari
 */


#include <stdio.h>
#include <stdlib.h>
#include "xparameters.h"
#include "xil_exception.h"
#include "xstreamer.h"
#include "xil_cache.h"
#include "xllfifo.h"
#include "xstatus.h"

#define MAT_SIZE_IN 28
#define KERNEL_SIZE 5
#define MAT_SIZE_OUT 24 //MAT_SIZE_IN-KERNEL_SIZE+1

volatile unsigned char *imag;
volatile unsigned char *result;
volatile unsigned char *imag_t;

#define IMAG(I, J) (imag[(I)*MAT_SIZE_IN + (J)])
#define RESULT(I, J) (result[(I)*MAT_SIZE_OUT + (J)])
#define KERNEL(I, J) (kernel[(I)*KERNEL_SIZE + (J)])
#define IMAG_T(I, J) (imag_t[(I)*KERNEL_SIZE*KERNEL_SIZE+(J)])

#define MAT_ADD_IMAGE 0x01000010
#define MAT_ADD_RESULT (MAT_ADD_IMAGE+ 100*MAT_SIZE_IN*MAT_SIZE_IN)
#define MAT_ADD_IMAGE_TRANSFORM 0x10000000


static unsigned char kernel[KERNEL_SIZE*KERNEL_SIZE+3] = { 0, 0, 0, 0, 0,
												0, 0, 0, 0, 0,
												0, 0, 1, 0, 0,
												0, 0, 0, 0, 0,
												0, 0, 0, 0, 0,
												0, 0, 0};

static int num_imag=0;

XLlFifo FifoInstance;

int my_axis_fifo_init()
{
	XLlFifo_Config *Config;
	XLlFifo *InstancePtr;
	int Status;

	InstancePtr = &FifoInstance;
	Status = XST_SUCCESS;

	/* Initialize the Device Configuration Interface driver */
	Config = XLlFfio_LookupConfig(XPAR_AXI_FIFO_0_DEVICE_ID);
	if (!Config) {
		xil_printf("No config found for %d\r\n", XPAR_AXI_FIFO_0_DEVICE_ID);
		return XST_FAILURE;
	}

	Status = XLlFifo_CfgInitialize(InstancePtr, Config, Config->BaseAddress);
	if (Status != XST_SUCCESS) {
		xil_printf("Initialization failed\n\r");
		return Status;
	}

	/* Check for the Reset value */
	Status = XLlFifo_Status(InstancePtr);
	XLlFifo_IntClear(InstancePtr,0xffffffff);
	Status = XLlFifo_Status(InstancePtr);
	if(Status != 0x0) {
		xil_printf("\n ERROR : Reset value of ISR0 : 0x%x\t Expected : 0x0\n\r",
			       XLlFifo_Status(InstancePtr));
		return XST_FAILURE;
	}

	return Status;
}

// A frame is transmittted by using the following sequence:
// 1) call XLlFifo_Write() one or more times to write all of the bytes in the next frame.
// 2) call XLlFifo_TxSetLen() to begin the transmission of frame just written.

unsigned my_send_to_fifo(void *BufPtr, unsigned nWords)
{
  unsigned available_words=0, words_to_send=0;
  unsigned bytes_to_send=0;

  available_words = XLlFifo_iTxVacancy(&FifoInstance);

// If you want to wait for available space in the FIFO uncomment the following loop
//  while (nWords > available_words) {
//	  available_words = XLlFifo_iTxVacancy(&FifoInstance);
//  }

  words_to_send = (nWords > available_words) ? available_words : nWords;
  bytes_to_send = words_to_send;
  XLlFifo_Write(&FifoInstance, BufPtr, bytes_to_send);
  XLlFifo_TxSetLen(&FifoInstance, bytes_to_send);

  /* Check for Transmission completion */
  while( !(XLlFifo_IsTxDone(&FifoInstance)) );

  return words_to_send;
}

// A frame is received by using the following sequence:
// 1) call XLlFifo_RxOccupancy() to check the occupancy count
// 2) call XLlFifo_RxGetLen() to get the length of the next incoming frame
// 3) call XLlFifo_Read() one or more times to read the number of bytes reported by XLlFifo_RxGetLen().

unsigned my_receive_from_fifo(void *BufPtr, unsigned nWords)
{
  unsigned bytes_to_read=0, frame_len=0, n=0;

  // wait for something to read
  while (! (n=XLlFifo_RxOccupancy(&FifoInstance)) ){}
  //printf("RxOccup=%d nWords=%d\n", n, nWords);
  frame_len = (unsigned)XLlFifo_RxGetLen(&FifoInstance);
  //printf("RxOccup=%d fl=%d\n\n", n, frame_len);
  bytes_to_read = ((nWords) > frame_len) ? frame_len : (nWords);
  XLlFifo_Read(&FifoInstance, BufPtr, bytes_to_read);

  /* Check for Reception completion */
  while( !(XLlFifo_IsRxDone(&FifoInstance)) );

  return (bytes_to_read);
}

////////////////////////////////////////////////////////////////////////////////

int main(){
	int i, j;
	unsigned nwords;

	// load do ficheiro de imagens
	imag = (unsigned char *)(MAT_ADD_IMAGE+num_imag*MAT_SIZE_IN*MAT_SIZE_IN);
	result = (unsigned char *)(MAT_ADD_RESULT);
  imag_t = (unsigned char *)(MAT_ADD_IMAGE_TRANSFORM);

  // criar matriz imagem entrada transformada
  for(i=0; i < MAT_SIZE_OUT*MAT_SIZE_OUT; i++){
    for(j = 0; j < KERNEL_SIZE*KERNEL_SIZE; j++){
    	IMAG_T(i, j) = IMAG(i/MAT_SIZE_OUT , i%MAT_SIZE_OUT + j%KERNEL_SIZE + j/KERNEL_SIZE*MAT_SIZE_IN);
      }
  }

  my_axis_fifo_init();

  nwords = my_send_to_fifo((void *)(kernel), KERNEL_SIZE*KERNEL_SIZE+3); // nwords = 5*5
  if (nwords < KERNEL_SIZE*KERNEL_SIZE) {
    printf("Error: Able to send only %d < requested %d\n", nwords, KERNEL_SIZE*KERNEL_SIZE);
    printf(" Exiting program\n");
    return 0;
  }

  nwords = my_send_to_fifo((void *)(imag_t),KERNEL_SIZE*KERNEL_SIZE*MAT_SIZE_OUT*MAT_SIZE_OUT); // nwords = 5*5*24*24
	if (nwords < (KERNEL_SIZE*KERNEL_SIZE*MAT_SIZE_OUT*MAT_SIZE_OUT)) {
		printf("Error: Able to send only %d < requested %d\n", nwords, KERNEL_SIZE*KERNEL_SIZE*MAT_SIZE_OUT*MAT_SIZE_OUT);
		printf("       Exiting program\n");
		return 0;
	}

	nwords = my_receive_from_fifo((void *)(result), MAT_SIZE_OUT*MAT_SIZE_OUT); // nwords = 24*24
	if (nwords < MAT_SIZE_OUT*MAT_SIZE_OUT) {
		printf("Error: Able to receive only %d < requested %d\n", nwords, MAT_SIZE_OUT*MAT_SIZE_OUT);
		printf("       Exiting program\n");
		return 0;
	}


	printf("\n\nPrinting entry image\n\n");
	for(i = 0; i < MAT_SIZE_IN;i ++){
		for(j = 0; j < MAT_SIZE_IN; j++){
			printf("%3d ", imag[i*MAT_SIZE_IN+j]);
		}
		printf("\n");
	}


	printf("\n\nPrinting result\n\n");
	for(i = 0; i < MAT_SIZE_OUT;i ++){
		for(j = 0; j < MAT_SIZE_OUT; j++)
			printf("%3d ", result[i*MAT_SIZE_OUT+j]);
		printf("\n");
	}

	return 0;
}



