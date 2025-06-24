#  Part of FPSpy
#
#  Preload library with floating point exception interception
#  aggregation via FPE sticky behavior and trap-and-emulate
#
#  Copyright (c) 2017 Peter Dinda - see LICENSE
#

-include config.mk

ifeq ($(CONFIG_TOOLCHAIN_PREFIX),"")
  CONFIG_TOOLCHAIN_PREFIX=
endif

CC = $(CONFIG_TOOLCHAIN_PREFIX)gcc
LD = $(CONFIG_TOOLCHAIN_PREFIX)ld
AR = $(CONFIG_TOOLCHAIN_PREFIX)ar

ifeq ($(CONFIG_ARCH_X64),1)
   ARCH_DIR=x64
else ifeq ($(CONFIG_ARCH_ARM64),1)
   ARCH_DIR=arm64
else ifeq ($(CONFIG_ARCH_RISCV64),1)
   ARCH_DIR=riscv64
endif


CFLAGS_FPSPY = -g -O2 -Wall -fno-strict-aliasing -fPIC -shared -Iinclude -Iinclude/$(ARCH_DIR) -D$(ARCH_DIR)
LDFLAGS_FPSPY =  -lm -ldl

CFLAGS_TOOL = -g -O2 -Wall -fno-strict-aliasing -Iinclude -Iinclude/$(ARCH_DIR)
LDFLAGS_TOOL =  -lm

CFLAGS_TEST = -g -O2 -Wall -fno-strict-aliasing -pthread -D$(ARCH_DIR)
LDFLAGS_TEST =  -lm

CFLAGS_ROUNDING = -g -O0 -Wall -D$(ARCH_DIR)
LDFLAGS_ROUNDING =  -lm


all: bin/$(ARCH_DIR)/fpspy.so bin/$(ARCH_DIR)/test_fpspy bin/$(ARCH_DIR)/trace_print bin/$(ARCH_DIR)/test_fpspy_rounding bin/$(ARCH_DIR)/sleepy bin/$(ARCH_DIR)/dopey




bin/$(ARCH_DIR)/fpspy.so: src/fpspy.c include/*.h src/$(ARCH_DIR)/*.c src/$(ARCH_DIR)/*.S include/$(ARCH_DIR)/*.h
	$(CC) $(CFLAGS_FPSPY) src/fpspy.c src/$(ARCH_DIR)/*.c src/$(ARCH_DIR)/*.S $(LDFLAGS_FPSPY) -o bin/$(ARCH_DIR)/fpspy.so

bin/$(ARCH_DIR)/test_fpspy: test/test_fpspy.c
	$(CC) $(CFLAGS_TEST) test/test_fpspy.c $(LDFLAGS_TEST) -o bin/$(ARCH_DIR)/test_fpspy

lib/$(ARCH_DIR)/libtrace.a: src/libtrace.c include/libtrace.h include/trace_record.h
	$(CC) $(CFLAGS_TOOL) -c src/libtrace.c -o lib/$(ARCH_DIR)/libtrace.o
	$(AR) ruv lib/$(ARCH_DIR)/libtrace.a lib/$(ARCH_DIR)/libtrace.o
	rm lib/$(ARCH_DIR)/libtrace.o

bin/$(ARCH_DIR)/trace_print: lib/$(ARCH_DIR)/libtrace.a src/trace_print.c
	$(CC) $(CFLAGS_TOOL) src/trace_print.c lib/$(ARCH_DIR)/libtrace.a $(LDFLAGS_TOOL) -o bin/$(ARCH_DIR)/trace_print



test: bin/$(ARCH_DIR)/fpspy.so bin/$(ARCH_DIR)/test_fpspy
	@echo ==================================
	-FPSPY_MODE=individual LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so ./bin/$(ARCH_DIR)/test_fpspy
	@echo ==================================
	-FPSPY_MODE=aggregate LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so ./bin/$(ARCH_DIR)/test_fpspy
	@echo ==================================
	-FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so ./bin/$(ARCH_DIR)/test_fpspy
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_AGGRESSIVE=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so ./bin/$(ARCH_DIR)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_GENERAL_SIGNAL=1 FPSPY_MODE=individual LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so ./bin/$(ARCH_DIR)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_GENERAL_SIGNAL=1 FPSPY_MODE=aggregate LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so ./bin/$(ARCH_DIR)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_GENERAL_SIGNAL=1 FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so ./bin/$(ARCH_DIR)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_GENERAL_SIGNAL=1 FPSPY_MODE=aggregate FPSPY_AGGRESSIVE=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so ./bin/$(ARCH_DIR)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_FPE_SIGNAL=1 FPSPY_MODE=individual LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so ./bin/$(ARCH_DIR)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_FPE_SIGNAL=1 FPSPY_MODE=aggregate LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so ./bin/$(ARCH_DIR)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_FPE_SIGNAL=1 FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so ./bin/$(ARCH_DIR)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_FPE_SIGNAL=1 FPSPY_MODE=aggregate FPSPY_AGGRESSIVE=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so ./bin/$(ARCH_DIR)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_FE_FUNC=1 FPSPY_MODE=individual LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so ./bin/$(ARCH_DIR)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_FE_FUNC=1 FPSPY_MODE=aggregate LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so ./bin/$(ARCH_DIR)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_FE_FUNC=1 FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so ./bin/$(ARCH_DIR)/test_fpspy
	@echo ==================================
	-TEST_FPSPY_BREAK_FE_FUNC=1 FPSPY_MODE=aggregate FPSPY_AGGRESSIVE=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so ./bin/$(ARCH_DIR)/test_fpspy
	@echo ==================================

bin/$(ARCH_DIR)/sleepy: test/sleepy.c
	$(CC) $(CFLAGS_TEST) test/sleepy.c $(LDFLAGS_TEST) -o bin/$(ARCH_DIR)/sleepy

test_sleepy: bin/$(ARCH_DIR)/fpspy.so bin/$(ARCH_DIR)/sleepy
	@echo ==================================
	-FPSPY_MODE=individual FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_POISSON=100000:100000 FPSPY_TIMER=real ./bin/$(ARCH_DIR)/sleepy

bin/$(ARCH_DIR)/dopey: test/dopey.c
	$(CC) $(CFLAGS_TEST) test/dopey.c $(LDFLAGS_TEST) -o bin/$(ARCH_DIR)/dopey

test_dopey: bin/$(ARCH_DIR)/fpspy.so bin/$(ARCH_DIR)/dopey
	@echo ==================================
	-FPSPY_MODE=individual  FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_POISSON=100000:100000 FPSPY_TIMER=virtual ./bin/$(ARCH_DIR)/dopey

bin/$(ARCH_DIR)/test_fpspy_rounding: test/test_fpspy_rounding.c
	$(CC) $(CFLAGS_ROUNDING) test/test_fpspy_rounding.c $(LDFLAGS_ROUNDING) -o bin/$(ARCH_DIR)/test_fpspy_rounding


test_rounding: bin/$(ARCH_DIR)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_FORCE_ROUNDING=positive bin/$(ARCH_DIR)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_FORCE_ROUNDING=negative bin/$(ARCH_DIR)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_FORCE_ROUNDING=zero bin/$(ARCH_DIR)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_FORCE_ROUNDING=nearest bin/$(ARCH_DIR)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_FORCE_ROUNDING="positive;daz" bin/$(ARCH_DIR)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_FORCE_ROUNDING="negative;daz" bin/$(ARCH_DIR)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_FORCE_ROUNDING="zero;daz" bin/$(ARCH_DIR)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_FORCE_ROUNDING="nearest;daz" bin/$(ARCH_DIR)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_FORCE_ROUNDING="positive;ftz" bin/$(ARCH_DIR)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_FORCE_ROUNDING="negative;ftz" bin/$(ARCH_DIR)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_FORCE_ROUNDING="zero;ftz" bin/$(ARCH_DIR)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_FORCE_ROUNDING="nearest;ftz" bin/$(ARCH_DIR)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_FORCE_ROUNDING="positive;daz;ftz" bin/$(ARCH_DIR)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_FORCE_ROUNDING="negative;daz;ftz" bin/$(ARCH_DIR)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_FORCE_ROUNDING="zero;daz;ftz" bin/$(ARCH_DIR)/test_fpspy_rounding
	@echo ==================================
	-FPSPY_MODE=aggregate FPSPY_DISABLE_PTHREADS=yes LD_PRELOAD=./bin/$(ARCH_DIR)/fpspy.so FPSPY_FORCE_ROUNDING="nearest;daz;ftz" bin/$(ARCH_DIR)/test_fpspy_rounding

clean:
	-rm bin/$(ARCH_DIR)/fpspy.so bin/$(ARCH_DIR)/test_fpspy bin/$(ARCH_DIR)/test_fpspy_rounding lib/$(ARCH_DIR)/libtrace.o lib/$(ARCH_DIR)/libtrace.a bin/$(ARCH_DIR)/trace_print
	-rm __test_fpspy.*.fpemon
	-rm __test_fpspy_rounding.*.fpemon
	-rm __sleepy.*fpemon
	-rm __dopey.*.fpemon
	-rm bin/$(ARCH_DIR)/*dopey bin/$(ARCH_DIR)/*sleepy


menuconfig:
	@scripts/menuconfig.py

defconfig:
	@rm -f .config
	@echo "Using default configuration"
	@echo "q" | env TERM=xterm-256color python3 scripts/menuconfig.py >/dev/null

cfg:
	@scripts/menuconfig.py

# run `make reconfig` if `.config` has changed.
reconfig:
	@touch .config
	@echo -e "q" | env TERM=xterm-256color python3 scripts/menuconfig.py >/dev/null

