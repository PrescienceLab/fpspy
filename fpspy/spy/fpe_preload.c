/*

  Preload library with floating point exception interception 
  aggregation via FPE sticky behavior and trap-and-emulate

  Copyright (c) 2017 Peter A. Dinda


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
#include <sys/reg.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>
#include <math.h>
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

#include "trace_record.h"

#include <sys/time.h>

#define DEBUG_OUTPUT 1
#define NO_OUTPUT 0

#if DEBUG_OUTPUT
#define DEBUG(S, ...) fprintf(stderr, "fpe_preload: debug(%8d): " S, gettid(), ##__VA_ARGS__)
#else 
#define DEBUG(S, ...) 
#endif

#if NO_OUTPUT
#define INFO(S, ...) 
#define ERROR(S, ...)
#else
#define INFO(S, ...) fprintf(stderr,  "fpe_preload: info(%8d): " S, gettid(), ##__VA_ARGS__)
#define ERROR(S, ...) fprintf(stderr, "fpe_preload: ERROR(%8d): " S, gettid(), ##__VA_ARGS__)
#endif

volatile static int inited=0;
volatile static int aborted=0; // set if the target is doing its own FPE processing
volatile static int maxcount=65546; // maximum number to record, per thread
volatile static int sample_period=1; // 1 = capture every one
volatile static int exceptmask=FE_ALL_EXCEPT; // which C99 exceptions to handle, default all
volatile static int mxcsrmask_base = 0x3f; // which sse exceptions to handle, default all (using base zero)

#define MXCSR_FLAG_MASK (mxcsrmask_base<<0)
#define MXCSR_MASK_MASK (mxcsrmask_base<<7)

volatile static enum {AGGREGATE,INDIVIDUAL} mode = AGGREGATE;
volatile static int aggressive = 0;

static int (*orig_fork)() = 0;
static int (*orig_pthread_create)(pthread_t *tid, const pthread_attr_t *attr, void *(*start)(void*), void *arg) = 0;
static int (*orig_pthread_exit)(void *ret) __attribute__((noreturn)) = 0;
static sighandler_t (*orig_signal)(int sig, sighandler_t func) = 0;
static int (*orig_sigaction)(int sig, const struct sigaction *act, struct sigaction *oldact) = 0;
static int (*orig_feenableexcept)(int) = 0 ;
static int (*orig_fedisableexcept)(int) = 0 ;
static int (*orig_fegetexcept)() = 0 ;
static int (*orig_feclearexcept)(int) = 0 ;
static int (*orig_fegetexceptflag)(fexcept_t *flagp, int excepts) = 0 ;
static int (*orig_feraiseexcept)(int excepts) = 0; 
static int (*orig_fesetexceptflag)(const fexcept_t *flagp, int excepts) = 0;
static int (*orig_fetestexcept)(int excepts) = 0;
static int (*orig_fegetround)(void) = 0;
static int (*orig_fesetround)(int rounding_mode) = 0;
static int (*orig_fegetenv)(fenv_t *envp) = 0;
static int (*orig_feholdexcept)(fenv_t *envp) = 0;
static int (*orig_fesetenv)(const fenv_t *envp) = 0;
static int (*orig_feupdateenv)(const fenv_t *envp) = 0;


static struct sigaction oldsa_fpe, oldsa_trap, oldsa_int, oldsa_alrm;


#define ORIG_RETURN(func,...) if (orig_##func) { return orig_##func(__VA_ARGS__); } else { ERROR("cannot call orig_" #func " returning zero\n"); return 0; }
#define ORIG_IF_CAN(func,...) if (orig_##func) { if (!DEBUG_OUTPUT) { orig_##func(__VA_ARGS__); } else { DEBUG("orig_"#func" returns 0x%x\n",orig_##func(__VA_ARGS__)); } } else { DEBUG("cannot call orig_" #func " - skipping\n"); }
//#define SHOW_CALL_STACK() DEBUG("callstack (3 deep) : %p -> %p -> %p\n", __builtin_return_address(3), __builtin_return_address(2), __builtin_return_address(1))
//#define SHOW_CALL_STACK() DEBUG("callstack (2 deep) : %p -> %p\n", __builtin_return_address(2), __builtin_return_address(1))
//#define SHOW_CALL_STACK() DEBUG("callstack (1 deep) : %p\n", __builtin_return_address(1))
#define SHOW_CALL_STACK()

#define MAX_CONTEXTS 1024


static inline uint64_t __attribute__((always_inline)) rdtsc(void)
{
  uint32_t lo, hi;
  asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return lo | ((uint64_t)(hi) << 32);
}

static inline int gettid()
{
  return syscall(SYS_gettid);
}

// This is to allow us to handle multiple threads 
// and to follow forks later
typedef struct monitoring_context {
  uint64_t start_time; // cycles when context created
  enum {INIT, AWAIT_FPE, AWAIT_TRAP, ABORT} state;
  int aborting_in_trap;
  int tid;
  int fd; 
  uint64_t count;
} monitoring_context_t;

static int  context_lock;
static monitoring_context_t context[MAX_CONTEXTS];

static void init_monitoring_contexts()
{
  memset(context,0,sizeof(context));
  context_lock=0;
}

static void lock_contexts()
{
  while (!__sync_bool_compare_and_swap(&context_lock,0,1)) {}
}

static void unlock_contexts()
{
  __sync_and_and_fetch(&context_lock,0);
}



static monitoring_context_t *find_monitoring_context(int tid)
{
  int i;
  lock_contexts();
  for (i=0;i<MAX_CONTEXTS;i++) { 
    if (context[i].tid == tid) {
      unlock_contexts();
      return &context[i];
    }
  }
  unlock_contexts();
  return 0;
}

static monitoring_context_t *alloc_monitoring_context(int tid)
{
  int i;
  lock_contexts();
  for (i=0;i<MAX_CONTEXTS;i++) { 
    if (!context[i].tid) {
      context[i].tid = tid;
      unlock_contexts();
      return &context[i];
    }
  }
  unlock_contexts();
  return 0;
}

static void free_monitoring_context(int tid)
{
  int i;
  lock_contexts();
  for (i=0;i<MAX_CONTEXTS;i++) { 
    if (context[i].tid == tid) {
      context[i].tid = 0;
      unlock_contexts();
    }
  }
  unlock_contexts();
}

// Expected value is 1/rate_parameter
static float next_exp(float rate_parameter){
  double u = rand() / ((double)RAND_MAX);
  return -(log(1-u))/rate_parameter;
}

typedef struct timer_state {
  enum {ON, OFF} state;
  struct itimerval it;
} timer_state_t;

static timer_state_t timer; // Struct to hold timer state

#define MAX_TIMER_GRANULARITY 200
// ^--- Rigorously defined

// Timer will only be set in OFF state
static void set_timer(time_t arrival_sec, suseconds_t arrival_usec, time_t service_sec, suseconds_t service_usec, char* string){
  if (arrival_usec < MAX_TIMER_GRANULARITY) {
    arrival_usec = MAX_TIMER_GRANULARITY;
  }

  if (service_usec < MAX_TIMER_GRANULARITY) {
    service_usec = MAX_TIMER_GRANULARITY;
  }
  DEBUG("%lu: Arrival: %lu Service: %lu \n", rdtsc(), arrival_usec, service_usec);  
  struct itimerval it = {
    .it_interval = {
      .tv_sec = 0,//arrival_sec,
      .tv_usec = arrival_usec,
    },
    .it_value = {
      .tv_sec = 0,//service_sec,
      .tv_usec =  service_usec,
    }
  };
  setitimer(ITIMER_VIRTUAL,&it, NULL);
}

static void set_timer_exp(float rate_parameter, char *string){
  double arrival = next_exp(rate_parameter);
  double arrival_integral_ptr;
  /* modf gives the integral part of the floating point number as ret parameter */
  /* Fractional part of the floating point number is in frac_ptr */
  // This will be fixed;
  double arrival_frac_ptr = modf(arrival, &arrival_integral_ptr);
  int afp = (int)(arrival_frac_ptr*1000000);
  int aip = (int)arrival_integral_ptr;
  double service = next_exp(rate_parameter);
  double service_integral_ptr;
  double service_frac_ptr = modf(service, &service_integral_ptr);
  int sfp = (int)(service_frac_ptr*1000000);
  int sip = (int)service_integral_ptr;
  set_timer(aip,afp,sip,sfp, string);
}

#define TIME_S 1 // Time seconds
#define TIME_M 0 // Time microseconds

static void init_timer_state(void) {
  DEBUG("Init timer state\n");
  timer = (timer_state_t){
    .state = ON,
    .it = {
      .it_interval = {
	.tv_sec = 0, // Never repeat
	.tv_usec = 0,
      },
      .it_value = {
	.tv_sec = TIME_S,
	.tv_usec = TIME_M,
      },
    }
  };
  setitimer(ITIMER_VIRTUAL, &(timer.it), NULL); // Init wait for 1 second
  DEBUG("End timer state\n");
}


static void stringify_current_fe_exceptions(char *buf)
{
  int have=0;
  buf[0]=0;

#define FE_HANDLE(x) if (orig_fetestexcept(x)) { if (!have) { strcat(buf,#x); have=1; } else {strcat(buf," " #x ); } }
  FE_HANDLE(FE_DIVBYZERO);
  FE_HANDLE(FE_INEXACT);
  FE_HANDLE(FE_INVALID);
  FE_HANDLE(FE_OVERFLOW);
  FE_HANDLE(FE_UNDERFLOW);
  if (!have) {
    strcpy(buf,"NO_EXCEPTIONS_RECORDED");
  }
}

/*
static void show_current_fe_exceptions()
{
  char buf[80];
  stringify_current_fe_exceptions(buf);
  INFO("%s\n", buf);
}
*/

static int writeall(int fd, void *buf, int len)
{
  int n;
  int left = len;

  do {
    n = write(fd,buf,left);
    if (n<0) {
      return -1;
    }
    left -= n;
    buf += n;
  } while (left>0);
  
  return 0;
}



static __attribute__((constructor)) void fpe_preload_init(void);


static inline void set_trap_flag_context(ucontext_t *uc, int val)
{
  if (val) {
    uc->uc_mcontext.gregs[REG_EFL] |= 0x100UL; 
  } else {
    uc->uc_mcontext.gregs[REG_EFL] &= ~0x100UL; 
  }
}


static inline void clear_fp_exceptions_context(ucontext_t *uc)
{
  uc->uc_mcontext.fpregs->mxcsr &= ~MXCSR_FLAG_MASK; 
}

static inline void set_mask_fp_exceptions_context(ucontext_t *uc, int mask)
{
  if (mask) {
    uc->uc_mcontext.fpregs->mxcsr |= MXCSR_MASK_MASK;
  } else {
    uc->uc_mcontext.fpregs->mxcsr &= ~MXCSR_MASK_MASK;
  }
}

static void abort_operation(char *reason)
{
  if (!inited) {
    DEBUG("Initializing before abortingi\n");
    fpe_preload_init();
    DEBUG("Done with fpe_preload_init()\n");
  }

  if (!aborted) {
    ORIG_IF_CAN(fedisableexcept,FE_ALL_EXCEPT);
    ORIG_IF_CAN(feclearexcept,FE_ALL_EXCEPT);
    ORIG_IF_CAN(sigaction,SIGFPE,&oldsa_fpe,0);

    if (mode==INDIVIDUAL) {

      monitoring_context_t *mc = find_monitoring_context(gettid());

      if (!mc) {
	ERROR("Cannot find monitoring context to write abort record\n");
      } else {
	
	mc->state = ABORT;

	// write an abort record
	struct individual_trace_record r;
	memset(&r,0xff,sizeof(r));

	r.time = rdtsc() - mc->start_time;
	
	if (writeall(mc->fd,&r,sizeof(r))) {
	  ERROR("Failed to write abort record\n");
	}
	
      }

      // even if we have no monitoring context we need to restore
      // the mcontext.  If we do have a monitoring context,
      // and we are a trap, the mcontext has already been restored
      if (!mc || !mc->aborting_in_trap) {
	// signal ourselves to restore the FP and TRAP state in the context
	kill(gettid(),SIGTRAP);
      }
    }

    // finally remove our trap handler
    ORIG_IF_CAN(sigaction,SIGTRAP,&oldsa_trap,0);
    
    aborted = 1;
    DEBUG("Aborted operation because %s\n",reason);
  }
}

static int bringup_monitoring_context(int tid);


int fork()
{
  int rc;

  DEBUG("fork\n");

  rc = orig_fork();

  if (aborted) {
    return rc;
  }
  
  if (rc<0) {
    DEBUG("fork failed\n");
    return rc;
  }

  if (rc==0) {
    // child 

    // clear exceptions - we will not inherit the current ones from the parent
    ORIG_IF_CAN(feclearexcept,exceptmask);

    // in aggregate mode, a distinct log file will be generated by the destructor
    
    // make new context for individual mode
    if (mode==INDIVIDUAL) {
      
      if (bringup_monitoring_context(gettid())) { 
	ERROR("Failed to start up monitoring context at fork\n");
	// we won't break, however.. 
      } else {
	// we should have inherited all the sighandlers, etc, from our parent
	
	// now kick ourselves to set the sse bits; we are currently in state INIT
	kill(gettid(),SIGTRAP);
	// we should now be in the right state
      }
      
    }
    DEBUG("Done with setup on fork\n");
    return rc;

  } else {
    // parent - nothing to do
    return rc;
  }
}

struct tramp_context {
  void *(*start)(void *);
  void *arg;
  int  done;
};

static void handle_aggregate_thread_exit();

static void *trampoline(void *p)
{
  struct tramp_context *c = (struct tramp_context *)p;
  void *(*start)(void *) = c->start;
  void *arg = c->arg;
  void *ret;

  // let our wrapper go - this must also be a software barrier
  __sync_fetch_and_or(&c->done,1);

  DEBUG("Setting up thread %d\n",gettid());
  
  // clear exceptions just in case
  ORIG_IF_CAN(feclearexcept,exceptmask);
  
  if (mode==INDIVIDUAL) {

    // make new context for individual mode
    if (bringup_monitoring_context(gettid())) { 
      ERROR("Failed to start up monitoring context on thread creation\n");
      // we won't break, however.. 
    } else {
      // we should have inherited all the sighandlers, etc, from the spawning thread
      
      // now kick ourselves to set the sse bits; we are currently in state INIT
      kill(gettid(),SIGTRAP);
      // we should now be in the right state
    }
    DEBUG("Done with setup on thread creation\n");
  }
 
  DEBUG("leaving trampoline\n");
  
  ret = start(arg);

  // if it's returning normally instead of via pthread_exit(), we'll do the cleanup here
  pthread_exit(ret);
  
}

int pthread_create(pthread_t *tid, const pthread_attr_t *attr, void *(*start)(void*), void *arg)
{
  struct tramp_context c;

  DEBUG("pthread_create\n");

  if (aborted) {
    return orig_pthread_create(tid,attr,start,arg);
  }
  
  c.start = start;
  c.arg = arg;
  c.done = 0;

  int rc = orig_pthread_create(tid,attr,trampoline,&c);

  if (!rc) { 
    // don't race on the tramp context - wait for thread to copy out
    while (!__sync_fetch_and_and(&c.done,1)) { }
  }

  DEBUG("pthread_create done\n");

  return rc;
}


__attribute__((noreturn)) void pthread_exit(void *ret)  
{
  DEBUG("pthread_exit(%p)\n",ret);

  // we will process this even if we have aborted, since
  // we want to flush aggregate info even if it's just an abort record
  if (mode==INDIVIDUAL) {
    // nothing to do since we've been writing the log file all along
    // and we will flush them all on exit
  } else {
    handle_aggregate_thread_exit();
  }

  orig_pthread_exit(ret);
}


sighandler_t signal(int sig, sighandler_t func)
{
  DEBUG("signal(%d,%p)\n",sig,func);
  SHOW_CALL_STACK();
  if ((sig==SIGFPE || sig==SIGTRAP) && mode==INDIVIDUAL && !aborted) {
    if (!aggressive) { 
      abort_operation("target is using sigaction with SIGFPE or SIGTRAP (nonaggressive)");
    } else {
      // do not override our signal handlers - we are not aborting
      DEBUG("not overriding SIGFPE or SIGTRAP because we are in aggressive mode\n");
      return 0;
    }
  }
  ORIG_RETURN(signal,sig,func);
}



int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact)
{
  DEBUG("sigaction(%d,%p,%p)\n",sig,act,oldact);
  SHOW_CALL_STACK();
  if ((sig == SIGVTALRM || sig==SIGFPE || sig==SIGTRAP) && mode==INDIVIDUAL && !aborted) {
    if (!aggressive) { 
      abort_operation("target is using sigaction with SIGFPE, SIGTRAP, or SIGVTALRM");
    } else {
      // do not override our signal handlers - we are not aborting
      DEBUG("not overriding SIGFPE or SIGTRAP because we are in aggressive mode\n");
      return 0;
    }
  }
  ORIG_RETURN(sigaction,sig,act,oldact);
}


int feclearexcept(int excepts)
{
  DEBUG("feclearexcept(0x%x)\n",excepts);
  SHOW_CALL_STACK();
  abort_operation("target is using feclearexcept");
  ORIG_RETURN(feclearexcept,excepts);
}

int feenableexcept(int excepts)
{
  DEBUG("feenableexcept(0x%x)\n",excepts);
  SHOW_CALL_STACK();
  abort_operation("target is using feenableexcept");
  ORIG_RETURN(feenableexcept,excepts);
}

int fedisableexcept(int excepts)
{
  DEBUG("fedisableexcept(0x%x)\n",excepts);
  SHOW_CALL_STACK();
  abort_operation("target is using fedisableexcept");
  ORIG_RETURN(fedisableexcept,excepts);
}

int fegetexcept(void)
{
  DEBUG("fegetexcept()\n");
  SHOW_CALL_STACK();
  abort_operation("target is using fegetexcept");
  ORIG_RETURN(fegetexcept);
}

int fegetexceptflag(fexcept_t *flagp, int excepts)
{
  DEBUG("fegetexceptflag(%p,0x%x)\n",flagp,excepts);
  SHOW_CALL_STACK();
  abort_operation("target is using fegetexceptflag");
  ORIG_RETURN(fegetexceptflag, flagp, excepts);
}

int feraiseexcept(int excepts) 
{
  DEBUG("feraiseexcept(0x%x)\n",excepts);
  SHOW_CALL_STACK();
  abort_operation("target is using feraiseexcept");
  ORIG_RETURN(feraiseexcept,excepts);
}

int fesetexceptflag(const fexcept_t *flagp, int excepts)
{
  DEBUG("fesetexceptflag(%p,0x%x\n",flagp,excepts);
  SHOW_CALL_STACK();
  abort_operation("target is using fesetexceptflag");
  ORIG_RETURN(fesetexceptflag, flagp, excepts);
}

int fetestexcept(int excepts)
{
  DEBUG("fesetexcept(0x%x)\n",excepts);
  SHOW_CALL_STACK();
  abort_operation("target is using fetestexcept");
  ORIG_RETURN(fetestexcept, excepts);
}

int fegetround(void)
{
  DEBUG("fegetround()\n");
  SHOW_CALL_STACK();
  abort_operation("target is using fegetround");
  ORIG_RETURN(fegetround);
}

int fesetround(int rounding_mode)
{
  DEBUG("fesetround(0x%x)\n",mode);
  SHOW_CALL_STACK();
  abort_operation("target is using fesetround");
  ORIG_RETURN(fesetround,rounding_mode);
}

int fegetenv(fenv_t *envp)
{
  DEBUG("fegetenv(%p)\n",envp);
  SHOW_CALL_STACK();
  abort_operation("target is using fegetenv");
  ORIG_RETURN(fegetenv,envp);

}

int feholdexcept(fenv_t *envp)
{
  DEBUG("feholdexcept(%p)\n",envp);
  SHOW_CALL_STACK();
  abort_operation("target is using feholdexcept");
  ORIG_RETURN(feholdexcept,envp);
}


int fesetenv(const fenv_t *envp)
{
  DEBUG("fesetenv(%p)\n",envp);
  SHOW_CALL_STACK();
  abort_operation("target is using fesetenv");
  ORIG_RETURN(fesetenv,envp);
}

int feupdateenv(const fenv_t *envp)
{
  DEBUG("feupdateenv(%p)\n",envp);
  SHOW_CALL_STACK();
  abort_operation("target is using feupdateenv");
  ORIG_RETURN(feupdateenv,envp);
}

    
static int setup_shims()
{
#define SHIMIFY(x) if (!(orig_##x = dlsym(RTLD_NEXT, #x))) { DEBUG("Failed to setup SHIM for " #x "\n");  return -1; }

  SHIMIFY(fork);
  SHIMIFY(pthread_create);
  SHIMIFY(pthread_exit);
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



static void sigtrap_handler(int sig, siginfo_t *si, void *priv)
{
  monitoring_context_t *mc = find_monitoring_context(gettid());
  ucontext_t *uc = (ucontext_t *)priv;

  if (!mc || mc->state==ABORT) { 
    clear_fp_exceptions_context(uc);     // exceptions cleared
    set_mask_fp_exceptions_context(uc,1);// exceptions masked
    set_trap_flag_context(uc,0);         // traps disabled
    if (!mc) {
      // this may end badly
      abort_operation("Cannot find monitoring context during sigtrap_handler exec");
    } else {
      DEBUG("FP and TRAP mcontext restored on abort\n");
    }
    return;
  }

  if (mc && mc->state==INIT) {
    clear_fp_exceptions_context(uc);     // exceptions cleared
    set_mask_fp_exceptions_context(uc,0);// exceptions unmasked
    set_trap_flag_context(uc,0);         // traps disabled
    mc->state = AWAIT_FPE;
    DEBUG("MXCSR state initialized\n");
    return;
  }
  
  DEBUG("TRAP signo 0x%x errno 0x%x code 0x%x rip %p\n",
	si->si_signo, si->si_errno, si->si_code, si->si_addr);

  if (mc->state == AWAIT_TRAP) { 
    mc->count++;
    clear_fp_exceptions_context(uc);      // exceptions cleared
    if (maxcount!=-1 && mc->count >= maxcount) { 
      // disable further operation since we've recorded enough
      set_mask_fp_exceptions_context(uc,1); // exceptions masked
    } else {
      set_mask_fp_exceptions_context(uc,0); // exceptions unmasked
    }
    set_trap_flag_context(uc,0);          // traps disabled
    mc->state = AWAIT_FPE;
  } else {
    clear_fp_exceptions_context(uc);     // exceptions cleared
    set_mask_fp_exceptions_context(uc,1);// exceptions masked
    set_trap_flag_context(uc,0);         // traps disabled
    mc->aborting_in_trap = 1;
    abort_operation("Surprise state during sigtrap_handler exec");
  }
}



static void sigfpe_handler(int sig, siginfo_t *si,  void *priv)
{
  monitoring_context_t *mc = find_monitoring_context(gettid());
  ucontext_t *uc = (ucontext_t *)priv;

  if (!mc) {
    clear_fp_exceptions_context(uc);     // exceptions cleared
    set_mask_fp_exceptions_context(uc,1);// exceptions masked
    set_trap_flag_context(uc,0);         // traps disabled
    abort_operation("Cannot find monitoring context during sigfpe_handler exec");
    return;
  }

  if (!(mc->count % sample_period)) { 
    individual_trace_record_t r;  

    r.time = rdtsc() - mc->start_time;
    r.rip = (void*) uc->uc_mcontext.gregs[REG_RIP];
    r.rsp = (void*) uc->uc_mcontext.gregs[REG_RSP];
    r.code =  si->si_code;
    r.mxcsr =  uc->uc_mcontext.fpregs->mxcsr;
    memcpy(r.instruction,r.rip,15);
    r.pad = 0;
    
    if (writeall(mc->fd,&r,sizeof(r))) {
      ERROR("Failed to write record\n");
    }
  }
  
#if DEBUG_OUTPUT
  char buf[80];
#define CASE(X) case X : strcpy(buf, #X); break; 
  switch (si->si_code) {
    CASE(FPE_FLTDIV);
    CASE(FPE_FLTINV);
    CASE(FPE_FLTOVF);
    CASE(FPE_FLTUND);
    CASE(FPE_FLTRES);
    CASE(FPE_FLTSUB);
    CASE(FPE_INTDIV);
    CASE(FPE_INTOVF);
  default:
    sprintf(buf,"UNKNOWN(0x%x)\n",si->si_code);
    break;
  }
#endif
  
  DEBUG("FPE signo 0x%x errno 0x%x code 0x%x rip %p %s\n",
	si->si_signo, si->si_errno, si->si_code, si->si_addr,buf);
  DEBUG("FPE RIP=%p RSP=%p\n",
	(void*) uc->uc_mcontext.gregs[REG_RIP], (void*)  uc->uc_mcontext.gregs[REG_RSP]);
    
  if (mc->state == AWAIT_FPE) { 
    clear_fp_exceptions_context(uc);      // exceptions cleared
    set_mask_fp_exceptions_context(uc,1); // exceptions masked
    set_trap_flag_context(uc,1);          // traps enabled
    mc->state = AWAIT_TRAP;
  } else {
    clear_fp_exceptions_context(uc);     // exceptions cleared
    set_mask_fp_exceptions_context(uc,1);// exceptions masked
    set_trap_flag_context(uc,0);         // traps disabled
    abort_operation("Surprise state during sigfpe_handler exec");
  }
}

static __attribute__((destructor)) void fpe_preload_deinit(void);


static void sigint_handler(int sig, siginfo_t *si,  void *priv)
{

  DEBUG("Handling break\n");


  if (oldsa_int.sa_sigaction) { 
    fpe_preload_deinit(); // dump everything out
    // invoke underlying handler
    oldsa_int.sa_sigaction(sig,si,priv);
  } else {
    // exit - our deinit will be called
    exit(-1);
  }
}
  
static void sigalrm_handler(int sig, siginfo_t *si,  void *priv){

  DEBUG("Handling Alarm\n");
  
  ucontext_t *uc = (ucontext_t *)priv;

  if (timer.state == ON){
    DEBUG("STATE IS ON GOING TO OFF\n");
    // Alarm received while in ON state
    // Mask FPE & set itimer
    clear_fp_exceptions_context(uc); // Clear fpe 
    set_mask_fp_exceptions_context(uc,1); // Mask fpe
    //    set_trap_flag_context(uc,0); // disable traps
    timer.state = OFF;
    set_timer_exp(100, "OFF"); 
  } else {
    DEBUG("STATE IS OFF GOING TO ON\n");
    clear_fp_exceptions_context(uc); // Clear fpe
    set_mask_fp_exceptions_context(uc,0); //Unmask fpe
    //    set_trap_flag_context(uc,1); // enable traps
    timer.state = ON;
  }
}

static int bringup_monitoring_context(int tid)
{
  monitoring_context_t *c;
  char name[80];

  if (!(c = alloc_monitoring_context(tid))) { 
    ERROR("Cannot allocate monitoring context\n");
    return -1;
  }

  sprintf(name,"__%s.%lu.%d.individual.fpemon", program_invocation_short_name, time(0), tid);
  if ((c->fd = open(name,O_CREAT | O_WRONLY, 0666))<0) { 
    ERROR("Cannot open monitoring output file\n");
    free_monitoring_context(tid);
    return -1;
  }

  c->start_time = rdtsc();
  c->state = INIT;
  c->aborting_in_trap = 0;
  c->count = 0;

  return 0;
}

 
static int bringup()
{
  if (setup_shims()) { 
    ERROR("Cannot setup shims\n");
    return -1;
  }

  ORIG_IF_CAN(feclearexcept,exceptmask);

  if (mode==INDIVIDUAL) {
    
    init_monitoring_contexts();

    if (bringup_monitoring_context(gettid())) { 
      ERROR("Failed to start up monitoring context at startup\n");
      return -1;
    }

    struct sigaction sa;

    memset(&sa,0,sizeof(sa));
    sa.sa_sigaction = sigfpe_handler;
    sa.sa_flags |= SA_SIGINFO;
   
    ORIG_IF_CAN(sigaction,SIGFPE,&sa,&oldsa_fpe);

    memset(&sa,0,sizeof(sa));
    sa.sa_sigaction = sigtrap_handler;
    sa.sa_flags |= SA_SIGINFO;
    
    ORIG_IF_CAN(sigaction,SIGTRAP,&sa,&oldsa_trap);

    memset(&sa,0,sizeof(sa));
    sa.sa_sigaction = sigint_handler;
    sa.sa_flags |= SA_SIGINFO;
    
    ORIG_IF_CAN(sigaction,SIGINT,&sa,&oldsa_int);

    memset(&sa, 0,sizeof(sa));
    sa.sa_sigaction = sigalrm_handler;
    sa.sa_flags |= SA_SIGINFO;
    if (sigaddset(&sa.sa_mask,SIGVTALRM) != 0){
      DEBUG("UNABLE TO MASK SIGVTALRM");
    }

    ORIG_IF_CAN(sigaction,SIGVTALRM,&sa,&oldsa_alrm);
    
    ORIG_IF_CAN(feenableexcept,exceptmask);

    
    
    // now kick ourselves to set the sse bits; we are currently in state INIT

    kill(getpid(),SIGTRAP);
  }
  init_timer_state();

  inited=1;
  DEBUG("Done with setup\n");
  return 0;
}

// 

static void config_exceptions(char *buf)
{
  if (mode==AGGREGATE) {
    DEBUG("ignoring exception list for aggregate mode\n");
    return ;
  }
  
  exceptmask = 0;
  mxcsrmask_base = 0;
  
  if (strcasestr(buf,"inv")) {
    DEBUG("tracking INVALID\n");
    exceptmask |= FE_INVALID ;
    mxcsrmask_base |= 0x1;
  }
  if (strcasestr(buf,"den")) {
    DEBUG("tracking DENORM\n");
    exceptmask |= 0 ; // not provided...  
    mxcsrmask_base |= 0x2;
  }
  if (strcasestr(buf,"div")) {
    DEBUG("tracking DIVIDE_BY_ZERO\n");
    exceptmask |= FE_DIVBYZERO ;
    mxcsrmask_base |= 0x4;
  }
  if (strcasestr(buf,"over")) {
    DEBUG("tracking OVERFLOW\n");
    exceptmask |= FE_OVERFLOW;
    mxcsrmask_base |= 0x8;
  }
  if (strcasestr(buf,"under")) {
    DEBUG("tracking UNDERFLOW\n");
    exceptmask |= FE_UNDERFLOW ;
    mxcsrmask_base |= 0x10;
  }
  if (strcasestr(buf,"prec")) {
    DEBUG("tracking PRECISION\n");
    exceptmask |= FE_INEXACT ;
    mxcsrmask_base |= 0x20;
  }

}



// Called on load of preload library
static __attribute__((constructor)) void fpe_preload_init(void) 
{

  INFO("init\n");
  if (!inited) { 
    if (getenv("FPE_MODE")) {
      if (!strcasecmp(getenv("FPE_MODE"),"individual")) { 
	mode=INDIVIDUAL;
	DEBUG("Setting INDIVIDUAL mode\n");
      } else {
	if (!strcasecmp(getenv("FPE_MODE"),"aggregate")) {
	  mode=AGGREGATE;
	  DEBUG("Setting AGGREGATE mode\n");
	} else {
	  ERROR("FPE_MODE is given, but mode %s does not make sense\n",getenv("FPE_MODE"));
	  abort();
	}
      } 
    } else {
      mode=AGGREGATE;
      DEBUG("No FPE_MODE is given, so assuming AGGREGATE mode\n");
    }
    if (getenv("FPE_MAXCOUNT")) { 
      maxcount = atoi(getenv("FPE_MAXCOUNT"));
    }
    if (getenv("FPE_AGGRESSIVE") && tolower(getenv("FPE_AGGRESSIVE")[0])=='y') {
      DEBUG("Setting AGGRESSIVE\n");
      aggressive=1;
    }
    if (getenv("FPE_SAMPLE")) {
      sample_period = atoi(getenv("FPE_SAMPLE"));
      DEBUG("Setting sample period to %d\n", sample_period);
    }
    if (getenv("FPE_EXCEPT_LIST")) {
      config_exceptions(getenv("FPE_EXCEPT_LIST"));
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


static void handle_aggregate_thread_exit()
{
  char buf[80];
  int fd;
  DEBUG("Dumping aggregate exceptions\n");
  //show_current_fe_exceptions();
  sprintf(buf,"__%s.%lu.%d.aggregate.fpemon", program_invocation_short_name, time(0),gettid());
  if ((fd = open(buf,O_CREAT | O_WRONLY, 0666))<0) { 
    ERROR("Cannot open monitoring output file\n");
  } else {
    if (!aborted) { 
      stringify_current_fe_exceptions(buf);
      strcat(buf,"\n");
    } else {
      strcpy(buf,"ABORTED\n");
    }
    if (writeall(fd,buf,strlen(buf))) {
      ERROR("Failed to write all of monitoring output\n");
    }
    DEBUG("aggregate exception string: %s",buf);
    close(fd);
  }
}

    
// Called on unload of preload library
static __attribute__((destructor)) void fpe_preload_deinit(void) 
{ 
  // destroy the tracer thread
  DEBUG("deinit\n");
  if (inited) { 
    if (mode==AGGREGATE) {
      handle_aggregate_thread_exit();
    } else {
      int i;
      DEBUG("FPE exceptions previously dumped to files - now closing them\n");
      for (i=0;i<MAX_CONTEXTS;i++) { 
	if (context[i].tid) { 
	  close(context[i].fd);
	}
      }
    }
  }
  inited=0;
  DEBUG("done\n");
}

