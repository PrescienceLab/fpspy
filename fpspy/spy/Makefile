#  Preload library with floating point exception interception 
#  aggregation via FPE sticky behavior and trap-and-emulate
#

all: fpe_preload.so test_fpe_preload

fpe_preload.so: fpe_preload.c
	gcc -Wall -O2 -fPIC -shared fpe_preload.c -lm -ldl  -o fpe_preload.so

test_fpe_preload: test_fpe_preload.c
	gcc -Wall -O2 -fno-strict-aliasing test_fpe_preload.c -lm -o test_fpe_preload

clean:
	rm fpe_preload.so test_fpe_preload

test: fpe_preload.so test_fpe_preload
	LD_PRELOAD=./fpe_preload.so ./test_fpe_preload
