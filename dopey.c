/*

  Part of FPSpy

  Copyright (c) 2017 Peter A. Dinda - see LICENSE

*/


#include <stdlib.h>

#define N (1024ULL*1024ULL*256ULL)

double A[N];

volatile double sum;

int main()
{
    long i;

    for (i=0;i<N;i++) {
	A[i] = drand48();
    }
    while(1) {
	sum = drand48();
	for (i=0;i<N;i++) {
	    sum += A[i];
	}
    }
}
