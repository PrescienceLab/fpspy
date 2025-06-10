#pragma once

/* The functions exported through this header are intended for the arch-specific
 * backends to call back into the generic FPSpy core.
 *
 * For example, the architecture-specific backends need access to the common way
 * FPSpy aborts operation when some inconsistent state is encountered.
 */

#include <signal.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>

#include "trace_record.h"

void fp_trap_handler(siginfo_t *si, ucontext_t *uc);
void brk_trap_handler(siginfo_t *si, ucontext_t *uc);
void abort_operation(char *reason);

// State of our internal random number generator
// In typical use, there will be one of these per thread
// if we are using timing (Poisson sampling)
typedef struct rand_state {
  uint64_t xi;
} rand_state_t;

// State of a Poisson sampler.  We will have one per thread
// if timing is in use.  The sampler switches between ON and OFF
// states, where the duration spent in state is drawn from an
// exponential random distribution.   PASTA!
typedef struct sampler_state {
  enum { OFF = 0, ON } state;
  int delayed_processing;
  rand_state_t rand;
  uint64_t on_mean_us;
  uint64_t off_mean_us;
  struct itimerval it;
} sampler_state_t;

// State used to monitor a thread
typedef struct monitoring_context {
  uint64_t start_time;  // cycles when context created
  enum { INIT, AWAIT_FPE, AWAIT_TRAP, ABORT } state;
  int aborting_in_trap;
  int tid;
  int fd;
  uint64_t count;
  uint64_t trap_state;      // for use by the architectural trap mechanism
  sampler_state_t sampler;  // used only when sampling is on
  // for buffering of trace records
  uint64_t trace_record_count;
  individual_trace_record_t trace_records[CONFIG_TRACE_BUFLEN];
} monitoring_context_t;

monitoring_context_t *find_monitoring_context(int tid);
