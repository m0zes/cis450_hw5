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
#define MAX(a, b) (((a)>(b)) ? (a) : (b))
#define MIN(a, b) (((a)<(b)) ? (a) : (b))

int MCSLength(char *str1, int len1, char* str2, int len2) {
	int** arr = malloc(sizeof(int*)*(len1+1));
	int i, j, local_max = 0, index = 0;
	for (i = 0; i <= len1; i++)
		arr[i] = calloc(len2+1, sizeof(int));
	for (i = 1; i <= len1; i++) {
		for (j = 1; j <= len2; j++) {
			if (str1[i-1] == str2[j-1]) {
				arr[i][j] = arr[i-1][j-1] + 1;
				if (arr[i][j] > local_max) {
					local_max = arr[i][j];
					index = i - local_max;
				}
			}
		}
		int x,y;
		//for (x = 0; x <= len1; x++) {
		//	for (y = 0; y <= len2; y++) {
		//		printf("%d ", arr[x][y]);
		//	}
		//	printf("\n");
		//}
	}
	for (i = 0; i <= len1; i++)
		free(arr[i]);

	free(arr);
	for (i = index; i < index + local_max; i++)
		printf("%c", str1[i]);
	printf("\n");
	return local_max;
}

//int mcs(int id) {
//	int sp = ((int) id) * (ARRAY_SIZE / NUM_THREADS);
//	int ep = sp + (ARRAY_SIZE / NUM_THREADS);
//	
//}
/*
 * Read file, char by char. headers start with '>' or ';', ignore until newline.
 * read "gene" until we reach the next header. return int of num of chars in buff
 */
int readLine(char *buff) {
	int buffsize = 400, readchars = 0;
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
				if ( commentline == 0 ) {
					startedgene = 1;
					buff[readchars++] = c;
				}
		}
	} while (c != EOF);
	return readchars;
}

int main() {
	f = fopen("dna-small","r");
	int count = 0;
	do {
		char *str1 = malloc(sizeof(char)*4000);
		int len1 = readLine(str1);
		char *str2 = malloc(sizeof(char)*4000);
		printf("String1: %s\n", str1);
		int len2 = readLine(str2);
		printf("String2: %s\n", str2);
		int out = MCSLength(str1, len1, str2, len2);
		printf("matching is %d\n", out);
		free(str1);
		free(str2);
		count++;
	} while (!feof(f));
	printf("%d\n",count);
	fclose(f);
	return 0;
}

