#include <stdio.h>
#include "libtrace.h"

/*

  Part of FPSpy

  Copyright (c) 2017 Peter A. Dinda - see LICENSE

*/


int main(int argc, char *argv[])
{
  if (argc!=2) {
    fprintf(stderr,"trace_print <individual trace file>\n");
    return -1;
  }

  if (trace_print(argv[1],stdout,0)) {
    fprintf(stderr,"Failed to print %s\n",argv[1]);
    return -1;
  }

  return 0;

}
