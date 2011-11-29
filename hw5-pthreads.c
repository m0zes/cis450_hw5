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

FILE *f = "dna-small";
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
				printf("%c matches\n", str1[i-1]);
				arr[i][j] = arr[i-1][j-1] + 1;
				if (arr[i][j] > local_max) {
					local_max = arr[i][j];
					index = i - local_max;
				}
			}
			printf("Array[%d][%d] is %d\n",i,j,arr[i][j]);
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
int main() {
	char *str1 = "MAQQRRGGFKRRKKVDFIAANKIEVVDYKDTELLKRFISERGKILPRRVTGTSAKNQRKVVNAIKRARVMALLPFVAEDQN";
	char *str2 = "MASTQNIVEEVQKMLDTYDTNKDGEITKAEAVEYFKGKKAFNPERSAIYLFQVYDKDNDGKITIKELAGDIDFDKALKEYKEKQAKSKQQEAEVEEDIEAFILRHNKDDNTDITKDELIQGFKETGAKDPEKSANFILTEMDTNKDGTITVKELRVYYQKVQKLLNPDQ";
	int out = MCSLength(str1, 82, str2, 171);
	printf("matching is %d\n", out);
	return 0;
}

