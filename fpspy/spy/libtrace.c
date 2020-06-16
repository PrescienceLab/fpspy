#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "libtrace.h"

//  Part of FPSpy
//
//  Preload library with floating point exception interception 
//  aggregation via FPE sticky behavior and trap-and-emulate
//
//  Copyright (c) 2018 Peter A. Dinda - see LICENSE

trace_t *trace_attach(char *file)
{
  struct stat s;
  uint64_t len;
  trace_t *t;
  int fd;

  fd = open(file,O_RDONLY,0666);

  if (fd<0) {
    return 0;
  }

  t = malloc(sizeof(trace_t));

  if (!t) {
    close(fd);
    return t;
  }

  memset(t,0,sizeof(*t));
  
  t->fd = fd;
  
  if (fstat(fd,&s)<0) {
    close(fd);
    free(t);
    return 0;
  }

  len = s.st_size;

  if (len % sizeof(individual_trace_record_t)) {
    close(fd);
    free(t);
    return 0;
  }
  
  t->numrecs = len / sizeof(individual_trace_record_t);

  t->rec = mmap(0,len,PROT_READ,MAP_SHARED,fd,0);

  if (t->rec == MAP_FAILED) {
    close(fd);
    free(t);
    return 0;
  }

  return t;
}
 
void trace_detach(trace_t *t)
{
  munmap(t->rec,t->numrecs*sizeof(individual_trace_record_t));
  close(t->fd);
  free(t);
}


int trace_map(char *file, void (*filter)(individual_trace_record_t *, void *), void *state)
{
   uint64_t i;
  trace_t *t = trace_attach(file);

  if (!t) {
    return -1;
  }

  for (i=0;i<t->numrecs;i++) {
    filter(&t->rec[i],state);
  }

  trace_detach(t);
  
  return 0;

}


static inline void print(individual_trace_record_t *r, FILE *out)
{
  char *op;
  int i;

  switch (r->code) {
  case 1:
    op = "FPE_INTDIV";
    break;
  case 2:
    op = "FPE_INTOVF";
    break;
  case 3:
    op = "FPE_FLTDIV";
    break;
  case 4:
    op = "FPE_FLTOVF";
    break;
  case 5:
    op = "FPE_FLTUND";
    break;
  case 6:
    op = "FPE_FLTRES";
    break;
  case 7:
    op = "FPE_FLTINV";
    break;
  case 8:
    op = "FPE_FLTSUB";
    break;
  case -1:
    op = "***ABORT!!";
    break;
  default:
    op = "***UNKNOWN";
    break;
  }
  
  fprintf(out,
	  "%-16ld\t%s\t%016lx\t%016lx\t%08x\t%08x\t",
	  r->time, op, (uint64_t)r->rip, (uint64_t)r->rsp, r->code, r->mxcsr);

  for (i=0;i<15;i++) {
    fprintf(out,"%02x", ((int) r->instruction[i]) & 0xff);
  }

  fprintf(out,"\n");
}
  

int trace_print(char *file, FILE *dest, int (*select)(individual_trace_record_t *))
{
  uint64_t i;
  trace_t *t = trace_attach(file);

  if (!t) {
    return -1;
  }

  for (i=0;i<t->numrecs;i++) {
    if (!select || select(&t->rec[i])) {
      print(&t->rec[i],dest);
    }
  }

  trace_detach(t);
  
  return 0;

}
  

