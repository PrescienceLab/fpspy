/*

  Part of FPSpy

  Test code for floating point exception interception

  Copyright (c) 2017 Peter A. Dinda - see LICENSE

*/

#include <util.h>
#define __GNU_SOURCE

void use(double x);

void nanny() {
  volatile double x, y, z;

  x = 0.0;
  y = 0.0;
  z = x / y;
  use(z);
}

void use(double x) {}

int main(int argc, char *argv[], char *envp[]) {
  enable_delegation();

  nanny();

  return 0;
}
