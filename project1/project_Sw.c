/*
 * convolution.c
 *
 *  Created on: 20 Mar 2019
 *      Author: Ion Ciobotari
 */

#include <stdio.h>
#include <stdlib.h>

#define MAT_SIZE_IN 28
#define KERNEL_SIZE 5
#define MAT_SIZE_OUT 24 //MAT_SIZE_IN-KERNEL_SIZE+1

volatile unsigned char *imag;
volatile int *result;
volatile unsigned char *imag_in;

#define IMAG(I, J) (imag[(I)*MAT_SIZE_IN + (J)])
#define RESULT(I, J) (result[(I)*MAT_SIZE_OUT + (J)])
#define KERNEL(I, J) (kernel[(I)*KERNEL_SIZE + (J)])
#define IMAG_IN(I, J) (imag_in[(I)*KERNEL_SIZE*KERNEL_SIZE+(J)])

#define MAT_ADD_IMAGE 0x01000010
#define MAT_ADD_RESULT (MAT_ADD_IMAGE+ 100*MAT_SIZE_IN*MAT_SIZE_IN)
#define MAT_ADD_IMAGE_TRANSFORM 0x10000000


static int kernel0[KERNEL_SIZE*KERNEL_SIZE] = { 0, 0, 0, 0, 0,
												0, 0, 0, 0, 0,
												0, 0, 1, 0, 0,
												0, 0, 0, 0, 0,
												0, 0, 0, 0, 0};


static int num_imag=0;

int main() {
	int i, j, k;

	// load do ficheiro de imagens
	imag = (unsigned char *)(MAT_ADD_IMAGE+0x0016+num_imag*MAT_SIZE_IN*MAT_SIZE_IN);
	result = (int *)(MAT_ADD_RESULT);
    imag_in = (unsigned char *)(MAT_ADD_IMAGE_TRANSFORM);

    // criar matriz imagem entrada transformada
    for(i=0; i < MAT_SIZE_OUT*MAT_SIZE_OUT; i++){
        for(j = 0; j < KERNEL_SIZE*KERNEL_SIZE; j++){
                IMAG_IN(i, j) = IMAG(i/MAT_SIZE_OUT , i%MAT_SIZE_OUT + j%KERNEL_SIZE + j/KERNEL_SIZE*MAT_SIZE_IN);
        }
    }

    // calcular matriz resultado
	for(i = 0; i < MAT_SIZE_OUT; i++){
		for(j = 0; j < MAT_SIZE_OUT; j++){
			RESULT(i, j) = 0;
			for(k = 0; k < KERNEL_SIZE*KERNEL_SIZE; k++){
				RESULT(i, j)+= kernel0[k] * IMAG_IN(i*MAT_SIZE_OUT+j, k);
			}
		}
	}

	printf("Printing entry image\n");
	for(i = 0; i < MAT_SIZE_IN;i ++){
		for(j = 0; j < MAT_SIZE_IN; j++){
			printf("%3d ", imag[i*MAT_SIZE_IN+j]);
		}
		printf("\n");
	}

	printf("Printing result image\n");
	for(i = 0; i < MAT_SIZE_OUT;i ++){
		for(j = 0; j < MAT_SIZE_OUT; j++)
			printf("%3d ", result [i*MAT_SIZE_OUT+j]);
		printf("\n");
	}

	return 0;
}
