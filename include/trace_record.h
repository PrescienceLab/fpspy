//  Part of FPSpy
//
//  Preload library with floating point exception interception 
//  aggregation via FPE sticky behavior and trap-and-emulate
//
//  Copyright (c) 2018 Peter A. Dinda - see LICENSE

#ifndef __TRACE_RECORD
#define __TRACE_RECORD

// all bits set indicates an abort
struct individual_trace_record {
  uint64_t time; // cycles from start of monitoring
  void    *rip;
  void    *rsp;
  int      code;  // as in siginfo_t->si_code
  int      mxcsr; 
  char     instruction[15];
  char     pad;
} __attribute__((packed));

typedef struct individual_trace_record individual_trace_record_t;

#endif
