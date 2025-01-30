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
ENVIRONMENT=verilator

# hard coded assuming we are doing cross-compilation
ifeq ($(ARCH),riscv64)
	ifeq ($(ENVIRONMENT),verilator)
  		PREFIX=riscv64-unknown-elf-
		CFLAGS = -D__riscv_xlen=64 -mcmodel=medany 
		LDFLAGS = -DPREALLOCATE=1 -fno-common -fno-builtin-printf \
					-fvisibility=hidden -nostartfiles -Tutils/$(ARCH)/link.ld
	else
		PREFIX=riscv64-unknown-linux-gnu-
	endif
else
  PREFIX=
endif

CC = $(PREFIX)gcc
LD = $(PREFIX)ld
AR = $(PREFIX)ar

CFLAGS += -O0 -Wall -fno-strict-aliasing -g -D$(ARCH) -Iutils/$(ARCH)/include/
LDFLAGS += -lc -lgcc -static -Wall -fno-strict-aliasing -D$(ARCH) -std=gnu99

BIN = ../bin
BUILD = ./build

all: $(BIN)/$(ARCH)/ve_test_fpspy $(BIN)/$(ARCH)/ve_full_nanny \
	$(BIN)/$(ARCH)/ve_just_nanny $(BIN)/$(ARCH)/ve_no_nanny

$(BUILD)/syscalls.o: utils/$(ARCH)/src/syscalls.c
	$(CC) $(CFLAGS) -c -o $@ $^

$(BUILD)/crt.o: utils/$(ARCH)/src/crt.S
	$(CC) $(CFLAGS) -c -o $@ $^

$(BUILD)/test_fpspy.o: test_fpspy.c
	$(CC) $(CFLAGS) -c -o $@ $^

$(BUILD)/full_nanny.o: full_nanny.c
	$(CC) $(CFLAGS) -c -o $@ $^

$(BUILD)/just_nanny.o: just_nanny.c
	$(CC) $(CFLAGS) -c -o $@ $^

$(BUILD)/no_nanny.o: no_nanny.c
	$(CC) $(CFLAGS) -c -o $@ $^

$(BUILD)/sleepy.o: sleepy.c
	$(CC) $(CFLAGS) -c -o $@ $^

$(BUILD)/dopey.o: dopey.c
	$(CC) $(CFLAGS) -c -o $@ $^

$(BIN)/$(ARCH)/fs_test_fpspy: $(BUILD)/test_fpspy.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN)/$(ARCH)/fs_sleepy: $(BUILD)/sleepy.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN)/$(ARCH)/fs_dopey: $(BUILD)/dopey.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN)/$(ARCH)/fs_full_nanny: $(BUILD)/full_nanny.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN)/$(ARCH)/fs_just_nanny: $(BUILD)/just_nanny.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN)/$(ARCH)/fs_no_nanny: $(BUILD)/no_nanny.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN)/$(ARCH)/ve_test_fpspy: $(BUILD)/syscalls.o $(BUILD)/crt.o $(BUILD)/test_fpspy.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN)/$(ARCH)/ve_sleepy: $(BUILD)/syscalls.o $(BUILD)/crt.o $(BUILD)/sleepy.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN)/$(ARCH)/ve_dopey: $(BUILD)/syscalls.o $(BUILD)/crt.o $(BUILD)/dopey.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN)/$(ARCH)/ve_full_nanny: $(BUILD)/syscalls.o $(BUILD)/crt.o $(BUILD)/full_nanny.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN)/$(ARCH)/ve_just_nanny: $(BUILD)/syscalls.o $(BUILD)/crt.o $(BUILD)/just_nanny.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN)/$(ARCH)/ve_no_nanny: $(BUILD)/syscalls.o $(BUILD)/crt.o $(BUILD)/no_nanny.o
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	rm -rf $(BUILD) && mkdir $(BUILD) 
	rm -f $(BIN)/$(ARCH)/*

