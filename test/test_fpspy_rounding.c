#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <fenv.h>
#include <float.h>
#include <stdint.h>
#ifdef x64
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif
#include <ucontext.h>
/*
typedef union  {
    uint32_t val;
    struct {
        uint8_t ie:1;  // detected nan
        uint8_t de:1;  // detected denormal
        uint8_t ze:1;  // detected divide by zero
        uint8_t oe:1;  // detected overflow (infinity)
        uint8_t ue:1;  // detected underflow (zero)
        uint8_t pe:1;  // detected precision (rounding)
        uint8_t daz:1; // denormals become zeros
        uint8_t im:1;  // mask nan exceptions
        uint8_t dm:1;  // mask denorm exceptions
        uint8_t zm:1;  // mask zero exceptions
        uint8_t om:1;  // mask overflow exceptions
        uint8_t um:1;  // mask underflow exceptions
        uint8_t pm:1;  // mask precision exceptions
        uint8_t rounding:2; // rounding (toward 00=>nearest,01=>negative,10=>positive,11=>zero)
        uint8_t fz:1;  // flush to zero (denormals are zeros)
        uint16_t rest;
    } __attribute__((packed));
} __attribute__((packed)) mxcsr_t;


void show_fe_current_rounding_method()
{
    printf("Current rounding method:  ");
    switch (fegetround()) {
           case FE_TONEAREST:  printf ("FE_TONEAREST");  break;
           case FE_DOWNWARD:   printf ("FE_DOWNWARD");   break;
           case FE_UPWARD:     printf ("FE_UPWARD");     break;
           case FE_TOWARDZERO: printf ("FE_TOWARDZERO"); break;
           default:            printf ("unknown");
    };
    printf("\n");
}





static void dump_mxcsr(ucontext_t *uc)
{
    char buf[256];

    mxcsr_t *m = (mxcsr_t *)&uc->uc_mcontext.fpregs->mxcsr;

    printf("mxcsr = %08x flags:", m->val);



}
*/

void print_bits(unsigned x)
{
  int i;
  for (i=0;i<sizeof(x)*8;i++) {
    printf("%d",(x>>(31-i))&0x1);
  }
}

void print_bits_float(float x)
{
  print_bits(*((unsigned*)&x));
}

void print_bits_double(double x)
{
  print_bits(*(((unsigned *)&x)+1));
  print_bits(*((unsigned*)&x));
}

static uint32_t get_fpsr()
{
#ifdef x64
  uint32_t val=0;
  __asm__ __volatile__ ("stmxcsr %0" : "=m"(val) : : "memory" );
  return val;
#endif

#ifdef arm64
  uint64_t v;
  __asm__ __volatile__ ("mrs %0, fpsr" : "=r"(v) : : "memory");
  return v;
#endif

#ifdef riscv64
  uint64_t v;
  __asm__ __volatile__ ("frcsr %0" : "=r"(v) : : "memory");
  return v;
#endif
}


const char *show_classification(double x) {
  switch(fpclassify(x)) {
  case FP_INFINITE:  return "Inf";
  case FP_NAN:       return "NaN";
  case FP_NORMAL:    return "normal";
  case FP_SUBNORMAL: return "subnormal";
  case FP_ZERO:      return "zero";
  default:           return "unknown";
  }
}

float divide(float a, float b){
  if(b == 0){
    printf("ERROR: DIVZERO in divide()\n");
  }
  return a/b;
}

void rounding_test(){
  volatile float a = 1.5;
  volatile int result = rintf(a);
  printf("Rounding 1.5 to: %d\n",result); 
  printf("FP Hex: %08x\n",*(int*)&a);
  print_bits_float(a);
  printf("\n");
  printf("FPSR: %08x\n",get_fpsr());
  print_bits(get_fpsr());
  printf("\n");
}

void daz_test(){
#ifdef x64
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_OFF);
  volatile float a = 10;
  volatile float b = 0.0000000000000000000000000000000000000000000001;
  printf("DAZ divisor Classification: %s\n",show_classification(b));
  printf("DAZ result: %.50f\n",divide(a,b));
#else
  printf("Can't do DAZ test on this architecture\n");
#endif

}


void ftz_test(){
#ifdef x64
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_OFF);
  volatile float a = 1.000000000000000000000000000001;
  volatile float b = 1;
  volatile float result = a-b;
  printf("FTZ result classification: %s\n",show_classification(result));
  printf("FTZ Result: %.50f\n",result);
#else
  printf("Can't do FTZ test on this architecture\n");
#endif

}

int main(){
  printf("STARTING ROUNDING TESTS\n");
  //show_fe_current_rounding_method();
  rounding_test();
  ftz_test();
  daz_test();
  printf("ROUNDING TESTS CONCLUDED\n");
  return 0;
}
