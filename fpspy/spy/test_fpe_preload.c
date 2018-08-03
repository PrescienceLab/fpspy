/*

  Test code for floating point exception interception

  Copyright (c) 2017 Peter A. Dinda

*/

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#define __GNU_SOURCE
#include <fenv.h>

double foo(double x)
{
  return sin(x);
} 

void divzero()
{
  volatile double x,y,z;
  
  x=99.0;
  y=0.0;
  printf("Doing divide by zero\n");
  z = x/y;
  printf("%.18le / %.18le = %.18le\n", x,y,z);
}

void nanny()
{
  volatile double x,y,z;
  
  x=0.0;
  y=0.0;
  printf("Doing NAN\n");
  z = x/y;
  printf("%.18le / %.18le = %.18le\n", x,y,z);
}


void denorm()
{
  volatile double x,y,z;
  unsigned long val;
  // smallest normal number
  // 0 00000000001 00000000000....1
  // all zero except for bit 52 and bit 0 
  val =  0x0010000000000001ULL;
  x=*(double*)&val;
  y=4.0;
  printf("Doing denorm\n");
  z = x/y;
  printf("%.18le / %.18le = %.18le\n", x,y,z);
}

void underflow()
{
  volatile double x,y,z;
  unsigned long val;
  // smallest denormal number
  // 0 00000000000 00000000000....1
  // all zero except forand bit 0 
  val =  0x0000000000000001ULL;
  x=*(double*)&val;
  y=4.0;
  printf("Doing underflow\n");
  z = x/y;
  printf("%.18le / %.18le = %.18le\n", x,y,z);
}

void overflow()
{
  volatile double x,y,z;
  unsigned long val;
  // largest normal number
  // 0 1111111110 11111111...
  // all 1s except for bit 52 
  val =  0x7fefffffffffffffULL;
  x=*(double*)&val;
  y=4.0;
  printf("Doing overflow\n");
  z = x*y;
  printf("%.18le * %.18le = %.18le\n", x,y,z);
}  

void inexact()
{
  volatile double x,y,z;
  unsigned long val;
  // largest normal number
  // 0 1111111110 11111111...
  // all 1s except for bit 52 
  val =  0x7fefffffffffffffULL;
  x=*(double*)&val;
  // normal number with smallest exponent, biggest mantissa
  // 0 00000000001 11111....
  // all 0s except for bit 52..0 
  val =  0x001fffffffffffffULL;
  y=*(double*)&val;
  printf("Doing inexact\n");
  z = x-y;
  printf("%.18le - %.18le = %.18le\n", x,y,z);
}  

void handler(int sig)
{
  printf("Caught my own signal %d and am exiting\n",sig);
  exit(0);
}

int main()
{
  printf("Hello from test_fpe_preload\n");
  
  divzero();
  nanny();
  denorm();

  // if we abort here, we should have some partial output in the logs
  
  if (getenv("TEST_FPE_BREAK_GENERAL_SIGNAL")) {
    signal(SIGUSR1,handler);
  }
  if (getenv("TEST_FPE_BREAK_FPE_SIGNAL")) {
    signal(SIGFPE,handler);
  }
  if (getenv("TEST_FPE_BREAK_FE_FUNC")) {
    feclearexcept(FE_ALL_EXCEPT);
  }

  underflow();
  overflow();
  inexact();

  return 0;
}
