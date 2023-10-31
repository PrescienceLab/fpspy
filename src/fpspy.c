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
#include <sys/reg.h>
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

#include <math.h>

#include "config.h"
#include "debug.h"
#include "arch.h"
#include "trace_record.h"

#include <sys/time.h>



volatile static int inited=0;
volatile static int aborted=0; // set if the target is doing its own FPE processing
volatile static int maxcount=65546; // maximum number to record, per thread
volatile static int sample_period=1; // 1 = capture every one

volatile static uint64_t random_seed;

volatile static int timers=0; // are using timing-based sampling?
// used for poisson sampler
volatile static uint64_t on_mean_us, off_mean_us;

// user for timer config
volatile static int timer_type = ITIMER_REAL;

volatile static int exceptmask=FE_ALL_EXCEPT; // which C99 exceptions to handle, default all

static int      control_round_config = 0; // control rounding and related (daz/ftz)
static uint32_t orig_round_config;        // captured at start
static uint32_t our_round_config = 0;     // as we want to run 

volatile static enum {AGGREGATE,INDIVIDUAL} mode = AGGREGATE;
volatile static int aggressive = 0;
volatile static int disable_pthreads = 0;

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


/*
static inline int gettid()
{
  return syscall(SYS_gettid);
}
*/

typedef struct rand_state {
    uint64_t xi;
} rand_state_t;

typedef struct sampler_state {
    enum {OFF=0, ON}   state;
    int              delayed_processing;
    rand_state_t     rand;
    uint64_t         on_mean_us;
    uint64_t         off_mean_us;
    struct itimerval it;
} sampler_state_t;

// This is to allow us to handle multiple threads 
// and to follow forks later
typedef struct monitoring_context {
  uint64_t start_time; // cycles when context created
  enum {INIT, AWAIT_FPE, AWAIT_TRAP, ABORT} state;
  int aborting_in_trap;
  int tid;
  int fd; 
  uint64_t count;
  uint64_t trap_state;       // for use by the architectural trap mechanism
  sampler_state_t sampler;   // used only when sampling is on
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

// built in random number generator to avoid changing the state
// of the application's random number generator
//
// This is borrowed from NK and should probably be replaced
//
static void seed_rand(sampler_state_t *s, uint64_t seed)
{
  s->rand.xi = seed;
}

// linear congruent, full 64 bit space
static inline uint64_t _pump_rand(uint64_t xi, uint64_t a, uint64_t c)
{
  uint64_t xi_new = (a*xi + c);
  
  return xi_new;
}    

static inline uint64_t pump_rand(sampler_state_t *s)
{
  s->rand.xi = _pump_rand(s->rand.xi, 0x5deece66dULL, 0xbULL);
  
  return s->rand.xi;
}

static inline uint64_t get_rand(sampler_state_t *s)
{
  return pump_rand(s);
}


// we assume here that the FP state is saved and restored
// by the handler wrapper code, otherwise this will damage things badly
// this is of course true for Linux user, but not necessarily NK kernel
// period in us, return in us
// we also need to be sure that we don't cause an exception ourselves
static uint64_t next_exp(sampler_state_t *s, uint64_t mean_us)
{
  arch_fp_csr_t oldfpcsr; 
  uint64_t ret = 0;

  arch_config_machine_fp_csr_for_local(&oldfpcsr);

  // now we are safe to do FP that might itself change flags
  
  uint64_t r = get_rand(s);
  double u;
  r = r & -2ULL; // make sure that we are not at max
  
  
  u = ((double) r) / ((double) (-1ULL));
  
  // u = [0,1)
  
  u = -log(1.0 - u) * ((double)mean_us);
  
  // now shape u back into a uint64_t
  
  if (u > ((double)(-1ULL))) {
    ret = -1ULL;
  } else {
    ret = (uint64_t)u;
  }

  // restore state
  arch_set_machine_fp_csr(&oldfpcsr);
  
  // no more FP after this
  
  return ret;
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
  if (arch_have_special_fp_csr_exception(FE_DENORM)) {
    if (have) {
      strcat(buf," ");
    }
    strcat(buf, "FE_DENORM");
    have=1;
  }
  
  if (!have) {
    strcpy(buf,"NO_EXCEPTIONS_RECORDED");
  }
}

static __attribute__((unused))  void show_current_fe_exceptions()
{
  char buf[80];
  stringify_current_fe_exceptions(buf);
  INFO("%s\n", buf);
}

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


static __attribute__((constructor)) void fpspy_init(void);


static void abort_operation(char *reason)
{
  if (!inited) {
    DEBUG("Initializing before aborting\n");
    fpspy_init();
    DEBUG("Done with fpspy_init()\n");
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

	r.time = arch_cycle_count() - mc->start_time;
	
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

static int teardown_monitoring_context(int tid);


__attribute__((noreturn)) void pthread_exit(void *ret)  
{
  DEBUG("pthread_exit(%p)\n",ret);

  // we will process this even if we have aborted, since
  // we want to flush aggregate info even if it's just an abort record
  if (mode==INDIVIDUAL) {
    teardown_monitoring_context(gettid());
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
    
  if (disable_pthreads==0){
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


// is it really the case we cannot meaningfully manipulate ucontext
// here to change the FP engine?  Really?   Why would this work in
// both SIGFPE and SIGTRAP but not here?
static void update_sampler(monitoring_context_t *mc, ucontext_t *uc)
{
    sampler_state_t *s = &mc->sampler;

    //arch_dump_gp_csr(stderr,"update before",uc);
    //arch_dump_fp_csr(stderr,"update before",uc);

    // we are guaranteed to be in AWAIT_FPE state at this
    // point.
    //
    // ON->OFF : clear fpe, mask fpe, turn off traps
    // OFF->ON : clear fpe, unmask fpe, turn off traps
    //
    // traps should already be off, but why not be sure

    if (s->state==ON) { 
	DEBUG("Switching from on to off\n");
	arch_clear_fp_exceptions(uc);        // Clear fpe 
	arch_mask_fp_traps(uc);              // Mask fpe
	arch_reset_trap(uc,&mc->trap_state); // disable traps
    } else {
	DEBUG("Switching from off to on\n");
	arch_clear_fp_exceptions(uc);        // Clear fpe
	arch_unmask_fp_traps(uc);            //Unmask fpe
	arch_reset_trap(uc,&mc->trap_state); // disable traps
    }

    // schedule next wakeup

    uint64_t n = next_exp(s,s->state==ON ? s->off_mean_us : s->on_mean_us);

    if (!n) {
      // make sure we do actually wake up again
      // n = 0 would disable timer...
      n = 1;
    }

  if (s->state==OFF && n>MAX_US_ON) { 
        // about to turn on for too long, limit:
        n=MAX_US_ON;
    }

    if (s->state==ON && n>MAX_US_OFF) { 
        // about to turn off for too long, limit:
        n=MAX_US_OFF;
    }
    
    s->it.it_interval.tv_sec = 0;
    s->it.it_interval.tv_usec = 0;
    s->it.it_value.tv_sec = n / 1000000;
    s->it.it_value.tv_usec = n % 1000000;

    // flip state
    s->state = s->state==ON ? OFF : ON ;

    // don't reprocess again in case we are running delayed because
    // we were not intially in an AWAIT_FPE
    if (s->delayed_processing) {
	DEBUG("Completed delayed processing\n");
	s->delayed_processing = 0;
    }

    if (setitimer(timer_type, &s->it, NULL)) {
	ERROR("Failed to set timer?!\n");
    }

    //arch_dump_gp_csr(stderr,"update after",uc);
    //arch_dump_fp_csr(stderr,"update after",uc);
    
    DEBUG("Timer reinitialized for %lu us state %s\n",n,s->state==ON ? "ON" : "off");
}

    

static void sigtrap_handler(int sig, siginfo_t *si, void *priv)
{
  monitoring_context_t *mc = find_monitoring_context(gettid());
  ucontext_t *uc = (ucontext_t *)priv;
  
  DEBUG("TRAP signo 0x%x errno 0x%x code 0x%x rip %p\n",
        si->si_signo, si->si_errno, si->si_code, si->si_addr);


  if (!mc || mc->state==ABORT) { 
    arch_clear_fp_exceptions(uc);     // exceptions cleared
    arch_mask_fp_traps(uc);           // exceptions masked
    if (control_round_config) {
      arch_set_round_config(uc,orig_round_config);
    }
    // PAD - BAD use of trap_state here
    arch_reset_trap(uc,&mc->trap_state);  // trap off
    if (!mc) {
      // this may end badly
      abort_operation("Cannot find monitoring context during sigtrap_handler exec");
    } else {
      DEBUG("FP and TRAP mcontext restored on abort\n");
    }
    return;
  }

  if (mc && mc->state==INIT) {
    orig_round_config = arch_get_round_config(uc);
    arch_clear_fp_exceptions(uc);
    arch_unmask_fp_traps(uc);    
    if (control_round_config) {
      arch_set_round_config(uc,our_round_config);
    }
    arch_reset_trap(uc,&mc->trap_state);      // trap disabled
    mc->state = AWAIT_FPE;
    DEBUG("MXCSR state initialized\n");
    return;
  }
  
  if (mc->state == AWAIT_TRAP) { 
    mc->count++;
    arch_clear_fp_exceptions(uc);         
    if (maxcount!=-1 && mc->count >= maxcount) { 
      // disable further operation since we've recorded enough
      arch_mask_fp_traps(uc);             
      if (control_round_config) {
	arch_set_round_config(uc,orig_round_config);
      }
    } else {
      arch_unmask_fp_traps(uc);
      if (control_round_config) {
	arch_set_round_config(uc,our_round_config);
      }
    }
    arch_reset_trap(uc,&mc->trap_state);
    mc->state = AWAIT_FPE;
    if (mc->sampler.delayed_processing) {
	DEBUG("Delayed sampler handling\n");
	update_sampler(mc,uc);
    }
  } else {
    arch_clear_fp_exceptions(uc);
    arch_mask_fp_traps(uc);
    if (control_round_config) {
      arch_set_round_config(uc,orig_round_config);
    }
    arch_reset_trap(uc,&mc->trap_state);
    mc->aborting_in_trap = 1;
    abort_operation("Surprise state during sigtrap_handler exec");
  }

  DEBUG("TRAP done\n");
}



static void sigfpe_handler(int sig, siginfo_t *si,  void *priv)
{
  monitoring_context_t *mc = find_monitoring_context(gettid());
  ucontext_t *uc = (ucontext_t *)priv;

  DEBUG("FPE signo 0x%x errno 0x%x code 0x%x rip %p \n",
        si->si_signo, si->si_errno, si->si_code, si->si_addr);
  DEBUG("FPE RIP=%p RSP=%p\n",
        (void*) uc->uc_mcontext.gregs[REG_RIP], (void*)  uc->uc_mcontext.gregs[REG_RSP]);
    

  if (!mc) {
    arch_clear_fp_exceptions(uc);
    arch_mask_fp_traps(uc);
    if (control_round_config) {
      arch_set_round_config(uc,orig_round_config);
    }
    arch_reset_trap(uc,&mc->trap_state);
    abort_operation("Cannot find monitoring context during sigfpe_handler exec");
    return;
  }

  if (!(mc->count % sample_period)) { 
    individual_trace_record_t r;  

    r.time = arch_cycle_count() - mc->start_time;
    r.rip = (void*) arch_get_ip(uc);
    r.rsp = (void*) arch_get_sp(uc);
    r.code =  si->si_code;
    r.mxcsr =  arch_get_fp_csr(uc);
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
      
  if (mc->state == AWAIT_FPE) {
    arch_clear_fp_exceptions(uc);
    arch_mask_fp_traps(uc);
    if (control_round_config) {
      arch_set_round_config(uc,our_round_config);
    }
    arch_set_trap(uc,&mc->trap_state);
    mc->state = AWAIT_TRAP;
  } else {
    arch_clear_fp_exceptions(uc);
    arch_mask_fp_traps(uc);
    if (control_round_config) {
      arch_set_round_config(uc,orig_round_config);
    }
    arch_reset_trap(uc,0);
    abort_operation("Surprise state during sigfpe_handler exec");
  }
  DEBUG("FPE done\n");
}

static __attribute__((destructor)) void fpspy_deinit(void);


static void sigint_handler(int sig, siginfo_t *si,  void *priv)
{

  DEBUG("Handling break\n");


  if (oldsa_int.sa_sigaction) { 
    fpspy_deinit(); // dump everything out
    // invoke underlying handler
    oldsa_int.sa_sigaction(sig,si,priv);
  } else {
    // exit - our deinit will be called
    exit(-1);
  }
}

    

static void sigalrm_handler(int sig, siginfo_t *si,  void *priv)
{
  monitoring_context_t *mc = find_monitoring_context(gettid());
  ucontext_t *uc = (ucontext_t *)priv;
  
  DEBUG("Timeout for %d\n", gettid());
  
  if (!mc) {
    ERROR("Could not find monitoring context for %d\n",gettid());
    return;
  }
  if (mc->state != AWAIT_FPE) {
    // we are in the middle of handling an instruction, so we will
    // defer the transition until after this is done
    DEBUG("Delaying sampler processing because we are in the middle of an instruction\n");
    mc->sampler.delayed_processing = 1;
    return ;
  } else {
    update_sampler(mc,uc);
  }
}



void init_random(sampler_state_t *s)
{
  // randomization
  if (random_seed!=-1) {
    seed_rand(s,random_seed);
  } else {
    seed_rand(s,arch_cycle_count());
  }
}


void init_sampler(sampler_state_t *s)
{
  DEBUG("Init sampler (%p)\n",s);
  
  init_random(s);
  
  s->on_mean_us = on_mean_us;
  s->off_mean_us = off_mean_us;
  
  s->state = ON;
  
  if (!timers) {
    DEBUG("Sampler without timing\n");
    return;
  }
  
  uint64_t n = next_exp(s,s->on_mean_us);
  
  s->it.it_interval.tv_sec = 0;
  s->it.it_interval.tv_usec = 0;
  s->it.it_value.tv_sec = n / 1000000;
  s->it.it_value.tv_usec = n % 1000000;
  
  if (setitimer(timer_type, &(s->it), NULL)) {
    ERROR("Failed to set timer?!\n");
  }
  
  DEBUG("Timer initialized for %lu us\n",n);
  
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

  c->start_time = arch_cycle_count();
  c->state = INIT;
  c->aborting_in_trap = 0;
  c->count = 0;
  c->trap_state = 0;

  init_sampler(&c->sampler);
  
  return 0;
}


static int teardown_monitoring_context(int tid)
{
  monitoring_context_t *mc = find_monitoring_context(tid);

  if (!mc) {
    ERROR("Cannot find monitoring context for %d\n",tid);
    return -1;
  }

  // add later - not relevant now PAD
  // deinit_sampler(&mc->sampler);
 
  close(mc->fd);
  free_monitoring_context(tid);

  DEBUG("Tore down monitoring context for %d\n",tid);

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
    
    int alarm_sig =
      timer_type==ITIMER_REAL ? SIGALRM : 
      timer_type==ITIMER_VIRTUAL ? SIGVTALRM :
      timer_type==ITIMER_PROF ? SIGPROF : SIGALRM;
    
    memset(&sa,0,sizeof(sa));
    sa.sa_sigaction = sigfpe_handler;
    sa.sa_flags |= SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGINT);
    sigaddset(&sa.sa_mask, SIGTRAP);
    if (timers) {sigaddset(&sa.sa_mask, alarm_sig);}
    
    ORIG_IF_CAN(sigaction,SIGFPE,&sa,&oldsa_fpe);
    
    memset(&sa,0,sizeof(sa));
    sa.sa_sigaction = sigtrap_handler;
    sa.sa_flags |= SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGINT);
    sigaddset(&sa.sa_mask, SIGTRAP);
    if (timers) { sigaddset(&sa.sa_mask, alarm_sig); }
    sigaddset(&sa.sa_mask, SIGFPE); 
    ORIG_IF_CAN(sigaction,SIGTRAP,&sa,&oldsa_trap);
    
    memset(&sa,0,sizeof(sa));
    sa.sa_sigaction = sigint_handler;
    sa.sa_flags |= SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGTRAP);
    if (timers) { sigaddset(&sa.sa_mask, alarm_sig);}
    
    ORIG_IF_CAN(sigaction,SIGINT,&sa,&oldsa_int);
    
    if (timers) {
      // only initialize timing if we need it
      DEBUG("Setting up timer interrupt handler\n");
      memset(&sa, 0,sizeof(sa));
      sa.sa_sigaction = sigalrm_handler;
      sa.sa_flags |= SA_SIGINFO;
      sigemptyset(&sa.sa_mask);
      sigaddset(&sa.sa_mask, SIGINT);
      ORIG_IF_CAN(sigaction,
		  alarm_sig,
		  &sa,&oldsa_alrm);
    }
    
    ORIG_IF_CAN(feenableexcept,exceptmask);
    
    // now kick ourselves to set the sse bits; we are currently in state INIT
    
    kill(getpid(),SIGTRAP);
  }
  
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
  arch_clear_except_mask();
  
  if (strcasestr(buf,"inv")) {
    DEBUG("tracking INVALID\n");
    exceptmask |= FE_INVALID ;
    arch_set_except_mask(FE_INVALID);
  }
  if (strcasestr(buf,"den")) {
    DEBUG("tracking DENORM\n");
    exceptmask |= 0 ; // not provided in standard interface, catch via arch-specific...
    arch_set_except_mask(FE_DENORM);
  }
  if (strcasestr(buf,"div")) {
    DEBUG("tracking DIVIDE_BY_ZERO\n");
    exceptmask |= FE_DIVBYZERO ;
    arch_set_except_mask(FE_DIVBYZERO);
  }
  if (strcasestr(buf,"over")) {
    DEBUG("tracking OVERFLOW\n");
    exceptmask |= FE_OVERFLOW;
    arch_set_except_mask(FE_OVERFLOW);
  }
  if (strcasestr(buf,"under")) {
    DEBUG("tracking UNDERFLOW\n");
    exceptmask |= FE_UNDERFLOW ;
    arch_set_except_mask(FE_UNDERFLOW);
  }
  if (strcasestr(buf,"prec")) {
    DEBUG("tracking PRECISION\n");
    exceptmask |= FE_INEXACT ;
    arch_set_except_mask(FE_INEXACT);
  }

}

static void config_round_daz_ftz(char *buf)
{
  orig_round_config = arch_get_machine_round_config();

  our_round_config = 0;
  
  if (strcasestr(buf,"pos")) {
    arch_set_round_mode(&our_round_config,FPSPY_ROUND_POSITIVE);
  } else if (strcasestr(buf,"neg")) {
    arch_set_round_mode(&our_round_config,FPSPY_ROUND_NEGATIVE);
  } else if (strcasestr(buf,"zer")) {
    arch_set_round_mode(&our_round_config,FPSPY_ROUND_ZERO);
  } else if (strcasestr(buf,"nea")) {
    arch_set_round_mode(&our_round_config,FPSPY_ROUND_NEAREST);
  } else {
    ERROR("Unknown rounding mode - avoiding rounding control\n");
    control_round_config = 0;
    return;
  }

  int which=0;
  if (strcasestr(buf,"daz")) {
    which+=2;
  }
  if (strcasestr(buf,"ftz")) {
    which+=1;
  }
  arch_set_dazftz_mode(&our_round_config,which);

  control_round_config = 1;

  DEBUG("Configuring rounding control to 0x%08x\n", our_round_config);

}
    


// Called on load of preload library
static __attribute__((constructor)) void fpspy_init(void) 
{

  INFO("init\n");
  if (!inited) { 
    if (getenv("FPSPY_MODE")) {
      if (!strcasecmp(getenv("FPSPY_MODE"),"individual")) { 
	mode=INDIVIDUAL;
	DEBUG("Setting INDIVIDUAL mode\n");
      } else {
	if (!strcasecmp(getenv("FPSPY_MODE"),"aggregate")) {
	  mode=AGGREGATE;
	  DEBUG("Setting AGGREGATE mode\n");
	} else {
	  ERROR("FPSPY_MODE is given, but mode %s does not make sense\n",getenv("FPSPY_MODE"));
	  abort();
	}
      } 
    } else {
      mode=AGGREGATE;
      DEBUG("No FPSPY_MODE is given, so assuming AGGREGATE mode\n");
    }
    if (getenv("FPSPY_MAXCOUNT")) { 
      maxcount = atoi(getenv("FPSPY_MAXCOUNT"));
    }
    if (getenv("FPSPY_AGGRESSIVE") && tolower(getenv("FPSPY_AGGRESSIVE")[0])=='y') {
      DEBUG("Setting AGGRESSIVE\n");
      aggressive=1;
    }
    if ((getenv("FPSPY_DISABLE_PTHREADS") && tolower(getenv("FPSPY_DISABLE_PTHREADS")[0])=='y') || 
	(getenv("DISABLE_PTHREADS") && tolower(getenv("DISABLE_PTHREADS")[0])=='y') ) {
      disable_pthreads=1;
    }
    if (getenv("FPSPY_SAMPLE")) {
      sample_period = atoi(getenv("FPSPY_SAMPLE"));
      DEBUG("Setting sample period to %d\n", sample_period);
    }
    if (getenv("FPSPY_POISSON")) {
	if (sscanf(getenv("FPSPY_POISSON"),"%lu:%lu",&on_mean_us,&off_mean_us)!=2) {
	    ERROR("unsupported FPSPY_POISSON arguments\n");
	    return;
	} else {
	    DEBUG("Setting Poisson sampling %lu us off %lu us on\n",on_mean_us, off_mean_us);
	    timers = 1;
	}
    }
    if (getenv("FPSPY_TIMER")) {
	if (!strcasecmp(getenv("FPSPY_TIMER"),"virtual")) {
	    timer_type = ITIMER_VIRTUAL;
	    DEBUG("Using virtual timer\n");
	} else if (!strcasecmp(getenv("FPSPY_TIMER"),"real")) {
	    timer_type = ITIMER_REAL;
	    DEBUG("Using real timer\n");
	} else if (!strcasecmp(getenv("FPSPY_TIMER"),"prof")) {
	    timer_type = ITIMER_PROF;
	    DEBUG("Using profiling timer\n");
	} else {
	    ERROR("Unknown FPSPY_TIMER=%s type\n",getenv("FPSPY_TIMER"));
	    return;
	}
    }
    if (getenv("FPSPY_SEED")) {
	random_seed = atol(getenv("FPSPY_SEED"));
    } else {
	random_seed = -1; // random selection at mc start
    }
    if (getenv("FPSPY_EXCEPT_LIST")) {
      config_exceptions(getenv("FPSPY_EXCEPT_LIST"));
    }
    if (getenv("FPSPY_FORCE_ROUNDING")) {
      config_round_daz_ftz(getenv("FPSPY_FORCE_ROUNDING"));
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
static __attribute__((destructor)) void fpspy_deinit(void) 
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

