#  Part of FPSpy
#
#  Preload library with floating point exception interception 
#  aggregation via FPE sticky behavior and trap-and-emulate
#
#  Copyright (c) 2017 Peter Dinda - see LICENSE
#

#ARCH=x64
#ARCH=arm64
ARCH=riscv64

# hard coded assuming we are doing cross-compilation
ifeq ($(ARCH),riscv64)
  PREFIX=riscv64-unknown-elf-
else
  PREFIX=
endif

CC = $(PREFIX)gcc
LD = $(PREFIX)ld
AR = $(PREFIX)ar

CFLAGS = -O0 -Wall -fno-strict-aliasing -g -D__riscv_xlen=64 -mcmodel=medany \
				 -D$(ARCH) -Iutils/$(ARCH)/include/
LDFLAGS = -lc -lgcc -static -Wall -fno-strict-aliasing -D$(ARCH) \
					-DPREALLOCATE=1 -std=gnu99 -fno-common -fno-builtin-printf \
					-fvisibility=hidden -nostartfiles -Tutils/$(ARCH)/link.ld

BIN = ../bin
BUILD = ./build

all: $(BIN)/$(ARCH)/test_fpspy $(BIN)/$(ARCH)/test_fpspy_rounding $(BIN)/$(ARCH)/sleepy $(BIN)/$(ARCH)/dopey

$(BUILD)/syscalls.o: utils/$(ARCH)/src/syscalls.c
	$(CC) $(CFLAGS) -c -o $@ $^

$(BUILD)/crt.o: utils/$(ARCH)/src/crt.S
	$(CC) $(CFLAGS) -c -o $@ $^

$(BUILD)/test_fpspy.o: test_fpspy.c
	$(CC) $(CFLAGS) -c -o $@ $^

$(BUILD)/sleepy.o: sleepy.c
	$(CC) $(CFLAGS) -c -o $@ $^

$(BUILD)/dopey.o: dopey.c
	$(CC) $(CFLAGS) -c -o $@ $^

$(BIN)/$(ARCH)/test_fpspy: $(BUILD)/syscalls.o $(BUILD)/crt.o $(BUILD)/test_fpspy.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN)/$(ARCH)/sleepy: $(BUILD)/syscalls.o $(BUILD)/crt.o $(BUILD)/sleepy.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN)/$(ARCH)/dopey: $(BUILD)/syscalls.o $(BUILD)/crt.o $(BUILD)/dopey.o
	$(CC) $(LDFLAGS) -o $@ $^


clean:
	rm -rf $(BUILD) && mkdir $(BUILD) 
	rm -f $(BIN)/$(ARCH)/test_fpspy
	rm -f $(BIN)/$(ARCH)/dopey $(BIN)/$(ARCH)/sleepy

