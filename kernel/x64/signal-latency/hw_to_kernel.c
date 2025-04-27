#define _GNU_SOURCE
#include <fenv.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include "fpvm_ioctl.h"

#define N 100000

uint64_t start;
uint64_t end;

uint64_t time[N];
uint64_t count = 0;

static inline uint64_t my_rdtsc(void) {
  uint32_t lo, hi;
  asm volatile("rdtscp" : "=a"(lo), "=d"(hi));
  return lo | ((uint64_t)(hi) << 32);
}

int main() {
  int pid;
  int file_desc;


  file_desc = open("/dev/fpvm_dev", O_RDWR);

  // Try registering handler with fpvm_dev
  if (!(file_desc < 0)) {
    ioctl(file_desc, FPVM_IOCTL_REG, NULL);
  }

  feclearexcept(FE_ALL_EXCEPT);
  feenableexcept(FE_ALL_EXCEPT);

  for (int i = 0; i < N; ++i) {
    volatile double a = 0.123;
    volatile double b = 0.456;
    volatile unsigned long start = my_rdtsc();
    volatile unsigned long end = 0;
    volatile double z = a / b;
    asm __volatile__("movq %%rax, %0" : "=r"(end) : : "rax", "rdx");
    time[i] = end - start;
  }

  for (int i = 1; i < N; i++) {
    printf("%d\t%ld\n", i, time[i]);
  }


  return 0;
}
