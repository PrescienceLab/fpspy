#  Preload library with floating point exception interception 
#  aggregation via FPE sticky behavior and trap-and-emulate
#

all: fpe_preload.so test_fpe_preload

fpe_preload.so: fpe_preload.c
	gcc -O2 -Wall -fno-strict-aliasing -fPIC -shared fpe_preload.c -lm -ldl  -o fpe_preload.so

test_fpe_preload: test_fpe_preload.c
	gcc -O2 -Wall -fno-strict-aliasing -pthread test_fpe_preload.c -lm -o test_fpe_preload

clean:
	-rm fpe_preload.so test_fpe_preload
	-rm __test_fpe_preload.*.fpemon

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
