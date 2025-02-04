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

LINUX_PREFIX=riscv64-unknown-linux-gnu-
LINUX_CFLAGS=
LINUX_LDFLAGS=
LINUX_CC=$(LINUX_PREFIX)gcc
LINUX_LD=$(LINUX_PREFIX)ld
LINUX_AR=$(LINUX_PREFIX)ar

METAL_PREFIX=riscv64-unknown-elf-
METAL_CFLAGS=-D__riscv_xlen=64 -mcmodel=medany 
METAL_LDFLAGS=-DPREALLOCATE=1 -fno-common -fno-builtin-printf \
					    -fvisibility=hidden -nostartfiles -Tutils/$(ARCH)/link.ld
METAL_CC=$(METAL_PREFIX)gcc
METAL_LD=$(METAL_PREFIX)ld
METAL_AR=$(METAL_PREFIX)ar

CFLAGS = -O0 -Wall -fno-strict-aliasing -g -D$(ARCH) -Iutils/$(ARCH)/include/
LDFLAGS = -lc -lgcc -static -Wall -fno-strict-aliasing -D$(ARCH) -std=gnu99

BIN = ../bin
BUILD = ./build

INCLUDES = utils/riscv64/include/util.h utils/riscv64/include/util2.h utils/riscv64/include/encoding.h  

TESTS = full_nanny just_nanny no_nanny

CONFIGS = firesim firesim_pipelined verilator

VERILATOR_DEPS = $(BUILD)/verilator_crt.o $(BUILD)/verilator_syscalls.o
FIRESIM_DEPS = 
FIRESIM_PIPELINED_DEPS = $(BUILD)/firesim_pipelined_trap_entry.o

all: verilator_tests firesim_tests firesim_pipelined_tests

verilator_tests: $(addprefix $(BIN)/riscv64/verilator_, $(TESTS))
firesim_tests: $(addprefix $(BIN)/riscv64/firesim_, $(TESTS))
firesim_pipelined_tests: $(addprefix $(BIN)/riscv64/firesim_pipelined_, $(TESTS))

################################################################################

$(BUILD)/verilator_%.o: utils/riscv64/src/%.S $(INCLUDES)
	$(METAL_CC) $(CFLAGS) $(METAL_CFLAGS) -c -o $@ $<

$(BUILD)/verilator_%.o: utils/riscv64/src/%.c $(INCLUDES)
	$(METAL_CC) $(CFLAGS) $(METAL_CFLAGS) -c -o $@ $<

$(BUILD)/verilator_%.o: %.c $(INCLUDES)
	$(METAL_CC) $(CFLAGS) $(METAL_CFLAGS) -c -o $@ $<

$(BUILD)/firesim_%.o: utils/riscv64/src/%.S $(INCLUDES)
	$(LINUX_CC) $(CFLAGS) $(LINUX_CFLAGS) -c -o $@ $<

$(BUILD)/firesim_%.o: utils/riscv64/src/%.c $(INCLUDES)
	$(LINUX_CC) $(CFLAGS) $(LINUX_CFLAGS) -c -o $@ $<

$(BUILD)/firesim_%.o: %.c $(INCLUDES)
	$(LINUX_CC) $(CFLAGS) $(LINUX_CFLAGS) -c -o $@ $<

$(BUILD)/firesim_pipelined_%.o: utils/riscv64/src/%.S $(INCLUDES)
	$(LINUX_CC) $(CFLAGS) $(LINUX_CFLAGS) -DDELEGATE_TRAPS -c -o $@ $<

$(BUILD)/firesim_pipelined_%.o: utils/riscv64/src/%.c $(INCLUDES)
	$(LINUX_CC) $(CFLAGS) $(LINUX_CFLAGS) -DDELEGATE_TRAPS -c -o $@ $<

$(BUILD)/firesim_pipelined_%.o: %.c $(INCLUDES)
	$(LINUX_CC) $(CFLAGS) $(LINUX_CFLAGS) -DDELEGATE_TRAPS -c -o $@ $<

################################################################################

$(BIN)/riscv64/verilator_%: $(BUILD)/verilator_%.o $(VERILATOR_DEPS)
	$(METAL_CC) $(LDFLAGS) $(METAL_LDFLAGS) -o $@ $^

$(BIN)/riscv64/firesim_pipelined_%: $(BUILD)/firesim_pipelined_%.o $(FIRESIM_PIPELINED_DEPS)
	$(LINUX_CC) $(LDFLAGS) $(LINUX_LDFLAGS) -DDELEGATE_TRAPS -o $@ $^

$(BIN)/riscv64/firesim_%: $(BUILD)/firesim_%.o $(FIRESIM_DEPS)
	$(LINUX_CC) $(LDFLAGS) $(LINUX_LDFLAGS) -o $@ $^

clean:
	rm -rf $(BUILD) && mkdir -p $(BUILD)/utils 
	rm -f $(BIN)/$(ARCH)/*

