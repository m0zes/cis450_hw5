#include <stdio.h>
#include <stdlib.h>
/*
 * Length of "lines changes with every protein"
 */

FILE *f = "dna-small";

int mcs(int id) {
	int sp = ((int) id) * (ARRAY_SIZE / NUM_THREADS);
	int ep = sp + (ARRAY_SIZE / NUM_THREADS);

}
