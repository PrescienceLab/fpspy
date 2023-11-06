#  Part of FPSpy
#
#  Preload library with floating point exception interception 
#  aggregation via FPE sticky behavior and trap-and-emulate
#
#  Copyright (c) 2017 Peter Dinda - see LICENSE
#

#ARCH=x64
ARCH=arm64

CC = gcc
LD = ld
AR = ar

CFLAGS_FPSPY = -O2 -Wall -fno-strict-aliasing -fPIC -shared -Iinclude -Iinclude/$(ARCH) -D$(ARCH)
LDFLAGS_FPSPY =  -lm -ldl

CFLAGS_TOOL = -O2 -Wall -fno-strict-aliasing -Iinclude -Iinclude/$(ARCH)
LDFLAGS_TOOL =  -lm

CFLAGS_TEST = -O2 -Wall -fno-strict-aliasing -pthread
LDFLAGS_TEST =  -lm 

CFLAGS_ROUNDING = -O0 -Wall 
LDFLAGS_ROUNDING =  -lm


all: bin/$(ARCH)/fpspy.so bin/$(ARCH)/test_fpspy bin/$(ARCH)/trace_print bin/$(ARCH)/test_fpspy_rounding bin/$(ARCH)/sleepy bin/$(ARCH)/dopey




bin/$(ARCH)/fpspy.so: src/fpspy.c include/*.h src/$(ARCH)/*.c include/$(ARCH)/*.h
	$(CC) $(CFLAGS_FPSPY) src/fpspy.c src/$(ARCH)/*.c $(LDFLAGS_FPSPY) -o bin/$(ARCH)/fpspy.so

bin/$(ARCH)/test_fpspy: test/test_fpspy.c
	$(CC) $(CFLAGS_TEST) test/test_fpspy.c $(LDFLAGS_TEST) -o bin/$(ARCH)/test_fpspy

lib/$(ARCH)/libtrace.a: src/libtrace.c include/libtrace.h include/trace_record.h
	$(CC) $(CFLAGS_TOOL) -c src/libtrace.c -o lib/$(ARCH)/libtrace.o
	$(AR) ruv lib/$(ARCH)/libtrace.a lib/$(ARCH)/libtrace.o
	rm lib/$(ARCH)/libtrace.o

bin/$(ARCH)/trace_print: lib/$(ARCH)/libtrace.a src/trace_print.c
	$(CC) $(CFLAGS_TOOL) src/trace_print.c lib/$(ARCH)/libtrace.a $(LDFLAGS_TOOL) -o bin/$(ARCH)/trace_print



test: bin/$(ARCH)/fpspy.so bin/$(ARCH)/test_fpspy
	@echo ==================================
	-FPSPY_MODE=individual LD_PRELOAD=./bin/$(ARCH)/fpspy.so ./bin/$(ARCH)/test_fpspy
	@echo ==================================
	-FPSPY_MODE=aggregate LD_PRELOAD=./bin/$(ARCH)/fpspy.so ./bin/$(ARCH)/test_fpspy
	@echo ==================================
	-FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so ./bin/$(ARCH)/test_fpspy
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_AGGRESSIVE=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so ./bin/$(ARCH)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_GENERAL_SIGNAL=1 FPSPY_MODE=individual LD_PRELOAD=./bin/$(ARCH)/fpspy.so ./bin/$(ARCH)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_GENERAL_SIGNAL=1 FPSPY_MODE=aggregate LD_PRELOAD=./bin/$(ARCH)/fpspy.so ./bin/$(ARCH)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_GENERAL_SIGNAL=1 FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so ./bin/$(ARCH)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_GENERAL_SIGNAL=1 FPSPY_MODE=aggregate FPSPY_AGGRESSIVE=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so ./bin/$(ARCH)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_FPE_SIGNAL=1 FPSPY_MODE=individual LD_PRELOAD=./bin/$(ARCH)/fpspy.so ./bin/$(ARCH)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_FPE_SIGNAL=1 FPSPY_MODE=aggregate LD_PRELOAD=./bin/$(ARCH)/fpspy.so ./bin/$(ARCH)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_FPE_SIGNAL=1 FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so ./bin/$(ARCH)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_FPE_SIGNAL=1 FPSPY_MODE=aggregate FPSPY_AGGRESSIVE=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so ./bin/$(ARCH)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_FE_FUNC=1 FPSPY_MODE=individual LD_PRELOAD=./bin/$(ARCH)/fpspy.so ./bin/$(ARCH)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_FE_FUNC=1 FPSPY_MODE=aggregate LD_PRELOAD=./bin/$(ARCH)/fpspy.so ./bin/$(ARCH)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_FE_FUNC=1 FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so ./bin/$(ARCH)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_FE_FUNC=1 FPSPY_MODE=aggregate FPSPY_AGGRESSIVE=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so ./bin/$(ARCH)/test_fpspy
	@echo ==================================

bin/$(ARCH)/sleepy: test/sleepy.c
	$(CC) $(CFLAGS_TEST) test/sleepy.c $(LDFLAGS_TEST) -o bin/$(ARCH)/sleepy

test_sleepy: bin/$(ARCH)/fpspy.so bin/$(ARCH)/sleepy
	@echo ==================================
	-FPSPY_MODE=individual FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_POISSON=100000:100000 FPSPY_TIMER=real ./bin/$(ARCH)/sleepy

bin/$(ARCH)/dopey: test/dopey.c
	$(CC) $(CFLAGS_TEST) test/dopey.c $(LDFLAGS_TEST) -o bin/$(ARCH)/dopey

test_dopey: bin/$(ARCH)/fpspy.so bin/$(ARCH)/dopey
	@echo ==================================
	-FPSPY_MODE=individual  FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_POISSON=100000:100000 FPSPY_TIMER=virtual ./bin/$(ARCH)/dopey

bin/$(ARCH)/test_fpspy_rounding: test/test_fpspy_rounding.c
	$(CC) $(CFLAGS_ROUNDING) test/test_fpspy_rounding.c $(LDFLAGS_ROUNDING) -o bin/$(ARCH)/test_fpspy_rounding 


test_rounding: bin/$(ARCH)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_FORCE_ROUNDING=positive bin/$(ARCH)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_FORCE_ROUNDING=negative bin/$(ARCH)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_FORCE_ROUNDING=zero bin/$(ARCH)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_FORCE_ROUNDING=nearest bin/$(ARCH)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_FORCE_ROUNDING="positive;daz" bin/$(ARCH)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_FORCE_ROUNDING="negative;daz" bin/$(ARCH)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_FORCE_ROUNDING="zero;daz" bin/$(ARCH)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_FORCE_ROUNDING="nearest;daz" bin/$(ARCH)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_FORCE_ROUNDING="positive;ftz" bin/$(ARCH)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_FORCE_ROUNDING="negative;ftz" bin/$(ARCH)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_FORCE_ROUNDING="zero;ftz" bin/$(ARCH)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_FORCE_ROUNDING="nearest;ftz" bin/$(ARCH)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_FORCE_ROUNDING="positive;daz;ftz" bin/$(ARCH)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_FORCE_ROUNDING="negative;daz;ftz" bin/$(ARCH)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_FORCE_ROUNDING="zero;daz;ftz" bin/$(ARCH)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH)/fpspy.so FPSPY_FORCE_ROUNDING="nearest;daz;ftz" bin/$(ARCH)/test_fpspy_rounding

clean:
	-rm bin/$(ARCH)/fpspy.so bin/$(ARCH)/test_fpspy bin/$(ARCH)/test_fpspy_rounding lib/$(ARCH)/libtrace.o lib/$(ARCH)/libtrace.a bin/$(ARCH)/trace_print
	-rm __test_fpspy.*.fpemon
	-rm __test_fpspy_rounding.*.fpemon
	-rm __sleepy.*fpemon
	-rm __dopey.*.fpemon
	-rm bin/$(ARCH)/*dopey bin/$(ARCH)/*sleepy

