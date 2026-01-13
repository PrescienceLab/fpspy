/*

  Part of FPSpy

  Preload library with floating point exception interception
  aggregation via FPE sticky behavior and trap-and-emulate

  Copyright (c) 2017 Peter A. Dinda - see LICENSE


  This code does the following:

  - installs itself at load time of the target program
  - adds hooks for fpe* functions - if any of these are used, the library
    deactivates itself
  - adds hook for signal installation (individual mode only)
    so that it can get out of the way if the target program
    establishes its own floating point exception handler
  - removes itself at unload time of the target program, and records its observations

  There are two modes of operation:

  - Aggregate.  Here all that is done is to capture fpe sticky exception state
    at program start, and then again at program end.   This lets us determine,
    for a program that is not using fpe* / fpe signal itself, whether any of the
    fpe exceptions have occurred during its execution.

  - Individual.  Here we intercept each exception that occurs, using a debugger-style
    approach of, on exception, disabling exceptions, switching to trap mode, then
    restarting the instruction, then fauling on trap at the next instruction, then
    switching exceptions back on and switching traps off

  Additionally, you can operate in "aggressive" mode (for individual mode), which
  means that it will not get out of the way if the target program sets a SIGFPE signal;
  instead, the target program will just never see any of its own SIGFPEs.

  In individual mode, you can also employ Poisson sampling, where the on and off
  periods are chosen from an exponential random distibution whose parameters are
  given via an evironmental variable.

  Concurrency:
      - fork() - both parent and child are tracked.  Child's FPE state is cleared
                 any previous abort in parent is inherited
      - exec() - Tracking restarts (assuming the environment variables are inherited)
                 any previous abort is discarded
      - pthread_create() - both parent and child are tracked.  Child's FPE state is cleared
                           both have a log file.  May not work on a pthread_cancel
                           An abort in any thread is shared by all the threads


*/

#define _GNU_SOURCE
#include <sys/ptrace.h>
#ifdef x64
#include <sys/reg.h>
#endif
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>
#include <fenv.h>
#include <errno.h>
#include <string.h>
#include <sched.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <fcntl.h>
#include <ucontext.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <sys/time.h>

#include <math.h>

#include <execinfo.h>

#include "config.h"
#include "fpspy.h"
#include "debug.h"
#include "arch.h"
#include "trace_record.h"

// trap short-circuiting support from FPVM
// this allows much faster response to FP traps
// when the kernel module is available
#if CONFIG_TRAP_SHORT_CIRCUITING
#include <sys/ioctl.h>
#include "fpvm_ioctl.h"
#endif

//
// per-process state
//
volatile static int inited = 0;   // have we brought up at least one thread?
volatile static int aborted = 0;  // are we getting out of the target's way?
// PAD: might make sense to make this per monitoring context
static uint32_t orig_round_config;  // the FP rounding setup encountered at startup

//
// configuration info that can be overridden at runtime
//
volatile static int maxcount =
    -1;  // maximum number of events to record, per thread (-1=> no limit)
volatile static int sample_period = 1;  // sample period 1 => record every event

volatile static int kernel = 0;  // are we using kernel support?

volatile static int kernel_fd = -1;


volatile static int timers = 0;                    // are we using timing-based sampling?
volatile static uint64_t on_mean_us, off_mean_us;  // parameters for poisson sampling
volatile static int timer_type = ITIMER_REAL;      // which time base we are using

volatile static uint64_t random_seed =
    -1;  // random number seed for the internal rng. -1 => pick at start

volatile static int enabled_fp_traps = FE_ALL_EXCEPT;  // which FP exceptions to handle, default all

static int control_round_config = 0;   // will we control rounding+related (daz/ftz) or not
static uint32_t our_round_config = 0;  // if we control, what is the config we will force

volatile static enum { AGGREGATE, INDIVIDUAL } mode = AGGREGATE;  // our mode of operation
volatile static int aggressive =
    0;  // whether we will ignore some target operations that would normally cause us to abort
volatile static int disable_pthreads = 0;  // whether to avoid pthread override
volatile static int kickstart =
    0;  // whether we start with external SIGTRAP or internal one (first process only)
volatile static int abort_on_fpe =
    0;  // whether we abort (ie. crash with SIGARBT) the program on the first FPE
volatile static int create_monitor_file = 1;  // whether we write a monitor output file (*.fpemon)

unsigned char log_level = 2;  // how much log info

//
// pointers to the functions we override to control the target
// and to detect when we must move out of the way
//
static int (*orig_fork)() = 0;
static int (*orig_pthread_create)(
    pthread_t *tid, const pthread_attr_t *attr, void *(*start)(void *), void *arg) = 0;
static int (*orig_pthread_exit)(void *ret) __attribute__((noreturn)) = 0;
static sighandler_t (*orig_signal)(int sig, sighandler_t func) = 0;
static int (*orig_sigaction)(int sig, const struct sigaction *act, struct sigaction *oldact) = 0;
static int (*orig_feenableexcept)(int) = 0;
static int (*orig_fedisableexcept)(int) = 0;
static int (*orig_fegetexcept)() = 0;
static int (*orig_feclearexcept)(int) = 0;
static int (*orig_fegetexceptflag)(fexcept_t *flagp, int excepts) = 0;
static int (*orig_feraiseexcept)(int excepts) = 0;
static int (*orig_fesetexceptflag)(const fexcept_t *flagp, int excepts) = 0;
static int (*orig_fetestexcept)(int excepts) = 0;
static int (*orig_fegetround)(void) = 0;
static int (*orig_fesetround)(int rounding_mode) = 0;
static int (*orig_fegetenv)(fenv_t *envp) = 0;
static int (*orig_feholdexcept)(fenv_t *envp) = 0;
static int (*orig_fesetenv)(const fenv_t *envp) = 0;
static int (*orig_feupdateenv)(const fenv_t *envp) = 0;

//
// stashes of sigactions we override, available so that we can
// restore them on an abort
//
static struct sigaction oldsa_fpe, oldsa_trap, oldsa_int, oldsa_alrm;

//
// Wrappers for calling functions we have overriden
//
#define ORIG_RETURN(func, ...)                            \
  if (orig_##func) {                                      \
    return orig_##func(__VA_ARGS__);                      \
  } else {                                                \
    ERROR("cannot call orig_" #func " returning zero\n"); \
    return 0;                                             \
  }
#define ORIG_IF_CAN(func, ...)                        \
  if (orig_##func) {                                  \
    __auto_type __attribute__((unused)) rc  = orig_##func(__VA_ARGS__);	\
    DEBUG("orig_" #func " returns 0x%x\n", rc);       \
  } else {                                            \
    DEBUG("cannot call orig_" #func " - skipping\n"); \
  }

// For use in debugging
//
// #define SHOW_CALL_STACK() DEBUG("callstack (3 deep) : %p -> %p -> %p\n",
// __builtin_return_address(3), __builtin_return_address(2), __builtin_return_address(1)) #define
// SHOW_CALL_STACK() DEBUG("callstack (2 deep) : %p -> %p\n", __builtin_return_address(2),
// __builtin_return_address(1)) #define SHOW_CALL_STACK() DEBUG("callstack (1 deep) : %p\n",
// __builtin_return_address(1)) #define SHOW_CALL_STACK() DEBUG("callstack (0 deep) : %p\n",
// __builtin_return_address(0))
#define SHOW_CALL_STACK()



//
// Used for glibcs that do not provide a wrapper for this system call
//
// static inline int gettid()
//{
//  return syscall(SYS_gettid);
//}


//
// Allocator for monitoring contexts
//

static int context_lock;
static monitoring_context_t context[CONFIG_MAX_CONTEXTS];



static void init_monitoring_contexts() {
  memset(context, 0, sizeof(context));
  context_lock = 0;
}

static void lock_contexts() {
  while (!__sync_bool_compare_and_swap(&context_lock, 0, 1)) {
  }
}

static void unlock_contexts() { __sync_and_and_fetch(&context_lock, 0); }


monitoring_context_t *find_monitoring_context(int tid) {
  int i;
  lock_contexts();
  for (i = 0; i < CONFIG_MAX_CONTEXTS; i++) {
    if (context[i].tid == tid) {
      unlock_contexts();
      return &context[i];
    }
  }
  unlock_contexts();
  return 0;
}

static monitoring_context_t *alloc_monitoring_context(int tid) {
  int i;
  lock_contexts();
  for (i = 0; i < CONFIG_MAX_CONTEXTS; i++) {
    if (!context[i].tid) {
      context[i].tid = tid;
      unlock_contexts();
      return &context[i];
    }
  }
  unlock_contexts();
  return 0;
}

static void free_monitoring_context(int tid) {
  int i;
  lock_contexts();
  for (i = 0; i < CONFIG_MAX_CONTEXTS; i++) {
    if (context[i].tid == tid) {
      context[i].tid = 0;
      unlock_contexts();
    }
  }
  unlock_contexts();
}

//
// Built-in random number generator to avoid changing the state
// of the application's random number generator
//
// This is borrowed from NK and should probably be replaced
//
static void seed_rand(sampler_state_t *s, uint64_t seed) { s->rand.xi = seed; }

// linear congruent, full 64 bit space
static inline uint64_t _pump_rand(uint64_t xi, uint64_t a, uint64_t c) {
  uint64_t xi_new = (a * xi + c);

  return xi_new;
}

static inline uint64_t pump_rand(sampler_state_t *s) {
  s->rand.xi = _pump_rand(s->rand.xi, 0x5deece66dULL, 0xbULL);

  return s->rand.xi;
}

static inline uint64_t get_rand(sampler_state_t *s) { return pump_rand(s); }

void init_random(sampler_state_t *s) {
  // randomization
  if (random_seed != -1) {
    seed_rand(s, random_seed);
  } else {
    seed_rand(s, arch_cycle_count());
  }
}


//
// Poisson sampler logic
//


// we assume here that the FP state is saved and restored
// by the handler wrapper code, otherwise this will damage things badly
// this is of course true for Linux user, but not necessarily NK kernel
// period in us, return in us
// we also need to be sure that we don't cause an exception ourselves

// Draw from an expoential random distribution
static uint64_t next_exp(sampler_state_t *s, uint64_t mean_us) {
  arch_fp_csr_t oldfpcsr;
  uint64_t ret = 0;

  // snapshot machine FP state, and disable all intercepts
  // because we are about to do some FP ourselves
  arch_config_machine_fp_csr_for_local(&oldfpcsr);

  // now we are safe to do FP that might itself change flags

  uint64_t r = get_rand(s);
  double u;
  r = r & -2ULL;  // make sure that we are not at max


  u = ((double)r) / ((double)(-1ULL));

  // u = [0,1)

  u = -log(1.0 - u) * ((double)mean_us);

  // now shape u back into a uint64_t

  if (u > ((double)(-1ULL))) {
    ret = -1ULL;
  } else {
    ret = (uint64_t)u;
  }

  // restore FP hardware state
  arch_set_machine_fp_csr(&oldfpcsr);

  // no more FP after this!

  return ret;
}


//
//
// Output helpers
//

static void stringify_current_fe_exceptions(char *buf) {
  int have = 0;
  buf[0] = 0;

#define FE_HANDLE(x)          \
  if (orig_fetestexcept(x)) { \
    if (!have) {              \
      strcat(buf, #x);        \
      have = 1;               \
    } else {                  \
      strcat(buf, " " #x);    \
    }                         \
  }
  FE_HANDLE(FE_DIVBYZERO);
  FE_HANDLE(FE_INEXACT);
  FE_HANDLE(FE_INVALID);
  FE_HANDLE(FE_OVERFLOW);
  FE_HANDLE(FE_UNDERFLOW);
  if (arch_have_special_fp_csr_exception(FE_DENORM)) {
    if (have) {
      strcat(buf, " ");
    }
    strcat(buf, "FE_DENORM");
    have = 1;
  }

  if (!have) {
    strcpy(buf, "NO_EXCEPTIONS_RECORDED");
  }
}

static __attribute__((unused)) void show_current_fe_exceptions() {
  char buf[80];
  stringify_current_fe_exceptions(buf);
  INFO("%s\n", buf);
}

static int writeall(int fd, void *buf, int len) {
  int n;
  int left = len;

  do {
    n = write(fd, buf, left);
    if (n < 0) {
      return -1;
    }
    left -= n;
    buf += n;
  } while (left > 0);

  return 0;
}


static int flush_trace_records(monitoring_context_t *mc) {
  if (CONFIG_TRACE_BUFLEN == 0) {
    return 0;
  } else {
    if (mc->trace_record_count > 0) {
      int rc = writeall(
          mc->fd, mc->trace_records, mc->trace_record_count * sizeof(individual_trace_record_t));
      mc->trace_record_count = 0;
      return rc;
    } else {
      return 0;
    }
  }
}

static inline int push_trace_record(monitoring_context_t *mc, individual_trace_record_t *tr) {
  if (CONFIG_TRACE_BUFLEN == 0) {
    return writeall(mc->fd, tr, sizeof(individual_trace_record_t));
  } else {
    mc->trace_records[mc->trace_record_count] = *tr;
    mc->trace_record_count++;
    if (mc->trace_record_count >= CONFIG_TRACE_BUFLEN) {  // should never be > ...
      return flush_trace_records(mc);
    } else {
      return 0;
    }
  }
}


static void kick_self(void) {
#if CONFIG_RISCV_USE_ESTEP
  __asm__ __volatile__(".insn 0x00300073\n\t");
#else
  kill(gettid(), SIGTRAP);
#endif
}

static __attribute__((constructor)) void fpspy_init(void);


//
// Abort operation is invoked whenever FPSpy needs to "get out of the way"
//
void abort_operation(char *reason) {
  if (!inited) {
    ERROR("Initializing before aborting\n");
    fpspy_init();
    ERROR("Done with fpspy_init()\n");
  }

  if (!aborted) {
    ORIG_IF_CAN(fedisableexcept, FE_ALL_EXCEPT);
    ORIG_IF_CAN(feclearexcept, FE_ALL_EXCEPT);
    ORIG_IF_CAN(sigaction, SIGFPE, &oldsa_fpe, 0);

    if (mode == INDIVIDUAL) {
      monitoring_context_t *mc = find_monitoring_context(gettid());

      if (!mc) {
        ERROR("Cannot find monitoring context to write abort record\n");
      } else {
        mc->state = ABORT;

        // write an abort record
        struct individual_trace_record r;
        memset(&r, 0xff, sizeof(r));

        r.time = arch_cycle_count() - mc->start_time;

        if (push_trace_record(mc, &r)) {
          ERROR("Failed to push abort record\n");
        }
      }

      // even if we have no monitoring context we need to restore
      // the mcontext.  If we do have a monitoring context,
      // and we are a trap, the mcontext has already been restored
      if (!mc || !mc->aborting_in_trap) {
        // signal ourselves to restore the FP and TRAP state in the context
        kick_self();
      }
    }

    // finally remove our trap handler
    ORIG_IF_CAN(sigaction, SIGTRAP, &oldsa_trap, 0);

    aborted = 1;
    ERROR("Aborted operation because %s\n", reason);
  }
}

//
// function intercepts to manage FPSpy functionality
// (handling processes/threads, in particular), and to
// detect when the target is doing something that requires
// us to get out of the way
//


static int bringup_monitoring_context(int tid);

//
// fork() is wrapped so that we can bring up FPSpy on the child process
//

int fork() {
  int rc;

  DEBUG("fork\n");

  rc = orig_fork();

  if (aborted) {
    return rc;
  }

  if (rc < 0) {
    DEBUG("fork failed\n");
    return rc;
  }

  if (rc == 0) {
    // child process - we need to bring up FPSpy on it

    // we inherit process state from parent, so what this looks like
    // now is like a new thread
    DEBUG("skipping architecture process init on fork\n");

    // clear exceptions - we will not inherit the current ones from the parent
    ORIG_IF_CAN(feclearexcept, enabled_fp_traps);

    // in aggregate mode, a distinct log file will be generated by the destructor

    // make new context for individual mode
    if (mode == INDIVIDUAL) {
      if (bringup_monitoring_context(gettid())) {
        ERROR("Failed to start up monitoring context at fork\n");
        // we won't break, however..
      } else {
        // we should have inherited all the sighandlers, etc, from our parent

        // now kick ourselves to set the sse bits; we are currently in state INIT
        // this will also do the architectural init

        // note that kickstart only applies to "top-level" process
        // not this child
        kick_self();
        // we should now be in the right state
      }

    } else {
      // we need to bring up the architecture for this thread
      if (arch_thread_init(0)) {
        ERROR("Failed to bring up architectural state for thread\n");
        // we are doomed from this point
      }
    }

    DEBUG("Done with setup on fork\n");
    return rc;

  } else {
    // parent - nothing to do
    return rc;
  }
}


//
// pthread_create is wrapped so that we can trampoline thread
// creation through our own code, which will create a monitoring
// context for the new thread, and tear it down on exit
//

struct tramp_context {
  void *(*start)(void *);
  void *arg;
  int done;
};

static void handle_aggregate_thread_exit();

// This is where a new thread stars now
static void *trampoline(void *p) {
  struct tramp_context *c = (struct tramp_context *)p;
  void *(*start)(void *) = c->start;
  void *arg = c->arg;
  void *ret;

  // let our wrapper go - this must also be a software barrier
  __sync_fetch_and_or(&c->done, 1);

  DEBUG("Setting up thread %d\n", gettid());

  // clear exceptions just in case
  ORIG_IF_CAN(feclearexcept, enabled_fp_traps);

  if (mode == INDIVIDUAL) {
    // make new context for individual mode
    if (bringup_monitoring_context(gettid())) {
      ERROR("Failed to start up monitoring context on thread creation\n");
      // we won't break, however..
    } else {
      // we should have inherited all the sighandlers, etc, from the spawning thread

      // now kick ourselves to set the sse bits; we are currently in state INIT
      kick_self();
      // we should now be in the right state
      // the architecure init is done in the trap handler
    }
    DEBUG("Done with setup on thread creation\n");
  } else {
    // we need to do the architecture init here
    arch_thread_init(0);
  }

  DEBUG("leaving trampoline\n");

  ret = start(arg);

  // if it's returning normally instead of via pthread_exit(), we'll do the cleanup here
  pthread_exit(ret);
}

// pthread_create is wrapped so that it can trampoline through our bringup
// code, plus do other setup as needed
int pthread_create(pthread_t *tid, const pthread_attr_t *attr, void *(*start)(void *), void *arg) {
  struct tramp_context c;

  DEBUG("pthread_create\n");

  if (aborted) {
    return orig_pthread_create(tid, attr, start, arg);
  }

  c.start = start;
  c.arg = arg;
  c.done = 0;

  int rc = orig_pthread_create(tid, attr, trampoline, &c);

  if (!rc) {
    // don't race on the tramp context - wait for thread to copy out
    while (!__sync_fetch_and_and(&c.done, 1)) {
    }
  }

  DEBUG("pthread_create done\n");

  return rc;
}

static int teardown_monitoring_context(int tid);


// a pthread can stop via an explicit pthread_exit call, so we must
// intercept that and do a graceful teardown
__attribute__((noreturn)) void pthread_exit(void *ret) {
  DEBUG("pthread_exit(%p)\n", ret);

  // we will process this even if we have aborted, since
  // we want to flush aggregate info even if it's just an abort record
  if (mode == INDIVIDUAL) {
    teardown_monitoring_context(gettid());
  } else {
    handle_aggregate_thread_exit();
  }

  orig_pthread_exit(ret);
}


// If the target installs a signal handler over one that we need, we
// must get out of the way, unless we are in aggressive mode
sighandler_t signal(int sig, sighandler_t func) {
  DEBUG("signal(%d,%p)\n", sig, func);
  SHOW_CALL_STACK();
  if ((sig == SIGFPE || sig == SIGTRAP) && mode == INDIVIDUAL && !aborted) {
    if (!aggressive) {
      abort_operation("target is using sigaction with SIGFPE or SIGTRAP (nonaggressive)");
    } else {
      // do not override our signal handlers - we are not aborting
      DEBUG("not overriding SIGFPE or SIGTRAP because we are in aggressive mode\n");
      return 0;
    }
  }
  ORIG_RETURN(signal, sig, func);
}



// If the target installs a signal handler over one that we need, we
// must get out of the way, unless we are in aggressive mode
int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact) {
  DEBUG("sigaction(%d,%p,%p)\n", sig, act, oldact);
  SHOW_CALL_STACK();
  if ((sig == SIGVTALRM || sig == SIGFPE || sig == SIGTRAP) && mode == INDIVIDUAL && !aborted) {
    if (!aggressive) {
      abort_operation("target is using sigaction with SIGFPE, SIGTRAP, or SIGVTALRM");
    } else {
      // do not override our signal handlers - we are not aborting
      DEBUG("not overriding SIGFPE or SIGTRAP because we are in aggressive mode\n");
      return 0;
    }
  }
  ORIG_RETURN(sigaction, sig, act, oldact);
}


// if the target manipulates FP state, we will always get out of the way
int feclearexcept(int excepts) {
  DEBUG("feclearexcept(0x%x)\n", excepts);
  SHOW_CALL_STACK();
  abort_operation("target is using feclearexcept");
  ORIG_RETURN(feclearexcept, excepts);
}

// if the target manipulates FP state, we will always get out of the way
int feenableexcept(int excepts) {
  DEBUG("feenableexcept(0x%x)\n", excepts);
  SHOW_CALL_STACK();
  abort_operation("target is using feenableexcept");
  ORIG_RETURN(feenableexcept, excepts);
}

// if the target manipulates FP state, we will always get out of the way
int fedisableexcept(int excepts) {
  DEBUG("fedisableexcept(0x%x)\n", excepts);
  SHOW_CALL_STACK();
  abort_operation("target is using fedisableexcept");
  ORIG_RETURN(fedisableexcept, excepts);
}

// if the target manipulates FP state, we will always get out of the way
int fegetexcept(void) {
  DEBUG("fegetexcept()\n");
  SHOW_CALL_STACK();
  abort_operation("target is using fegetexcept");
  ORIG_RETURN(fegetexcept);
}

// if the target manipulates FP state, we will always get out of the way
int fegetexceptflag(fexcept_t *flagp, int excepts) {
  DEBUG("fegetexceptflag(%p,0x%x)\n", flagp, excepts);
  SHOW_CALL_STACK();
  abort_operation("target is using fegetexceptflag");
  ORIG_RETURN(fegetexceptflag, flagp, excepts);
}

// if the target manipulates FP state, we will always get out of the way
int feraiseexcept(int excepts) {
  DEBUG("feraiseexcept(0x%x)\n", excepts);
  SHOW_CALL_STACK();
  abort_operation("target is using feraiseexcept");
  ORIG_RETURN(feraiseexcept, excepts);
}

// if the target manipulates FP state, we will always get out of the way
int fesetexceptflag(const fexcept_t *flagp, int excepts) {
  DEBUG("fesetexceptflag(%p,0x%x\n", flagp, excepts);
  SHOW_CALL_STACK();
  abort_operation("target is using fesetexceptflag");
  ORIG_RETURN(fesetexceptflag, flagp, excepts);
}

// if the target manipulates FP state, we will always get out of the way
int fetestexcept(int excepts) {
  DEBUG("fesetexcept(0x%x)\n", excepts);
  SHOW_CALL_STACK();
  abort_operation("target is using fetestexcept");
  ORIG_RETURN(fetestexcept, excepts);
}

// if the target manipulates FP state, we will always get out of the way
int fegetround(void) {
  DEBUG("fegetround()\n");
  SHOW_CALL_STACK();
  abort_operation("target is using fegetround");
  ORIG_RETURN(fegetround);
}

// if the target manipulates FP state, we will always get out of the way
int fesetround(int rounding_mode) {
  DEBUG("fesetround(0x%x)\n", mode);
  SHOW_CALL_STACK();
  abort_operation("target is using fesetround");
  ORIG_RETURN(fesetround, rounding_mode);
}

// if the target manipulates FP state, we will always get out of the way
int fegetenv(fenv_t *envp) {
  DEBUG("fegetenv(%p)\n", envp);
  SHOW_CALL_STACK();
  abort_operation("target is using fegetenv");
  ORIG_RETURN(fegetenv, envp);
}

// if the target manipulates FP state, we will always get out of the way
int feholdexcept(fenv_t *envp) {
  DEBUG("feholdexcept(%p)\n", envp);
  SHOW_CALL_STACK();
  abort_operation("target is using feholdexcept");
  ORIG_RETURN(feholdexcept, envp);
}


// if the target manipulates FP state, we will always get out of the way
int fesetenv(const fenv_t *envp) {
  DEBUG("fesetenv(%p)\n", envp);
  SHOW_CALL_STACK();
  abort_operation("target is using fesetenv");
  ORIG_RETURN(fesetenv, envp);
}

// if the target manipulates FP state, we will always get out of the way
int feupdateenv(const fenv_t *envp) {
  DEBUG("feupdateenv(%p)\n", envp);
  SHOW_CALL_STACK();
  abort_operation("target is using feupdateenv");
  ORIG_RETURN(feupdateenv, envp);
}

//
// "shims" are the installation of our overrides of target functions that we need to see
// We need to capture pointers to the original target functions
//

static int setup_shims() {
#define SHIMIFY(x)                              \
  if (!(orig_##x = dlsym(RTLD_NEXT, #x))) {     \
    DEBUG("Failed to setup SHIM for " #x "\n"); \
    return -1;                                  \
  }

  if (disable_pthreads == 0) {
    SHIMIFY(pthread_create);
    SHIMIFY(pthread_exit);
  }
  SHIMIFY(fork);
  SHIMIFY(signal);
  SHIMIFY(sigaction);
  SHIMIFY(feclearexcept);
  SHIMIFY(feenableexcept);
  SHIMIFY(fedisableexcept);
  SHIMIFY(fegetexcept);
  SHIMIFY(fegetexceptflag);
  SHIMIFY(feraiseexcept);
  SHIMIFY(fesetexceptflag);
  SHIMIFY(fetestexcept);
  SHIMIFY(fegetround);
  SHIMIFY(fesetround);
  SHIMIFY(fegetenv);
  SHIMIFY(feholdexcept);
  SHIMIFY(fesetenv);
  SHIMIFY(feupdateenv);

  return 0;
}


//
// Poisson Sampler
//

void init_sampler(sampler_state_t *s) {
  DEBUG("Init sampler (%p)\n", s);

  init_random(s);

  s->on_mean_us = on_mean_us;
  s->off_mean_us = off_mean_us;

  s->state = ON;

  if (!timers) {
    DEBUG("Sampler without timing\n");
    return;
  }

  uint64_t n = next_exp(s, s->on_mean_us);

  s->it.it_interval.tv_sec = 0;
  s->it.it_interval.tv_usec = 0;
  s->it.it_value.tv_sec = n / 1000000;
  s->it.it_value.tv_usec = n % 1000000;

  if (setitimer(timer_type, &(s->it), NULL)) {
    ERROR("Failed to set timer?!\n");
  }

  DEBUG("Timer initialized for %lu us\n", n);
}

// n.b: is it really the case we cannot meaningfully manipulate ucontext
// here to change the FP engine?  Really?   Why would this work in
// both SIGFPE and SIGTRAP but not here?
static void update_sampler(monitoring_context_t *mc, ucontext_t *uc) {
  sampler_state_t *s = &mc->sampler;

  // arch_dump_gp_csr("update before",uc);
  // arch_dump_fp_csr("update before",uc);

  // we are guaranteed to be in AWAIT_FPE state at this
  // point.
  //
  // ON->OFF : clear fpe, mask fpe, turn off traps
  // OFF->ON : clear fpe, unmask fpe, turn off traps
  //
  // traps should already be off, but why not be sure

  if (s->state == ON) {
    DEBUG("Switching from on to off\n");
    arch_clear_fp_exceptions(uc);               // Clear fpe
    arch_mask_fp_traps(uc);                     // Mask fpe
    arch_reset_trap_mode(uc, &mc->trap_mode_state);  // disable trap mode
  } else {
    DEBUG("Switching from off to on\n");
    arch_clear_fp_exceptions(uc);               // Clear fpe
    arch_unmask_fp_traps(uc);                   // Unmask fpe
    arch_reset_trap_mode(uc, &mc->trap_mode_state);  // disable trap mode */
  }

  // schedule next wakeup

  uint64_t n = next_exp(s, s->state == ON ? s->off_mean_us : s->on_mean_us);

  if (!n) {
    // make sure we do actually wake up again
    // n = 0 would disable timer...
    n = 1;
  }

  if (s->state == OFF && n > CONFIG_MAX_US_ON) {
    // about to turn on for too long, limit:
    n = CONFIG_MAX_US_ON;
  }

  if (s->state == ON && n > CONFIG_MAX_US_OFF) {
    // about to turn off for too long, limit:
    n = CONFIG_MAX_US_OFF;
  }

  s->it.it_interval.tv_sec = 0;
  s->it.it_interval.tv_usec = 0;
  s->it.it_value.tv_sec = n / 1000000;
  s->it.it_value.tv_usec = n % 1000000;

  // flip state
  s->state = s->state == ON ? OFF : ON;

  // don't reprocess again in case we are running delayed because
  // we were not intially in an AWAIT_FPE
  if (s->delayed_processing) {
    DEBUG("Completed delayed processing\n");
    s->delayed_processing = 0;
  }

  if (setitimer(timer_type, &s->it, NULL)) {
    ERROR("Failed to set timer?!\n");
  }

  // arch_dump_gp_csr("update after",uc);
  // arch_dump_fp_csr("update after",uc);

  DEBUG("Timer reinitialized for %lu us state %s\n", n, s->state == ON ? "ON" : "off");
}

// Shared handling of a breakpoint trap, which occurs on the
// instruction immediately after one that had a floating point trap
// A breakpoint trap might be intiated by a SIGTRAP or other mechanisms,
// see below.
// The default use of a breakpoint trap is to transition to AWAIT_FPE state.
// Other circumstances require an abort or are part of an abort,
// except for when we catch a breakpoint trap  in INIT state, in which case,
// this is deferred startup for the thread
void brk_trap_handler(siginfo_t *si, ucontext_t *uc) {
  monitoring_context_t *mc = find_monitoring_context(gettid());

  if (!mc || mc->state == ABORT) {
    arch_clear_fp_exceptions(uc);
    arch_mask_fp_traps(uc);
    if (control_round_config) {
      arch_set_round_config(uc, orig_round_config);
    }
    arch_reset_trap_mode(uc, mc ? &mc->trap_mode_state : 0);  // best effort disable of trap
    if (!mc) {
      // this may end badly
      abort_operation("Cannot find monitoring context during brk_trap_handler exec");
    } else {
      DEBUG("FP and TRAP mcontext restored on abort\n");
    }
    return;
  }

  if (mc && mc->state == INIT) {
    if (arch_thread_init(uc)) {
      // bad news, probably...
      abort_operation("failed to setup thread for architecture\n");
    }
    orig_round_config = arch_get_round_config(uc);
    arch_clear_fp_exceptions(uc);
    arch_unmask_fp_traps(uc);
    if (control_round_config) {
      arch_set_round_config(uc, our_round_config);
    }
    arch_reset_trap_mode(uc, &mc->trap_mode_state);
    mc->state = AWAIT_FPE;
    DEBUG("state initialized - waiting for first SIGFPE\n");
    return;
  }

  if (mc->state == AWAIT_TRAP) {
    mc->count++;
    arch_clear_fp_exceptions(uc);
    if (maxcount != -1 && mc->count >= maxcount) {
      // disable further operation since we've recorded enough
      arch_mask_fp_traps(uc);
      if (control_round_config) {
        arch_set_round_config(uc, orig_round_config);
      }
    } else {
      arch_unmask_fp_traps(uc);
      if (control_round_config) {
        arch_set_round_config(uc, our_round_config);
      }
    }
    arch_reset_trap_mode(uc, &mc->trap_mode_state);
    mc->state = AWAIT_FPE;
    if (mc->sampler.delayed_processing) {
      DEBUG("Delayed sampler handling\n");
      update_sampler(mc, uc);
    }
  } else {
    arch_clear_fp_exceptions(uc);
    arch_mask_fp_traps(uc);
    if (control_round_config) {
      arch_set_round_config(uc, orig_round_config);
    }
    arch_reset_trap_mode(uc, &mc->trap_mode_state);
    mc->aborting_in_trap = 1;
    abort_operation("Surprise state during sigtrap_handler exec");
  }
}

//
// FPSpy gets a SIGTRAP when the current instruction follows a FP
// instruction for which we took a SIGFPE.
//
static void sigtrap_handler(int sig, siginfo_t *si, void *priv) {
  ucontext_t *uc = (ucontext_t *)priv;


  DEBUG("TRAP signo 0x%x errno 0x%x code 0x%x ip %p\n", si->si_signo, si->si_errno, si->si_code,
      si->si_addr);
  DEBUG("TRAP ip=%p sp=%p fpcsr=%016lx gpcsr=%016lx\n", (void *)arch_get_ip(uc),
      (void *)arch_get_sp(uc), arch_get_fp_csr(uc), arch_get_gp_csr(uc));

  brk_trap_handler(si, uc);



  DEBUG("TRAP done\n");
}

// FPSpy gets here when the current instruction is a FP instruction that
// has generated an FP trap we care about.
// This should only happen in the AWAIT_FPE state.
void fp_trap_handler(siginfo_t *si, ucontext_t *uc) {
  if (abort_on_fpe) {
    abort();
  }

  monitoring_context_t *mc = find_monitoring_context(gettid());

  if (!mc) {
    arch_clear_fp_exceptions(uc);
    arch_mask_fp_traps(uc);
    if (control_round_config) {
      arch_set_round_config(uc, orig_round_config);
    }
    arch_reset_trap_mode(uc, 0);  // best effort
    ERROR("surprise state %d during %s (rip=%p)\n", mc->state, __func__, (void *)arch_get_ip(uc));

    abort_operation("Cannot find monitoring context during fp_trap_handler exec");
    return;
  }

  if (!(mc->count % sample_period)) {
    individual_trace_record_t r;
    r.time = arch_cycle_count() - mc->start_time;
    r.rip = (void *)arch_get_ip(uc);
    r.rsp = (void *)arch_get_sp(uc);
    r.code = si->si_code;
    r.mxcsr = arch_get_fp_csr(uc);
    if (arch_get_instr_bytes(uc, (uint8_t *)r.instruction, MAX_INSTR_SIZE) < 0) {
      ERROR("Failed to fetch instruction bytes\n");
    }
    r.pad = 0;

    //    DEBUG("writing record: %lu ip=%p sp=%p code=0x%x, fpcsr=%08x, inst=%08x\n",
    //           r.time, r.rip, r.rsp, r.code, r.mxcsr, *(uint32_t*)r.instruction);

    if (push_trace_record(mc, &r)) {
      ERROR("Failed to push record\n");
    }
  }


  if (mc->state == AWAIT_FPE) {
    arch_clear_fp_exceptions(uc);
    arch_mask_fp_traps(uc);
    if (control_round_config) {
      arch_set_round_config(uc, our_round_config);
    }
    arch_set_trap_mode(uc, &mc->trap_mode_state);
    mc->state = AWAIT_TRAP;
  } else {
    arch_clear_fp_exceptions(uc);
    arch_mask_fp_traps(uc);
    if (control_round_config) {
      arch_set_round_config(uc, orig_round_config);
    }
    arch_reset_trap_mode(uc, &mc->trap_mode_state);
    abort_operation("Surprise state during fp_trap_handler exec");
  }
}


//
// This is the entry for FP traps when regular SIGFPEs are used
//
static void sigfpe_handler(int sig, siginfo_t *si, void *priv) {
  ucontext_t *uc = (ucontext_t *)priv;

  DEBUG("SIGFPE signo 0x%x errno 0x%x code 0x%x ip %p \n", si->si_signo, si->si_errno, si->si_code,
      si->si_addr);
  DEBUG("SIGFPE ip=%p sp=%p fpcsr=%016lx gpcsr=%016lx\n", (void *)arch_get_ip(uc),
      (void *)arch_get_sp(uc), arch_get_fp_csr(uc), arch_get_gp_csr(uc));

  if (log_level > 0) {
    char buf[80];

    switch (si->si_code) {
      case FPE_FLTDIV:
        strcpy(buf, "FPE_FLTDIV");
        break;
      case FPE_FLTINV:
        strcpy(buf, "FPE_FLTINV");
        break;
      case FPE_FLTOVF:
        strcpy(buf, "FPE_FLTOVF");
        break;
      case FPE_FLTUND:
        strcpy(buf, "FPE_FLTUND");
        break;
      case FPE_FLTRES:
        strcpy(buf, "FPE_FLTRES");
        break;
      case FPE_FLTSUB:
        strcpy(buf, "FPE_FLTSUB");
        break;
      case FPE_INTDIV:
        strcpy(buf, "FPE_INTDIV");
        break;
      case FPE_INTOVF:
        strcpy(buf, "FPE_INTOVF");
        break;
      default:
        sprintf(buf, "UNKNOWN(0x%x)\n", si->si_code);
        break;
    }

    DEBUG("FPE %s\n", buf);
  }

  fp_trap_handler(si, uc);

  DEBUG("SIGFPE done\n");

  // copy back our limited gregset_t
}


#if CONFIG_INTERCEPT_MEMORY_FAULTS
//
// Allow our own interception for memory faults
// sigsegv/sigbus to facilate debugging in environments
// where gdb cannot be easily used.
//


static void memfault_handler(int sig, siginfo_t *si, void *priv) {
  ucontext_t *uc = (ucontext_t *)priv;
  void *ip = (void *)arch_get_ip(uc);
  void __attribute__((unused)) *sp = (void *)arch_get_sp(uc);
  void __attribute__((unused)) *addr = si->si_addr;

  DEBUG("%s ip=%p sp=%p addr=%p reason: %d (%s)\n",
      sig == SIGSEGV  ? "SIGSEGV"
      : sig == SIGBUS ? "SIGBUS"
                      : "UNKNOWN SIGNAL",
      ip, sp, addr, si->si_code,
      si->si_code == SEGV_MAPERR   ? "MAPERR"
      : si->si_code == SEGV_ACCERR ? "PERM"
                                   : "UNKNOWN");

  Dl_info dli;
  if (dladdr(ip, &dli)) {
    DEBUG("fname=%s fbase=%p sname=%s saddr=%p\n", dli.dli_fname ? dli.dli_fname : "UNKNOWN",
        dli.dli_fbase, dli.dli_sname ? dli.dli_sname : "UNKNOWN", dli.dli_saddr);
  } else {
    DEBUG("cannot resolve function\n");
  }


  // note that the following will likely be useless since we're looking at
  // the signal stack, not the application stack
  int count = 64;
  void *addrs[count];

  count = backtrace(addrs, count);
  if (count > 0) {
    backtrace_symbols_fd(addrs, count, STDERR_FILENO);
  } else {
    ERROR("cannot generate backtrace\n");
  }

  abort();
}

#endif


#define USE_MEMCPY 1



//
// Entry point for FP Trap for trap short circuiting (kernel module)
//
#if CONFIG_TRAP_SHORT_CIRCUITING
// this is currently completely x64-specific

// no need to manipulate mxcsr since all code here
// that might affect it should already safely wrap
// what is doing

static uint32_t get_mxcsr() {
  uint32_t val = 0;
  __asm__ __volatile__("stmxcsr %0" : "=m"(val) : : "memory");
  return val;
}


static inline void fxsave(struct _libc_fpstate *fpregs) {
  __asm__ __volatile__("fxsave (%0)" ::"r"(fpregs));
}

static inline void fxrstor(const struct _libc_fpstate *fpvm_fpregs) {
  __asm__ __volatile__("fxrstor (%0)" ::"r"(fpvm_fpregs));
}


#define USE_MEMCPY 1

// note that unlike FPVM, the handler WILL NOT and MUST NOT
// change any state except for possibly changing
// rflags.TF and mxcsr.trap bits
//
// See src/x64/user_fpspy_entry.S for a layout of
// the stack and what priv points to on entry.  The summary is
// that priv is pointing to the following, which is on the stack
//  Note that r8 through rflags is in gregset_t order
//
//  rsp + 144  [pad]    <= alignment pad
//  rsp + 136  rflags   ^
//             rip      |
//             rsp      |  like gregset
//             rcx      |
//             ...      |
//  rsp + 0    r8       v
//
// the handler will create a fake ucontext_t from this
// and a snapshot of FP state.  The user should assume
// the only parts of that ucontext_t to be trusted are
// the signal state, the above gregset_t subset, and
// the mxcsr in the fprs.
//
//
void fpspy_short_circuit_handler(void *priv) {
  // Build up a sufficiently detailed ucontext_t and
  // call the shared handler.  Copy in/out the FP and GP
  // state

  siginfo_t fake_siginfo = {0};
  struct _libc_fpstate fpregs;
  ucontext_t fake_ucontext;
  uint32_t old;

  // capture FP state (note that this eventually needs to do xsave)
  // no code we call can safely deal with the fpregs beyond SSE
  fxsave(&fpregs);

  old = get_mxcsr();

  uint32_t err = ~(old >> 7) & old;
  if (err & 0x001) { /* Invalid op*/
    fake_siginfo.si_code = FPE_FLTINV;
  } else if (err & 0x004) { /* Divide by Zero */
    fake_siginfo.si_code = FPE_FLTDIV;
  } else if (err & 0x008) { /* Overflow */
    fake_siginfo.si_code = FPE_FLTOVF;
  } else if (err & 0x012) { /* Denormal, Underflow */
    fake_siginfo.si_code = FPE_FLTUND;
  } else if (err & 0x020) { /* Precision */
    fake_siginfo.si_code = FPE_FLTRES;
  }

  siginfo_t *si = (siginfo_t *)&fake_siginfo;

  fake_ucontext.uc_mcontext.fpregs = &fpregs;

#if USE_MEMCPY
  memcpy(fake_ucontext.uc_mcontext.gregs, priv, 8 * (REG_EFL - REG_R8 + 1));
#else
  for (int i = REG_R8; i <= REG_EFL; i++) {
    fake_ucontext.uc_mcontext.gregs[i] = ((greg_t *)priv)[i];
  }
#endif

  ucontext_t *uc = (ucontext_t *)&fake_ucontext;

  uint8_t __attribute__((unused)) *rip = (uint8_t *)uc->uc_mcontext.gregs[REG_RIP];

  DEBUG(
      "SCFPE signo 0x%x errno 0x%x code 0x%x rip %p %02x %02x %02x %02x %02x "
      "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
      si->si_signo, si->si_errno, si->si_code, si->si_addr, rip[0], rip[1], rip[2], rip[3], rip[4],
      rip[5], rip[6], rip[7], rip[8], rip[9], rip[10], rip[11], rip[12], rip[13], rip[14], rip[15]);
  DEBUG("SCFPE RIP=%p RSP=%p\n", rip, (void *)uc->uc_mcontext.gregs[REG_RSP]);

  if (log_level > 0) {
    char buf[80];

    switch (si->si_code) {
      case FPE_FLTDIV:
        strcpy(buf, "FPE_FLTDIV");
        break;
      case FPE_FLTINV:
        strcpy(buf, "FPE_FLTINV");
        break;
      case FPE_FLTOVF:
        strcpy(buf, "FPE_FLTOVF");
        break;
      case FPE_FLTUND:
        strcpy(buf, "FPE_FLTUND");
        break;
      case FPE_FLTRES:
        strcpy(buf, "FPE_FLTRES");
        break;
      case FPE_FLTSUB:
        strcpy(buf, "FPE_FLTSUB");
        break;
      case FPE_INTDIV:
        strcpy(buf, "FPE_INTDIV");
        break;
      case FPE_INTOVF:
        strcpy(buf, "FPE_INTOVF");
        break;
      default:
        sprintf(buf, "UNKNOWN(0x%x)\n", si->si_code);
        break;
    }

    DEBUG("FPE exceptions %s\n", buf);
  }

  fp_trap_handler(si, uc);

  DEBUG("SCFPE  done\n");


#if USE_MEMCPY
  memcpy(priv, fake_ucontext.uc_mcontext.gregs, 8 * (REG_EFL - REG_R8 + 1));
#else
  for (int i = REG_R8; i <= REG_EFL; i++) {
    ((greg_t *)priv)[i] = fake_ucontext.uc_mcontext.gregs[i];
  }
#endif

  // restore FP state (note that this eventually needs to do xsave)
  // really, the only thing that should change is mxcsr, so this is
  // doing too much work
  fxrstor(&fpregs);

  return;
}
#endif



//
// FPSpy gets a SIGFPE when the current instruction is an FP instruction
// that has caused an FP exception that we care about.  This should
// only happen in the AWAIT_FPE state.
//

static __attribute__((destructor)) void fpspy_deinit(void);

//
// FPspy handles SIGINT so that it can do a gracefull
// shutdown and dump log files.  Due to this, the user
// can always see what FPSpy found, even on a premature stop
//
static void sigint_handler(int sig, siginfo_t *si, void *priv) {
  DEBUG("Handling break\n");

  if (oldsa_int.sa_sigaction) {
    fpspy_deinit();  // dump everything out
    // invoke underlying handler
    oldsa_int.sa_sigaction(sig, si, priv);
  } else {
    // exit - our deinit will be called
    exit(-1);
  }
}



//
// FPspy handles SIGALRM in time-based sampling mode.  The
// SIGALRM signifies the current interval is over
//
static void sigalrm_handler(int sig, siginfo_t *si, void *priv) {
  monitoring_context_t *mc = find_monitoring_context(gettid());
  ucontext_t *uc = (ucontext_t *)priv;

  DEBUG("Timeout for %d\n", gettid());

  if (!mc) {
    ERROR("Could not find monitoring context for %d\n", gettid());
    return;
  }
  if (mc->state != AWAIT_FPE) {
    // we are in the middle of handling an instruction, so we will
    // defer the transition until after this is done
    DEBUG("Delaying sampler processing because we are in the middle of an instruction\n");
    mc->sampler.delayed_processing = 1;
    return;
  } else {
    update_sampler(mc, uc);
  }
}



//
// monitoring context bringup and teardown
//

static int bringup_monitoring_context(int tid) {
  monitoring_context_t *c;
  char name[80];

  if (!(c = alloc_monitoring_context(tid))) {
    ERROR("Cannot allocate monitoring context\n");
    return -1;
  }

  if (create_monitor_file) {
    sprintf(name, "__%s.%lu.%d.individual.fpemon", program_invocation_short_name, time(0), tid);
    if ((c->fd = open(name, O_CREAT | O_WRONLY, 0666)) < 0) {
      ERROR("Cannot open monitoring output file\n");
      free_monitoring_context(tid);
      return -1;
    }
  }

#if CONFIG_TRAP_SHORT_CIRCUITING
  if (kernel && kernel_fd != -1) {
    extern void *_user_fpspy_entry;
    if (ioctl(kernel_fd, FPVM_IOCTL_REG, &_user_fpspy_entry)) {
      ERROR("SC failed to ioctl kernel support (/dev/fpvm_dev), very bad\n");
      abort_operation("thread failed to ioctl kernel support\n");
      return -1;
    } else {
      DEBUG("thread kernel setup successful\n");
    }
  }
#endif

#if CONFIG_RISCV_TRAP_PIPELINED_EXCEPTIONS
  init_pipelined_exceptions();
#endif

  c->start_time = arch_cycle_count();
  c->state = INIT;
  c->aborting_in_trap = 0;
  c->count = 0;
  c->trap_mode_state = 0;

  init_sampler(&c->sampler);

  return 0;
}


static int teardown_monitoring_context(int tid) {
  monitoring_context_t *mc = find_monitoring_context(tid);

  if (!mc) {
    ERROR("Cannot find monitoring context for %d\n", tid);
    return -1;
  }

  // add later - not relevant now PAD
  // deinit_sampler(&mc->sampler);

  flush_trace_records(mc);

  close(mc->fd);

  free_monitoring_context(tid);

  DEBUG("Tore down monitoring context for %d\n", tid);

  return 0;
}


//
// Bringup FPSpy in the process
//

static int bringup() {
  if (arch_process_init()) {
    ERROR("Cannot initialize architecture\n");
    return -1;
  }

  if (setup_shims()) {
    ERROR("Cannot setup shims\n");
    return -1;
  }

  ORIG_IF_CAN(feclearexcept, enabled_fp_traps);

#if CONFIG_INTERCEPT_MEMORY_FAULTS
  struct sigaction memsa;
  memset(&memsa, 0, sizeof(memsa));
  memsa.sa_sigaction = memfault_handler;
  memsa.sa_flags |= SA_SIGINFO;
  sigemptyset(&memsa.sa_mask);
  // old handlers not captured here since we
  // will abort in any case.   This option should
  // not be included for production, only debugging
  ORIG_IF_CAN(sigaction, SIGSEGV, &memsa, 0);
  ORIG_IF_CAN(sigaction, SIGBUS, &memsa, 0);

  //  *(int*)0=0;
#endif


  if (mode == INDIVIDUAL) {
    struct sigaction sa;

    int alarm_sig = timer_type == ITIMER_REAL      ? SIGALRM
                    : timer_type == ITIMER_VIRTUAL ? SIGVTALRM
                    : timer_type == ITIMER_PROF    ? SIGPROF
                                                   : SIGALRM;

    init_monitoring_contexts();

#if CONFIG_TRAP_SHORT_CIRCUITING
    // need to do this early because we rely on bringup_monitoring_context
    if (kernel && kernel_fd == -1) {
      kernel_fd = open("/dev/fpvm_dev", O_RDWR);
      if (kernel_fd < 0) {
        ERROR("SC failed to open kernel support (/dev/fpvm_dev), falling back to signal handler\n");
      }
    } else {
      DEBUG("skipping kernel support, even though it is enabled\n");
    }
#endif

    if (bringup_monitoring_context(gettid())) {
      // this can now happen due to bad kernel module
      // so should really do graceful abort
      ERROR("Failed to start up monitoring context at startup\n");
      return -1;
    }

#if CONFIG_TRAP_SHORT_CIRCUITING
    if (kernel && kernel_fd > 0) {
      goto skip_setup_sigfpe;
    }

#endif

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigfpe_handler;
    sa.sa_flags |= SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGINT);
    sigaddset(&sa.sa_mask, SIGTRAP);
    if (timers) {
      sigaddset(&sa.sa_mask, alarm_sig);
    }
    ORIG_IF_CAN(sigaction, SIGFPE, &sa, &oldsa_fpe);

#if CONFIG_TRAP_SHORT_CIRCUITING
  skip_setup_sigfpe:
#endif

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigtrap_handler;
    sa.sa_flags |= SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGINT);
    sigaddset(&sa.sa_mask, SIGTRAP);
    if (timers) {
      sigaddset(&sa.sa_mask, alarm_sig);
    }
    sigaddset(&sa.sa_mask, SIGFPE);
    ORIG_IF_CAN(sigaction, SIGTRAP, &sa, &oldsa_trap);

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigint_handler;
    sa.sa_flags |= SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGTRAP);
    if (timers) {
      sigaddset(&sa.sa_mask, alarm_sig);
    }

    ORIG_IF_CAN(sigaction, SIGINT, &sa, &oldsa_int);

    if (timers) {
      // only initialize timing if we need it
      DEBUG("Setting up timer interrupt handler\n");
      memset(&sa, 0, sizeof(sa));
      sa.sa_sigaction = sigalrm_handler;
      sa.sa_flags |= SA_SIGINFO;
      sigemptyset(&sa.sa_mask);
      sigaddset(&sa.sa_mask, SIGINT);
      ORIG_IF_CAN(sigaction, alarm_sig, &sa, &oldsa_alrm);
    }

    if (kickstart) {
      INFO("Send SIGTRAP to process %d to start\n", getpid());
    } else {
      // now kick ourselves to set the sse bits; we are currently in state INIT
      // this will also do the architecture init for the thread
      kick_self();
    }

  } else {
    // we need to bring up the architectural state for the thread
    if (arch_thread_init(0)) {
      ERROR("Failed to bring up thread architectural state\n");
      return -1;
    }
  }

  inited = 1;
  DEBUG("Done with setup\n");
  return 0;
}

//
// FPSpy runtime configuration, prior to bringup
//

static void config_exceptions(char *buf) {
  if (mode == AGGREGATE) {
    DEBUG("ignoring exception list for aggregate mode\n");
    return;
  }

  enabled_fp_traps = 0;
  /* The trap mask uses x86's notion of an FPE mask. Namely, exceptions are
   * delivered to the core only when the corresponding mask bit is 0!
   * Thus, clearing the mask (setting to all 0s) enables all FPEs! */
  arch_clear_trap_mask();  // Enable everything

  /* If we DON'T have one of these things in the provided exception list, then
   * selectively DISABLE each of the exceptions by setting the relevant mask bit
   * to 1. */
  if (strcasestr(buf, "inv")) {
    DEBUG("tracking INVALID\n");
    enabled_fp_traps |= FE_INVALID;
  } else {
    DEBUG("disabling INVALID\n");
    arch_set_trap_mask(FE_INVALID);
  }

  if (strcasestr(buf, "den")) {
    DEBUG("tracking DENORM\n");
    // not provided in standard interface, catch via arch-specific...
    enabled_fp_traps |= 0;
  } else {
    DEBUG("disabling DENORM\n");
    arch_set_trap_mask(FE_DENORM);
  }

  if (strcasestr(buf, "div")) {
    DEBUG("tracking DIVIDE_BY_ZERO\n");
    enabled_fp_traps |= FE_DIVBYZERO;
  } else {
    DEBUG("disabling DIVIDE_BY_ZERO\n");
    arch_set_trap_mask(FE_DIVBYZERO);
  }

  if (strcasestr(buf, "over")) {
    DEBUG("tracking OVERFLOW\n");
    enabled_fp_traps |= FE_OVERFLOW;
  } else {
    DEBUG("disabling OVERFLOW\n");
    arch_set_trap_mask(FE_OVERFLOW);
  }

  if (strcasestr(buf, "under")) {
    DEBUG("tracking UNDERFLOW\n");
    enabled_fp_traps |= FE_UNDERFLOW;
  } else {
    DEBUG("disabling UNDERFLOW\n");
    arch_set_trap_mask(FE_UNDERFLOW);
  }

  if (strcasestr(buf, "prec")) {
    DEBUG("tracking PRECISION\n");
    enabled_fp_traps |= FE_INEXACT;
  } else {
    DEBUG("disabling PRECISION\n");
    arch_set_trap_mask(FE_INEXACT);
  }
}

static void config_round_daz_ftz(char *buf) {
  orig_round_config = arch_get_machine_round_config();

  our_round_config = 0;

  if (strcasestr(buf, "pos")) {
    arch_set_round_mode(&our_round_config, FPSPY_ROUND_POSITIVE);
  } else if (strcasestr(buf, "neg")) {
    arch_set_round_mode(&our_round_config, FPSPY_ROUND_NEGATIVE);
  } else if (strcasestr(buf, "zer")) {
    arch_set_round_mode(&our_round_config, FPSPY_ROUND_ZERO);
  } else if (strcasestr(buf, "nea")) {
    arch_set_round_mode(&our_round_config, FPSPY_ROUND_NEAREST);
  } else {
    ERROR("Unknown rounding mode - avoiding rounding control\n");
    control_round_config = 0;
    return;
  }

  int which = 0;
  if (strcasestr(buf, "daz")) {
    which += 2;
  }
  if (strcasestr(buf, "ftz")) {
    which += 1;
  }
  arch_set_dazftz_mode(&our_round_config, which);

  control_round_config = 1;

  DEBUG("Configuring rounding control to 0x%08x\n", our_round_config);
}



// This is where FPSpy execution begins in a process -
// this is called on load of preload library, prior to main()
// of the target
static __attribute__((constructor)) void fpspy_init(void) {
  INFO("init\n");
  DEBUG("%s is located at 0x%016lx\n", __func__, (uintptr_t)fpspy_init);
  if (!inited) {
    if (getenv("FPSPY_DEBUG_LEVEL")) {
      char *nptr = getenv("FPSPY_DEBUG_LEVEL");
      char *endptr = NULL;
      long ret = strtol(nptr, &endptr, 10);
      if (*nptr != '\0' && *endptr == '\0' && (0 == ret || ret == 1)) {
        log_level = ret;
      } else {
        ERROR("FPSPY_DEBUG_LEVEL must be one of [0 | 1], but %ld was found\n", ret);
        abort();
      }
    }
    if (getenv("FPSPY_MODE")) {
      if (!strcasecmp(getenv("FPSPY_MODE"), "individual")) {
        if (!arch_machine_supports_fp_traps()) {
          ERROR(
              "FPSPY_MODE requests individual mode, but this machine does not support FP traps\n");
          abort();
        }
        mode = INDIVIDUAL;
        DEBUG("Setting INDIVIDUAL mode\n");
      } else {
        if (!strcasecmp(getenv("FPSPY_MODE"), "aggregate")) {
          mode = AGGREGATE;
          DEBUG("Setting AGGREGATE mode\n");
        } else {
          ERROR("FPSPY_MODE is given, but mode %s does not make sense\n", getenv("FPSPY_MODE"));
          abort();
        }
      }
    } else {
      mode = AGGREGATE;
      DEBUG("No FPSPY_MODE is given, so assuming AGGREGATE mode\n");
    }
    if (getenv("FPSPY_MAXCOUNT")) {
      maxcount = atoi(getenv("FPSPY_MAXCOUNT"));
    }
    if (getenv("FPSPY_AGGRESSIVE") && tolower(getenv("FPSPY_AGGRESSIVE")[0]) == 'y') {
      DEBUG("Setting AGGRESSIVE\n");
      aggressive = 1;
    }
    if ((getenv("FPSPY_DISABLE_PTHREADS") && tolower(getenv("FPSPY_DISABLE_PTHREADS")[0]) == 'y') ||
        (getenv("DISABLE_PTHREADS") && tolower(getenv("DISABLE_PTHREADS")[0]) == 'y')) {
      disable_pthreads = 1;
    }
    if (getenv("FPSPY_SAMPLE")) {
      sample_period = atoi(getenv("FPSPY_SAMPLE"));
      DEBUG("Setting sample period to %d\n", sample_period);
    }
    if (getenv("FPSPY_KERNEL") && tolower(getenv("FPSPY_KERNEL")[0]) == 'y') {
      DEBUG("Attempting to use FPSpy (i.e., FPVM) kernel suppport\n");
      kernel = 1;
    }
    if (getenv("FPSPY_POISSON")) {
      if (sscanf(getenv("FPSPY_POISSON"), "%lu:%lu", &on_mean_us, &off_mean_us) != 2) {
        ERROR("unsupported FPSPY_POISSON arguments\n");
        return;
      } else {
        DEBUG("Setting Poisson sampling %lu us off %lu us on\n", on_mean_us, off_mean_us);
        timers = 1;
      }
    }
    if (getenv("FPSPY_TIMER")) {
      if (!strcasecmp(getenv("FPSPY_TIMER"), "virtual")) {
        timer_type = ITIMER_VIRTUAL;
        DEBUG("Using virtual timer\n");
      } else if (!strcasecmp(getenv("FPSPY_TIMER"), "real")) {
        timer_type = ITIMER_REAL;
        DEBUG("Using real timer\n");
      } else if (!strcasecmp(getenv("FPSPY_TIMER"), "prof")) {
        timer_type = ITIMER_PROF;
        DEBUG("Using profiling timer\n");
      } else {
        ERROR("Unknown FPSPY_TIMER=%s type\n", getenv("FPSPY_TIMER"));
        return;
      }
    }
    if (getenv("FPSPY_SEED")) {
      random_seed = atol(getenv("FPSPY_SEED"));
    } else {
      random_seed = -1;  // random selection at mc start
    }
    if (getenv("FPSPY_EXCEPT_LIST")) {
      config_exceptions(getenv("FPSPY_EXCEPT_LIST"));
    }
    if (getenv("FPSPY_FORCE_ROUNDING")) {
      config_round_daz_ftz(getenv("FPSPY_FORCE_ROUNDING"));
    }
    if (getenv("FPSPY_KICKSTART") && tolower(getenv("FPSPY_KICKSTART")[0]) == 'y') {
      DEBUG("Enabling external kickstart (send SIGTRAP to begin)\n");
      kickstart = 1;
      // modify the environment variable so that children do
      // not also wait
      if (putenv("FPSPY_KICKSTART=n")) {
        ERROR("failed to rewrite FPSPY_KICKSTART\n");
      }
    }
    if (getenv("FPSPY_ABORT") && tolower(getenv("FPSPY_ABORT")[0]) == 'y') {
      abort_on_fpe = 1;
    }
    if (bringup()) {
      ERROR("cannot bring up framework\n");
      return;
    }
    return;
  } else {
    ERROR("already inited!\n");
    return;
  }
}

//
// This is invoked when a thread exits, and we are in
// aggregate mode.   The thread's aggregate info must be
// dumped to a file at this point
//
static void handle_aggregate_thread_exit() {
  char buf[80];
  int fd;
  DEBUG("Dumping aggregate exceptions\n");
  // show_current_fe_exceptions();
  sprintf(buf, "__%s.%lu.%d.aggregate.fpemon", program_invocation_short_name, time(0), gettid());
  if ((fd = open(buf, O_CREAT | O_WRONLY, 0666)) < 0) {
    ERROR("Cannot open monitoring output file\n");
  } else {
    if (!aborted) {
      stringify_current_fe_exceptions(buf);
      strcat(buf, "\n");
    } else {
      strcpy(buf, "ABORTED\n");
    }
    if (writeall(fd, buf, strlen(buf))) {
      ERROR("Failed to write all of monitoring output\n");
    }
    DEBUG("aggregate exception string: %s", buf);
    close(fd);
  }
}


// Last thing FPSpy should see.   This should get called
// when we fall off of main() in the target
static __attribute__((destructor)) void fpspy_deinit(void) {
  // destroy the tracer thread
  DEBUG("deinit\n");
  if (inited) {
    if (mode == AGGREGATE) {
      handle_aggregate_thread_exit();
    } else {
      teardown_monitoring_context(gettid());
      int i;
      DEBUG("FPE exceptions previously dumped to files - now closing them\n");
      for (i = 0; i < CONFIG_MAX_CONTEXTS; i++) {
        if (context[i].tid) {
          close(context[i].fd);
        }
      }
#if CONFIG_TRAP_SHORT_CIRCUITING
      if (kernel && kernel_fd > 0) {
        close(kernel_fd);
      }
#endif
      /* TODO: Close the RISC-V pipelined character device! */
    }
  }
  arch_process_deinit();
  inited = 0;
  DEBUG("done\n");
}
