#  Preload library with floating point exception interception 
#  aggregation via FPE sticky behavior and trap-and-emulate
#

all: fpe_preload.so test_fpe_preload trace_print


fpe_preload.so: fpe_preload.c trace_record.h
	gcc -O2 -Wall -fno-strict-aliasing -fPIC -shared fpe_preload.c -lm -ldl  -o fpe_preload.so

test_fpe_preload: test_fpe_preload.c
	gcc -O2 -Wall -fno-strict-aliasing -pthread test_fpe_preload.c -lm -o test_fpe_preload

libtrace.a: libtrace.c libtrace.h trace_record.h
	gcc -O2 -Wall -fno-strict-aliasing -c libtrace.c -o libtrace.o
	ar ruv libtrace.a libtrace.o

trace_print: libtrace.a trace_print.c
	gcc -O2 -Wall -fno-strict-aliasing trace_print.c libtrace.a -o trace_print



clean:
	-rm fpe_preload.so test_fpe_preload libtrace.o libtrace.a trace_print
	-rm __test_fpe_preload.*.fpemon
	-rm __sleepy.*fpemon
	-rm __dopey.*.fpemon
	-rm dopey sleepy
	-rm mpfr_test

test: fpe_preload.so test_fpe_preload
	@echo ==================================
	-FPE_MODE=individual LD_PRELOAD=./fpe_preload.so ./test_fpe_preload
	@echo ==================================
	-FPE_MODE=aggregate LD_PRELOAD=./fpe_preload.so ./test_fpe_preload
	@echo ==================================
	-FPE_MODE=individual FPE_AGGRESSIVE=yes LD_PRELOAD=./fpe_preload.so ./test_fpe_preload
	@echo ==================================
	-FPE_MODE=aggregate FPE_AGGRESSIVE=yes LD_PRELOAD=./fpe_preload.so ./test_fpe_preload
	@echo ==================================
	-TEST_FPE_BREAK_GENERAL_SIGNAL=1 FPE_MODE=individual LD_PRELOAD=./fpe_preload.so ./test_fpe_preload
	@echo ==================================
	-TEST_FPE_BREAK_GENERAL_SIGNAL=1 FPE_MODE=aggregate LD_PRELOAD=./fpe_preload.so ./test_fpe_preload
	@echo ==================================
	-TEST_FPE_BREAK_GENERAL_SIGNAL=1 FPE_MODE=individual FPE_AGGRESSIVE=yes LD_PRELOAD=./fpe_preload.so ./test_fpe_preload
	@echo ==================================
	-TEST_FPE_BREAK_GENERAL_SIGNAL=1 FPE_MODE=aggregate FPE_AGGRESSIVE=yes LD_PRELOAD=./fpe_preload.so ./test_fpe_preload
	@echo ==================================
	-TEST_FPE_BREAK_FPE_SIGNAL=1 FPE_MODE=individual LD_PRELOAD=./fpe_preload.so ./test_fpe_preload
	@echo ==================================
	-TEST_FPE_BREAK_FPE_SIGNAL=1 FPE_MODE=aggregate LD_PRELOAD=./fpe_preload.so ./test_fpe_preload
	@echo ==================================
	-TEST_FPE_BREAK_FPE_SIGNAL=1 FPE_MODE=individual FPE_AGGRESSIVE=yes LD_PRELOAD=./fpe_preload.so ./test_fpe_preload
	@echo ==================================
	-TEST_FPE_BREAK_FPE_SIGNAL=1 FPE_MODE=aggregate FPE_AGGRESSIVE=yes LD_PRELOAD=./fpe_preload.so ./test_fpe_preload
	@echo ==================================
	-TEST_FPE_BREAK_FE_FUNC=1 FPE_MODE=individual LD_PRELOAD=./fpe_preload.so ./test_fpe_preload
	@echo ==================================
	-TEST_FPE_BREAK_FE_FUNC=1 FPE_MODE=aggregate LD_PRELOAD=./fpe_preload.so ./test_fpe_preload
	@echo ==================================
	-TEST_FPE_BREAK_FE_FUNC=1 FPE_MODE=individual FPE_AGGRESSIVE=yes LD_PRELOAD=./fpe_preload.so ./test_fpe_preload
	@echo ==================================
	-TEST_FPE_BREAK_FE_FUNC=1 FPE_MODE=aggregate FPE_AGGRESSIVE=yes LD_PRELOAD=./fpe_preload.so ./test_fpe_preload
	@echo ==================================

sleepy: sleepy.c
	gcc -pthread sleepy.c -o sleepy

test_sleepy: fpe_preload.so sleepy
	@echo ==================================
	-FPE_MODE=individual LD_PRELOAD=./fpe_preload.so FPE_POISSON=100000:100000 FPE_TIMER=real ./sleepy

dopey: dopey.c
	gcc -pthread dopey.c -o dopey

test_dopey: fpe_preload.so dopey
	@echo ==================================
	-FPE_MODE=individual LD_PRELOAD=./fpe_preload.so FPE_POISSON=100000:100000 FPE_TIMER=virtual ./dopey

mpfr_test: mpfr_test.c
	gcc -static -O2 -Wall -fno-strict-aliasing -fPIC mpfr_test.c -lm -lmpfr -lgmp -o mpfr_test

