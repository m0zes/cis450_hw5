/* 
 * mpi hw5, written by Adam Tygart abd Ryan Hershberger
 * Could be further optimized by pipelining read operations and not cyclically creating/destroying child threads
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
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
int *local_counts;
int *local_lens;
int **reduce_vars;

int num_threads;

#ifndef WORK_UNIT
#define WORK_UNIT 4800
#endif

int queue_size = 0;

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
void *threaded_count(int myId) {
	int *local_counts = malloc((queue_size/num_threads/2)*sizeof(int));
	int local_count = 0;
	int startPos = (myId) * (queue_size/num_threads);
	int endPos = startPos + (queue_size/num_threads);

	int i, j;
	for (i = 0; i < queue_size/num_threads/2; i++) {
		local_counts[i] = 0;
		j = startPos + (i*2);
		if ((lens[j] != 0) && (lens[j+1] != 0)) {
			local_counts[i] = MCSLength(queue[j], lens[j], queue[j+1], lens[j+1]);
			local_count++;
		}
		else
			break;
	}
	if (myId == 0 ) {
		for (j = 0; j < local_count; j++) {
			reduce_vars[0][j] = local_counts[j];
		}
		comp_count += local_count;
		printf("Copied local (rank 0) counts to reduce_vars[0]\n");
		for (j = 1; j < num_threads; j++) {
			int recv_count;
			MPI_Status status;
			MPI_Recv(&recv_count, 1, MPI_INT, j, 1105, MPI_COMM_WORLD, &status);
			MPI_Recv(reduce_vars[i], recv_count, MPI_INT, j, 1106, MPI_COMM_WORLD, &status);
			printf("Received (rank %d) counts to reduce_vars[%d]\n", j, j);
			comp_count += recv_count;
		}
	} else {
		printf("Sending (rank %d) counts to reduce_vars[%d]\n", myId, myId);
		MPI_Send(&local_count, 1, MPI_INT, 0, 1105, MPI_COMM_WORLD);
		MPI_Send(local_counts, local_count, MPI_INT, 0, 1106, MPI_COMM_WORLD);
		free(local_counts);
	}
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
	int i, rc;
	MPI_Status status;
	MPI_Init(&argc, &argv);
	int rank, size, tag;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	num_threads = size;
	queue_size = WORK_UNIT * size;
	
	printf("WORK_UNIT = %d, queue_size = %d, size = %d\n", WORK_UNIT, queue_size, size);
	do {
		int t = 0;
		if (rank == 0) {
			queue = malloc(sizeof(char*)*queue_size);
			lens = calloc(sizeof(int),queue_size);
			int *temp_counts = (int*) realloc(counts, (queue_size + offset)/2 * sizeof(int));
			if (( queue == 0 ) || (lens == 0) || (temp_counts == 0)) {
				printf("Couldn't allocate memory for the work queues\n");
				exit(-1);
			}
			counts = temp_counts;
			reduce_vars = malloc(sizeof(int*)*size);
			for (i = 0; i < queue_size; i++) {
				lens[i] = readLine(queue, i);
				t += lens[i];
				if (( queue[i] == 0 )) {
					printf("Couldn't allocate memory for the work subqueues\n");
					exit(-1);
				}
				if (i < size)
					reduce_vars[i] = calloc(sizeof(int),queue_size);

			}
		}

		MPI_Bcast(queue, queue_size * t, MPI_CHAR, 0, MPI_COMM_WORLD);
		MPI_Bcast(lens, queue_size, MPI_INT, 0, MPI_COMM_WORLD);
		threaded_count(rank);

		if (rank == 0) {
			for (i = 0; i < size; i++) {
				int j;
				int startPos = i * (queue_size/num_threads);
				for (j = 0; j < queue_size/num_threads/2; j++) {
					printf("reduce_vars[%d][%d] = %c\n", i, j, reduce_vars[i][j]);
					counts[(offset/2) + (startPos/2) + j] = reduce_vars[i][j];
				}
				free(reduce_vars[i]);
			}
			for (i = 0; i < queue_size; i++) {
				free(queue[i]);
			}
			free(reduce_vars);
			if (feof(f))
				break;
		}
		free(queue);
		free(lens);

		offset += queue_size;
	} while (0);
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
	MPI_Finalize();
	return 0;
}

