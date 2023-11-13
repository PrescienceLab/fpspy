#include <stdio.h>
#include <stdint.h>

/*
 * ARM64 makes FP traps architectural traps optional. 
 * This code checks to see if your machine supports them.
 */


static uint64_t get_fpcr_machine(void)
{
  uint64_t fpcr;
  __asm__ __volatile__ ("mrs %0, fpcr" : "=r"(fpcr) : :);
  return fpcr;
}

static void set_fpcr_machine(uint64_t fpcr)
{
  __asm__ __volatile__ ("msr fpcr, %0" : : "r"(fpcr));
}

static uint64_t get_fpsr_machine(void)
{
  uint64_t fpsr;
  __asm__ __volatile__ ("mrs %0, fpsr" : "=r"(fpsr) : :);
  return fpsr;
}

static void set_fpsr_machine(uint64_t fpsr)
{
  __asm__ __volatile__ ("msr fpsr, %0" : : "r"(fpsr));
}

static void sync_fp(void)
{
  __asm__ __volatile__ ("dsb ish" : : : "memory");
}

int main()
{
  printf("before fpsr=%016lx fpcr=%016lx\n", get_fpsr_machine(),get_fpcr_machine());
  printf("now writing all bits high on both registers\n");
  set_fpsr_machine(-1UL);
  set_fpcr_machine(-1UL);
  sync_fp();

  uint64_t fpsr=get_fpsr_machine();
  uint64_t fpcr=get_fpcr_machine();
  
  printf("after fpsr=%016lx fpcr=%016lx\n", fpsr, fpcr);

  if ((fpcr & 0x9f00)!=0x9f00) {
    printf("This machine does not support FP traps - expect fpcr bits 15, 12:8 high\n");
  } else {
    printf("This machine does support FP traps\n");
  }

}
