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
    approach of, on exception, inserting a breakpoint immediately after the faulting 
    instruction, rerunning the instruction with fpe handling off, then using the 
    breakpoint to reenable fpe exception delivery.  [This is a work in progress
    since we need enough of an emulator to determine the size of the faulting instruction]

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
#include <fcntl.h>

#define INFO(S, ...) fprintf(stderr,  "fpe_preload: info: " S, ##__VA_ARGS__)
#define ERROR(S, ...) fprintf(stderr, "fpe_preload: ERROR: " S, ##__VA_ARGS__)
#define DEBUG(S, ...) fprintf(stderr, "fpe_preload: debug: " S, ##__VA_ARGS__)

//#define INFO(S, ...) 
//#define ERROR(S, ...)
//#define DEBUG(S, ...) 

volatile static int inited=0;
volatile static int aborted=0; // set if the target is doing its own FPE processing
volatile static int maxcount=65546; // maximum number to record, per thread

volatile enum {AGGREGATE,INDIVIDUAL} mode = AGGREGATE;

sighandler_t (*orig_signal)(int sig, sighandler_t func) = 0;
int (*orig_sigaction)(int sig, const struct sigaction *act, struct sigaction *oldact) = 0;
int (*orig_feenableexcept)(int) = 0 ;
int (*orig_fedisableexcept)(int) = 0 ;
int (*orig_fegetexcept)() = 0 ;
int (*orig_feclearexcept)(int) = 0 ;
int (*orig_fegetexceptflag)(fexcept_t *flagp, int excepts) = 0 ;
int (*orig_feraiseexcept)(int excepts) = 0; 
int (*orig_fesetexceptflag)(const fexcept_t *flagp, int excepts) = 0;
int (*orig_fetestexcept)(int excepts) = 0;
int (*orig_fegetround)(void) = 0;
int (*orig_fesetround)(int rounding_mode) = 0;
int (*orig_fegetenv)(fenv_t *envp) = 0;
int (*orig_feholdexcept)(fenv_t *envp) = 0;
int (*orig_fesetenv)(const fenv_t *envp) = 0;
int (*orig_feupdateenv)(const fenv_t *envp) = 0;

static struct sigaction oldsa_fpe, oldsa_trap;

#define MAX_CONTEXTS 10

struct individual_record {
  void    *rip;
  void    *rsp;
  int      code;  // as in siginfo_t->si_code
  int      mxcsr; 
} __packed;

typedef struct individual_record individual_record_t;


// This is to allow us to handle multiple threads 
// and to follow forks later
typedef struct monitoring_context {
  enum {AWAIT_FPE, AWAIT_TRAP} state;
  int pid;
  int fd; 
  int count;
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


static monitoring_context_t *find_monitoring_context(int pid)
{
  int i;
  lock_contexts();
  for (i=0;i<MAX_CONTEXTS;i++) { 
    //DEBUG("Searching for %d, considering %d getpid()=%d\n",pid,context[i].pid,getpid());
    if (context[i].pid == pid) {
      unlock_contexts();
      return &context[i];
    }
  }
  unlock_contexts();
  return 0;
}

static monitoring_context_t *alloc_monitoring_context(int pid)
{
  int i;
  lock_contexts();
  for (i=0;i<MAX_CONTEXTS;i++) { 
    if (!context[i].pid) {
      context[i].pid = pid;
      unlock_contexts();
      return &context[i];
    }
  }
  unlock_contexts();
  return 0;
}

static void free_monitoring_context(int pid)
{
  int i;
  lock_contexts();
  for (i=0;i<MAX_CONTEXTS;i++) { 
    if (context[i].pid == pid) {
      context[i].pid = 0;
      unlock_contexts();
    }
  }
  unlock_contexts();
}



static void stringify_current_fe_exceptions(char *buf)
{
  buf[0]=0;

#define FE_HANDLE(x) if (orig_fetestexcept(x)) { strcat(buf," " #x ); }
  FE_HANDLE(FE_DIVBYZERO);
  FE_HANDLE(FE_INEXACT);
  FE_HANDLE(FE_INVALID);
  FE_HANDLE(FE_OVERFLOW);
  FE_HANDLE(FE_UNDERFLOW);
}

/*
static void show_current_fe_exceptions()
{
  char buf[80];
  stringify_current_fe_exceptions(buf);
  INFO("%s\n", buf);
}
*/

static void abort_operation(char *reason)
{
  if (!aborted) { 
    if (mode==INDIVIDUAL) { 
      sigaction(SIGFPE,&oldsa_fpe,0);
    }
    aborted = 1;
    DEBUG("Aborted operation because %s\n",reason);
  }
}

sighandler_t signal(int sig, sighandler_t func)
{
  if (sig==SIGFPE) { 
    abort_operation("target is using signal with SIGFPE");
  }
  return orig_signal(sig,func);
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact)
{
  if (sig==SIGFPE) { 
    abort_operation("target is using sigaction with SIGFPE");
  }
  return orig_sigaction(sig,act,oldact);
}


int feclearexcept(int excepts)
{
  abort_operation("target is using feclearexcept");
  return orig_feclearexcept(excepts);
}

int feenableexcept(int excepts)
{
  abort_operation("target is using feenableexcept");
  return orig_feenableexcept(excepts);
}

int fedisableexcept(int excepts)
{
  abort_operation("target is using fedisableexcept");
  return orig_fedisableexcept(excepts);
}

int fegetexcept(void)
{
  abort_operation("target is using fegetexcept");
  return orig_fegetexcept();
}

int fegetexceptflag(fexcept_t *flagp, int excepts)
{
  abort_operation("target is using fegetexceptflag");
  return orig_fegetexceptflag(flagp, excepts);
}

int feraiseexcept(int excepts) 
{
  abort_operation("target is using feraiseexcept");
  return orig_feraiseexcept(excepts);
}

int fesetexceptflag(const fexcept_t *flagp, int excepts)
{
  abort_operation("target is using fesetexceptflag");
  return orig_fesetexceptflag(flagp, excepts);
}

int fetestexcept(int excepts)
{
  abort_operation("target is using fetestexcept");
  return orig_fetestexcept(excepts);
}

int fegetround(void)
{
  abort_operation("target is using fegetround");
  return orig_fegetround();
}

int fesetround(int rounding_mode)
{
  abort_operation("target is using fesetround");
  return orig_fesetround(rounding_mode);
}

int fegetenv(fenv_t *envp)
{
  abort_operation("target is using fegetenv");
  return orig_fegetenv(envp);

}

int feholdexcept(fenv_t *envp)
{
  abort_operation("target is using feholdexcept");
  return orig_feholdexcept(envp);
}


int fesetenv(const fenv_t *envp)
{
  abort_operation("target is using fesetenv");
  return orig_fesetenv(envp);
}

int feupdateenv(const fenv_t *envp)
{
  abort_operation("target is using feupdateenv");
  return orig_feupdateenv(envp);
}

    
static int setup_shims()
{
  DEBUG("shim setup\n");

#define SHIMIFY(x) if (!(orig_##x = dlsym(RTLD_NEXT, #x))) { return -1; }
  if (mode==INDIVIDUAL) {
    SHIMIFY(signal);
    SHIMIFY(sigaction);
  }
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


inline void set_trap_flag_context(ucontext_t *uc, int val)
{
  if (val) {
    uc->uc_mcontext.gregs[REG_EFL] |= 0x100UL; 
  } else {
    uc->uc_mcontext.gregs[REG_EFL] &= ~0x100UL; 
  }
}


inline void clear_fp_exceptions_context(ucontext_t *uc)
{
  uc->uc_mcontext.fpregs->mxcsr &= ~0x3f; 
}

inline void set_mask_fp_exceptions_context(ucontext_t *uc, int mask)
{
  if (mask) {
    uc->uc_mcontext.fpregs->mxcsr |= 0x1f80;
  } else {
    uc->uc_mcontext.fpregs->mxcsr &= ~0x1f80;
  }
}

static void sigtrap_handler(int sig, siginfo_t *si, void *priv)
{
  monitoring_context_t *mc = find_monitoring_context(getpid());
  ucontext_t *uc = (ucontext_t *)priv;

  if (!mc) { 
    clear_fp_exceptions_context(uc);     // exceptions cleared
    set_mask_fp_exceptions_context(uc,1);// exceptions masked
    set_trap_flag_context(uc,0);         // traps disabled
    abort_operation("Cannot find monitoring context during sigtrap_handler exec");
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
    abort_operation("Surprise state during sigtrap_handler exec");
  }
}

static void sigfpe_handler(int sig, siginfo_t *si,  void *priv)
{
  monitoring_context_t *mc = find_monitoring_context(getpid());
  ucontext_t *uc = (ucontext_t *)priv;
  char buf[80];

  if (!mc) { 
    clear_fp_exceptions_context(uc);     // exceptions cleared
    set_mask_fp_exceptions_context(uc,1);// exceptions masked
    set_trap_flag_context(uc,0);         // traps disabled
    abort_operation("Cannot find monitoring context during sigfpe_handler exec");
    return;
  }

  individual_record_t r;
  
  r.rip = (void*) uc->uc_mcontext.gregs[REG_RIP];
  r.rsp = (void*) uc->uc_mcontext.gregs[REG_RSP];
  r.code =  si->si_code;
  r.mxcsr =  uc->uc_mcontext.fpregs->mxcsr;

  DEBUG("Writing record %d\n",mc->count);
  if (write(mc->fd,&r,sizeof(r))!=sizeof(r)) { 
    ERROR("Failed to write record\n");
  }

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

static int bringup_monitoring_context(int pid)
{
  monitoring_context_t *c;
  char name[80];

  if (!(c = alloc_monitoring_context(pid))) { 
    ERROR("Cannot allocate monitoring context\n");
    return -1;
  }

  sprintf(name,"__%s.%d.%lu.individual.fpemon", program_invocation_short_name, pid, time(0));
  if ((c->fd = open(name,O_CREAT | O_WRONLY, 0666))<0) { 
    ERROR("Cannot open monitoring output file\n");
    free_monitoring_context(pid);
    return -1;
  }

  DEBUG("fd = %d\n",c->fd);
  
  c->state = AWAIT_FPE;
  c->count = 0;

  return 0;
}

static int bringup()
{
  if (setup_shims()) { 
    ERROR("Cannot setup shims\n");
    return -1;
  }
  orig_feclearexcept(FE_ALL_EXCEPT);
  if (mode==INDIVIDUAL) {
    init_monitoring_contexts();

    if (bringup_monitoring_context(getpid())) { 
      ERROR("Failed to start up monitoring context at startup\n");
      return -1;
    }

    struct sigaction sa;

    memset(&sa,0,sizeof(sa));
    sa.sa_sigaction = sigfpe_handler;
    sa.sa_flags |= SA_SIGINFO;
    
    orig_sigaction(SIGFPE,&sa,&oldsa_fpe);

    memset(&sa,0,sizeof(sa));
    sa.sa_sigaction = sigtrap_handler;
    sa.sa_flags |= SA_SIGINFO;
    
    orig_sigaction(SIGTRAP,&sa,&oldsa_trap);


    orig_feenableexcept(FE_ALL_EXCEPT);

  }
  inited=1;
  return 0;
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

// Called on unload of preload library
static __attribute__((destructor)) void syscall_preload_deinit(void) 
{ 
  // destroy the tracer thread
  DEBUG("deinit\n");
  if (inited && !aborted) { 
    if (mode==AGGREGATE) {
      char buf[80];
      int fd;
      //DEBUG("FP exceptions seen during run are:\n");
      //show_current_fe_exceptions();
      sprintf(buf,"__%s.%d.%lu.aggregate.fpemon", program_invocation_short_name, getpid(), time(0));
      if ((fd = open(buf,O_CREAT | O_WRONLY, 0666)<0)) { 
	ERROR("Cannot open monitoring output file\n");
      } else {
	stringify_current_fe_exceptions(buf);
	strcat(buf,"\n");
	write(fd,buf,strlen(buf));
	close(fd);
      }
    } else {
      int i;
      DEBUG("FPE exceptions previously dumped to files\n");
      for (i=0;i<MAX_CONTEXTS;i++) { 
	if (context[i].pid) { 
	  close(context[i].fd);
	}
      }
    }
  }
  inited=0;
  DEBUG("done\n");
}
