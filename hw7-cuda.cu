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
#define NUM_THREADS 1000
#endif

#ifndef WORK_UNIT
#define WORK_UNIT 100
#endif

#define QUEUE_SIZE NUM_THREADS*WORK_UNIT

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
		arr[i] = (int*)malloc((len2+1) *sizeof(int));
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
__global__ void threaded_count( int* dev_counts, char** dev_queue, int* dev_lens, int perThread, int totalThreads) {
	int local_counts[QUEUE_SIZE/NUM_THREADS/2];
	int local_count = 0;
	int startPos = ((int) 0) * (QUEUE_SIZE/NUM_THREADS);
	int endPos = startPos + (QUEUE_SIZE/NUM_THREADS);

	int i, j;
	for (i = 0; i < QUEUE_SIZE/NUM_THREADS/2; i++) {
		local_counts[i] = 0;
		j = startPos + (i*2);
		if ((dev_lens[j] != 0) && (dev_lens[j+1] != 0)) {
			local_counts[i] = MCSLength(dev_queue[j], dev_lens[j], dev_queue[j+1], dev_lens[j+1]);
			local_count++;
		}
		else
			break;
	}
	for (i = 0; i < QUEUE_SIZE/NUM_THREADS/2; i++) {
		dev_counts[(offset/2) + (startPos/2) + i] = local_counts[i];
	}
	comp_count += local_count;
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
	char **dev_queue;
	int *dev_lens;
	int *dev_counts;
	//pthread
	int i, rc;
	void *status;
	int perThread = WORK_UNIT;
	int totalSize = QUEUE_SIZE;
	int size = NUM_THREADS;
	int numThreadsPerBlock = 100;
	int numBlocks = size / numThreadsPerBlock;
	int totalThreads = numThreadsPerBlock * numBlocks;
	
	do {
		queue = (char**)malloc(sizeof(char*)*QUEUE_SIZE);
		cudaMalloc((void**)&dev_queue, sizeof(char*)*QUEUE_SIZE);

		lens = (int*)calloc(sizeof(int),QUEUE_SIZE);
		cudaMalloc((void**)&dev_lens, sizeof(int)*QUEUE_SIZE);

		int *temp_counts = (int*) realloc(counts, (QUEUE_SIZE + offset)/2 * sizeof(int));
		if (( queue == 0 ) || (lens == 0) || (temp_counts == 0)) {
			printf("Couldn't allocate memory for the work queues\n");
			exit(-1);
		}
		counts = temp_counts;
		for (i = 0; i < QUEUE_SIZE; i++) {
			lens[i] = readLine(queue, i);
			if (( queue[i] == 0 )) {
				printf("Couldn't allocate memory for the work subqueues\n");
				exit(-1);
			}
			cudaMalloc((void*)&(dev_queue[i]), (lens[i])*sizeof(char));
			cudaMemcpy(dev_queue[i], queue[i], lens[i]*sizeof(char), cudaMemcpyHostToDevice);
		}
		cudaMemcpy(dev_lens, lens, QUEUE_SIZE*sizeof(int), cudaMemcpyHostToDevice);

		cudaMalloc((void**)&dev_counts, (QUEUE_SIZE*sizeof(int))/2);
		cudaMemset( dev_counts, 0, (QUEUE_SIZE*sizeof(int))/2);

		dim3 dimGrid(numBlocks);
		dim3 dimBlock(numThreadsPerBlock);
		threaded_count<<< dimGrid, dimBlock >>>(dev_counts, dev_queue, dev_lens, perThread, totalThreads);
		cudaThreadSynchronize();
		int* temp = (int*) malloc(sizeof(int)*QUEUE_SIZE/2);
		cudaMemcpy(temp, dev_counts, (QUEUE_SIZE*sizeof(int))/2, cudaMemcpyDeviceToHost);
		for (i = 0; i < QUEUE_SIZE/2; i++)
			counts[offset+i] = temp[i];

		for (i = 0; i < QUEUE_SIZE; i++) {
			cudaFree(dev_queue[i]);
			free(queue[i]);
		}
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
	pthread_exit(NULL);
	return 0;
}

