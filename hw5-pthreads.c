/* 
 * pthreaded hw5, written by Adam Tygart abd Ryan Hershberger
 * Could be further optimized by pipelining read operations and not cyclically creating/destroying child threads
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
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
char **queue;
int *lens;
int *counts;
int comp_count;
int offset = 0;
pthread_mutex_t mutex_count;

#ifndef NUM_THREADS
#define NUM_THREADS 8
#endif

#ifndef WORK_UNIT
#define WORK_UNIT 4800
#endif

#define QUEUE_SIZE NUM_THREADS*WORK_UNIT

/*
 * Calculate the LCS of the two strings.
 */
int MCSLength(char *str1, int len1, char* str2, int len2) {
	int** arr = malloc(sizeof(int*)*(len1+1));
	if ( arr == 0 ) {
		printf("Couldn't allocate memory for the MCS array\n");
		exit(-1);
	}
	int i, j, local_max = 0;
	for (i = 0; i <= len1; i++) {
		arr[i] = calloc(len2+1, sizeof(int));
		if ( arr[i] == 0 ) {
			printf("Couldn't allocate memory for the MCS subarray\n");
			exit(-1);
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
	buff[i] = malloc(sizeof(char)*buffSize);
	char c;
	do {
		if (((readchars) >= buffSize) && (buffSize != 0)) {
			buffSize += buffStepSize;
			char* temp_buff = realloc(buff[i],sizeof(char)*buffSize);
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
void *threaded_count(void* myId) {
	int local_counts[QUEUE_SIZE/NUM_THREADS/2];
	int local_count = 0;
	int startPos = ((int) myId) * (QUEUE_SIZE/NUM_THREADS);
	int endPos = startPos + (QUEUE_SIZE/NUM_THREADS);

	int i, j;
	for (i = 0; i < QUEUE_SIZE/NUM_THREADS/2; i++) {
		local_counts[i] = 0;
		j = startPos + (i*2);
		if ((lens[j] != 0) && (lens[j+1] != 0)) {
			local_counts[i] = MCSLength(queue[j], lens[j], queue[j+1], lens[j+1]);
			local_count++;
		}
		else
			break;
	}
	pthread_mutex_lock (&mutex_count);
	for (i = 0; i < QUEUE_SIZE/NUM_THREADS/2; i++) {
		counts[(offset/2) + (startPos/2) + i] = local_counts[i];
	}
	comp_count += local_count;
	pthread_mutex_unlock(&mutex_count);
	return (void *) 0;
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
	//pthread
	int i, rc;
	pthread_t threads[NUM_THREADS];
	pthread_attr_t attr;
	void *status;
	do {
		queue = malloc(sizeof(char*)*QUEUE_SIZE);
		lens = calloc(sizeof(int),QUEUE_SIZE);
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
		}
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
		pthread_mutex_init(&mutex_count, NULL);

		for (i = 0; i < NUM_THREADS; i++) {
			rc = pthread_create(&threads[i], &attr, threaded_count, (void *) i);
			if (rc) {
				printf("Error creating threads\n");
				exit(-1);
			}
		}

		pthread_attr_destroy(&attr);
		for (i = 0; i < NUM_THREADS; i++) {
			rc = pthread_join(threads[i], &status);
			if (rc) {
				printf("Error Joining threads\n");
				exit(-1);
			}
		}
		pthread_mutex_destroy(&mutex_count);
		for (i = 0; i < QUEUE_SIZE; i++) {
			free(queue[i]);
		}
		free(queue);
		free(lens);

		//int out = MCSLength(str1, len1, str2, len2);
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

