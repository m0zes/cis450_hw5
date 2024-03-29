/* 
 * pthreaded hw5, written by Adam Tygart abd Ryan Hershberger
 * Could be further optimized by pipelining read operations and not cyclically creating/destroying child threads
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Length of "lines changes with every protein"
 * Thanks to wikipedia for the following pseudocode:
 * function LCSLength(X[1..m], Y[1..n])
 *     C = array(0..m, 0..n)
 *     for i := 0..m
 *        C[i,0] = 0
 *     for j := 0..n
 *        C[0,j] = 0
 *     for i := 1..m
 *         for j := 1..n
 *             if X[i] = Y[j]
 *                 C[i,j] := C[i-1,j-1] + 1
 *             else:
 *                 C[i,j] := max(C[i,j-1], C[i-1,j])
 *     return C[m,n]
 */

FILE *f;
int comp_count;
int offset = 0;

#ifndef NUM_THREADS
#define NUM_THREADS 4
#endif

#ifndef WORK_UNIT
#define WORK_UNIT 400
#endif

#define QUEUE_SIZE NUM_THREADS*WORK_UNIT


void checkCUDAError(const char *msg)
{
    cudaError_t err = cudaGetLastError();
    if( cudaSuccess != err) 
    {
        fprintf(stderr, "Cuda error: %s: %s.\n", msg, 
                             cudaGetErrorString( err) );
        exit(EXIT_FAILURE);
    }                         
}


/*
 * Calculate the LCS of the two strings.
 */
__device__ int MCSLength(char *str1, int len1, char* str2, int len2) {
	int** arr = (int**) malloc(sizeof(int*)*(len1+1));
	if ( arr == 0 ) {
		printf("Couldn't allocate memory for the MCS array\n");
	}
	int i, j, local_max = 0;
	for (i = 0; i <= len1; i++) {
		arr[i] = (int*)malloc((len2+1)*sizeof(int));
		if ( arr[i] == 0 ) {
			printf("Couldn't allocate memory for the MCS subarray\n");
		}
	}
	for (i = 1; i <= len1; i++) {
		for (j = 1; j <= len2; j++) {
			if (str1[i-1] == str2[j-1]) {
				arr[i][j] = arr[i-1][j-1] + 1;
				if (arr[i][j] > local_max)
					local_max = arr[i][j];
			}

		}
	}
	for (i = 0; i <= len1; i++)
		free(arr[i]);
	free(arr);
	return local_max;
}

/*
 * Read file, char by char. headers start with '>' or ';', ignore until newline.
 * read "gene" until we reach the next header. return int of num of chars in buff[i]
 */
int readLine(char **buff, int i) {
	int readchars = 0;
	int commentline = 0, startedgene = 0;
	int buffStepSize = 4000;
	int buffSize = 4000;
	buff[i] = (char*)malloc(sizeof(char)*buffSize);
	char c;
	do {
		if (((readchars) >= buffSize) && (buffSize != 0)) {
			buffSize += buffStepSize;
			char* temp_buff = (char*)realloc(buff[i],sizeof(char)*buffSize);
			buff[i] = temp_buff;
		}
		if (buff[i] == 0) {
			printf("Couldn't allocate memory for the buffer\n");
			exit(-2);
		}
		c = fgetc(f);
		switch (c) {
			case '\n':
				commentline = 0;
				break;
			case ';':
			case '>':
				commentline = 1;
				if (startedgene == 1) {
					long curr = ftell(f);
					fseek(f, curr-1, SEEK_SET);
					return readchars;
				}
				break;
			default:
				if ( commentline == 0 ) {
					startedgene = 1;
					if (c != EOF)
						buff[i][readchars++] = c;
				}
		}
	} while (c != EOF);
	return readchars;
}

/*
 * Is the worker function for a thread, calculate your chunk of the global data, calculate the MCS of each pair, copy the counts off to the global counts once locked
 */
__global__ void threaded_count(int* completed_count, int* counts, char* queue, int* lens) {
	int local_work_unit = blockDim.x*blockIdx.x;
	int local_counts[WORK_UNIT/2];
	int local_count = 0;
	int startPos = (threadIdx.x) + (local_work_unit);
	int endPos = startPos + (local_work_unit);
	char* str1;
	char* str2;
	int strlen1, strlen2;

	int i, j, k;
	for (i = 0; i < WORK_UNIT/2; i++) {
		local_counts[i] = 0;
		j = startPos + (i*2);
		if ((lens[j] != 0) && (lens[j+1] != 0)) {
		//dev_lens needs to hold starting positions of the current string in dev_queue
			str1 = (char*) malloc(lens[j]+1*sizeof(char));
			str2 = (char*) malloc(lens[j+1]+1*sizeof(char));
			strlen1 = lens[j+1] - lens[j];
			strlen2 = lens[j+2] - lens[j+1];
			for (k = 0; k < strlen1; k++)
				str1[k] = queue[lens[j] + k];
			for (k = 0; k < strlen2; k++)
				str2[k] = queue[lens[j+1] + k];
				
			local_counts[i] = MCSLength(str1, strlen1, str2, strlen2);
			free(str1);
			free(str2);
			local_count++;
		}
		else
			break;
	}
	for (i = 0; i < WORK_UNIT/2; i++) {
		counts[(startPos/2) + i] = local_counts[i];
	}
	atomicAdd(completed_count, local_count);
}

/*
 * Take a file-name on the command line, open it and read portions of the file at a time. start threads to calcluate MCS. Find the max and average MCSs
 */
int main(int argc, char* argv[]) {
	if (argc != 2 ) {
		printf("Please specify a file on the command line\n");
		exit(-1);
	}
	f = fopen(argv[1],"r");
	if ( f == 0 ) {
		printf("Couldn't open file\n");
		exit(-1);
	}
	char **queue;
	int *lens;
	int *counts;
	char *dev_queue;
	int *dev_lens;
	int *dev_counts;
	//pthread
	int i;
	int perThread = WORK_UNIT;
	int totalSize = QUEUE_SIZE;
	int size = NUM_THREADS;
	int numThreadsPerBlock = 100;
	int numBlocks = size / numThreadsPerBlock;
	int totalThreads = numThreadsPerBlock * numBlocks;
	int* dev_completed_count;
	cudaMalloc((void**)&dev_completed_count, sizeof(int));
	printf("we get this far!\n");

	counts = (int*)calloc(sizeof(int),QUEUE_SIZE);
	


	do {
		queue = (char**)malloc(sizeof(char*)*QUEUE_SIZE);
		printf("A\n");

		lens = (int*)calloc(sizeof(int),QUEUE_SIZE+1);
		cudaMalloc((void**)&dev_lens, sizeof(int)*(QUEUE_SIZE +1));

		printf("B\n");
		
		int *temp_counts = (int*) realloc(counts, (QUEUE_SIZE + offset)/2 * sizeof(int));
		
		printf("C\n");
		
		if (( queue == 0 ) || (lens == 0) || (temp_counts == 0)) {
			printf("Couldn't allocate memory for the work queues\n");
			exit(-1);
		}
		counts = temp_counts;
		
		printf("This is a TEST %d\n", QUEUE_SIZE);
		
		int t = 0;
		char *dev_queue_flat = (char *) malloc(sizeof(char));
		char *temp_flat;
		lens[0] = 0;
		for (i = 0; i < QUEUE_SIZE; i++) {
			lens[i+1] = t + readLine(queue, i);
			temp_flat = (char *) realloc(dev_queue_flat, (lens[i+1] + 1) * sizeof(char));
			dev_queue_flat = temp_flat;
			int j;
			for (j = 0; j <= lens[i+1] - t; j++)
				dev_queue_flat[t+j] = queue[i][j];
			t = lens[i+1];
			if (( queue[i] == 0 )) {
				printf("Couldn't allocate memory for the work subqueues\n");
				exit(-1);
			}
		}
		cudaMalloc((void**)&dev_queue, (t * sizeof(char)));
		cudaMemcpy(dev_queue, dev_queue_flat, t*sizeof(char), cudaMemcpyHostToDevice);
		cudaMemcpy(dev_lens, lens, QUEUE_SIZE*sizeof(int), cudaMemcpyHostToDevice);

		cudaMalloc((void**)&dev_counts, (QUEUE_SIZE*sizeof(int))/2);
		cudaMemset( dev_counts, 0, (QUEUE_SIZE*sizeof(int))/2);

		printf("A1\n");


		dim3 numBlocks(NUM_THREADS);
		dim3 threadsPerBlock(WORK_UNIT);
		threaded_count<<< numBlocks, threadsPerBlock >>>(dev_completed_count, dev_counts, dev_queue, dev_lens);
		cudaThreadSynchronize();
		int* temp = (int*) malloc(sizeof(int)*QUEUE_SIZE/2);
		cudaMemcpy(temp, dev_counts, (QUEUE_SIZE*sizeof(int))/2, cudaMemcpyDeviceToHost);
		for (i = 0; i < QUEUE_SIZE/2; i++)
			counts[offset+i] = temp[i];

		for (i = 0; i < QUEUE_SIZE; i++) {
			free(queue[i]);
		}
		cudaFree(dev_queue);
		cudaFree(dev_counts);
		free(temp);
		cudaFree(dev_queue);
		free(queue);
		cudaFree(dev_lens);
		free(lens);
		offset += QUEUE_SIZE;
	} while (!feof(f));
	unsigned long total = 0;
	int longest = 0, longest_loc = -1;
	for (i = 0; i < comp_count; i++) {
		total += counts[i];
		if (counts[i] > longest) {
			longest = counts[i];
			longest_loc = i;
		}
	}

	printf("Longest LCS: %d, is the %dth pair in the file\n", longest, longest_loc);
	printf("Average: %Lf\n",((long double) total)/comp_count);
	fclose(f);
	free(counts);
	return 0;
}

