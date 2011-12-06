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

#define NUM_THREADS 9
#define WORKER_THREADS 8
#define INPUT_THREADS 1
#define QUEUE_SIZE 40000
#define TRUE 1
#define FALSE 0
 
FILE *f;
char **queue1;
char **queue2;
int *lens1;
int *lens2;
int *counts;
int comp_count;
int offset = 0;
pthread_mutex_t mutex_count;
int input_num = 1;
int stop = FALSE;
int last_run = FALSE;


int MCSLength(char *str1, int len1, char* str2, int len2) {
	int** arr = malloc(sizeof(int*)*(len1+1));
	int i, j, local_max = 0;
	for (i = 0; i <= len1; i++)
		arr[i] = calloc(len2+1, sizeof(int));
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
 * read "gene" until we reach the next header. return int of num of chars in buff
 */
int readLine(char *buff) {
	int readchars = 0;
	//buff = malloc(sizeof(char)*buffsize);
	int commentline = 0, startedgene = 0;
	char c;
	do {
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
				if (commentline == 0 ) {
					startedgene = 1;
					if (c != EOF)
						buff[readchars++] = c;
				}
		}
	} while (c != EOF);
	return readchars;
}

void input_thread(){
	if(last_run == TRUE){return;}
	printf("input\n");
	if(input_num == 1){
		queue1 = malloc(sizeof(char*)*QUEUE_SIZE);
		lens1 = calloc(sizeof(int),QUEUE_SIZE);
		int i;
		for (i = 0; i < QUEUE_SIZE; i++) {
			queue1[i] = calloc(sizeof(char),100000);
			lens1[i] = readLine(queue1[i]);
		}
	
	}
	else if(input_num ==2){
		queue2 = malloc(sizeof(char*)*QUEUE_SIZE);
		lens2 = calloc(sizeof(int),QUEUE_SIZE);
		int i;
		for (i = 0; i < QUEUE_SIZE; i++) {
				queue2[i] = calloc(sizeof(char),100000);
				lens2[i] = readLine(queue2[i]);
		}
	
	}
}

void *threaded_count(void* myId) {
	
	if((int)myId == 0){
		input_thread();
	}
	else{
		int local_counts[QUEUE_SIZE/WORKER_THREADS/2];
		int local_count = 0;
		int startPos = ((int) myId) * (QUEUE_SIZE/WORKER_THREADS);
		int endPos = startPos + (QUEUE_SIZE/WORKER_THREADS);

		int i, j;
		for (i = 0; i < QUEUE_SIZE/WORKER_THREADS/2; i++) {
			local_counts[i] = 0;
			j = startPos + (i*2);
			if(input_num == 1){
				if ((lens2[j] != 0) && (lens2[j+1] != 0)) {
					local_counts[i] = MCSLength(queue2[j], lens2[j], queue2[j+1], lens2[j+1]);
				}
				local_count++;
			}else if(input_num == 2){
				if ((lens1[j] != 0) && (lens1[j+1] != 0)) {
					local_counts[i] = MCSLength(queue1[j], lens1[j], queue1[j+1], lens1[j+1]);
				}
				local_count++;
			}
			else
				break;
		}
		pthread_mutex_lock (&mutex_count);
		for (i = 0; i < QUEUE_SIZE/WORKER_THREADS/2; i++) {
			counts[(offset/2) + (startPos/2) + i] = local_counts[i];
		}
		printf("local_count: %d\ncomp_count: %d\n",local_count, comp_count);
		comp_count += local_count;
		pthread_mutex_unlock(&mutex_count);
		return (void *) 0;
	}
}

int main() {
	f = fopen("dna-small","r");
	//pthread
	int i, j, k, rc;
	pthread_t threads[NUM_THREADS];
	pthread_attr_t attr;
	void *status;
	
	stop = FALSE;
	last_run = FALSE;
	do {
		counts = (int*) realloc(counts, (QUEUE_SIZE + offset)/2 * sizeof(int));

		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
		pthread_mutex_init(&mutex_count, NULL);
		
		if(offset == 0){
			input_thread();
		}
		
		if(input_num == 1){
			input_num = 2;
		}else if(input_num == 2){
			input_num = 1;
		}
		
		for (i = 0; i < NUM_THREADS; i++) {
			rc = pthread_create(&threads[i], &attr, threaded_count, (void *) i);
			if (rc) {
				printf("Error");
				exit(-1);
			}
		}

//		pthread_attr_destroy(&attr);
		
		
		
		char* temp = malloc(sizeof(char)*1000);
		
		for(i = 0; i < strnlen(queue1[0], 1000); i++){
			temp = queue1[i];
		}
		
		for (j = 0; j < NUM_THREADS; j++) {
			rc = pthread_join(threads[j], &status);
			if (rc) {
				printf("Error 2");
				exit(-1);
			}
		}
		queue1[0] = temp;
		pthread_mutex_destroy(&mutex_count);
		for (k = 0; k < QUEUE_SIZE; k++) {
			if((input_num == 1) && (queue2[k] != NULL)){
				free(queue2[k]);
			}else if((input_num == 2) && (queue1[k] != NULL)){
				free(queue1[k]);
			}
		}

		pthread_attr_destroy(&attr); //moved this from previous comment

		if((input_num == 1) && (queue2 != NULL) && (lens2 != NULL)){
			free(queue2);
			free(lens2);
		}else if((input_num == 2) && (queue1 != NULL) && (lens1 != NULL)){
			free(queue1);
			free(lens1);
		}

		//int out = MCSLength(str1, len1, str2, len2);
		offset += QUEUE_SIZE;
		if(feof(f)){
			if(last_run == TRUE){
				stop = TRUE;
			}
			last_run = TRUE;
		}
	} while (stop == FALSE);
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

