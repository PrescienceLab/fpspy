//  Preload library with floating point exception interception 
//  aggregation via FPE sticky behavior and trap-and-emulate
//
//  Copyright (c) 2018 Peter A. Dinda

#ifndef __LIB_TRACE
#define __LIB_TRACE

#include <stdint.h>
#include <stdio.h>
#include "trace_record.h"

typedef struct trace {
  uint64_t                   numrecs;
  individual_trace_record_t *rec;
  int                        fd;
} trace_t;

trace_t *trace_attach(char *file);
void     trace_detach(trace_t *trace);

int      trace_map(char *file, void (*filter)(individual_trace_record_t *, void *), void *);

// select = 0 => all
int      trace_print(char *file, FILE *dest, int (*select)(individual_trace_record_t *));

#endif

