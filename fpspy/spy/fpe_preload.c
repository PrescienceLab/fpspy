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

#define INFO(S, ...) fprintf(stderr,  "fpe_preload: info: " S, ##__VA_ARGS__)
#define ERROR(S, ...) fprintf(stderr, "fpe_preload: ERROR: " S, ##__VA_ARGS__)
#define DEBUG(S, ...) fprintf(stderr, "fpe_preload: debug: " S, ##__VA_ARGS__)

//#define INFO(S, ...) 
//#define ERROR(S, ...)
//#define DEBUG(S, ...) 

volatile static int inited=0;
volatile static int aborted=0; // set if the target is doing its own FPE processing

enum {AGGREGATE,INDIVIDUAL} mode = AGGREGATE;

typedef struct syscall {
  uint64_t number;
  uint64_t arg[6];
  uint64_t rc;
} syscall_t;

#define STACK_SIZE (4096*16)
static int  monitor_pid=-1;
static int  target_pid=-1; 
static char monitor_stack[2*STACK_SIZE];
static syscall_t cur_syscall;


#define PEEK(d,pid,s)  \
  (d) = ptrace(PTRACE_PEEKUSER,(pid),8*(s),0);	\
  if (errno) { perror("peek " #s); return -1; } 

#define POKE(d,pid,s)  \
  if (ptrace(PTRACE_POKEUSER,(pid),8*(d),&s)<0) { perror("poke " #s); return -1; } 


static int peek_regs(int pid, struct user_regs_struct *regs)
{
  if (ptrace(PTRACE_GETREGS, pid, 0, regs)<0) { 
    return -1;
  } else {
    return 0;
  }
}

static int poke_regs(int pid, struct user_regs_struct *regs)
{
  if (ptrace(PTRACE_SETREGS, pid, 0, regs)<0) { 
    return -1;
  } else {
    return 0;
  }
}

// Fetch request
static int extract_syscall_request(syscall_t *s)
{
  struct user_regs_struct regs;
  if (peek_regs(target_pid,&regs)) { 
    ERROR("can't get registers\n");
    return -1;
  }
  s->number  = regs.orig_rax;
  s->arg[0] = regs.rdi;
  s->arg[1] = regs.rsi;
  s->arg[2] = regs.rdx;
  s->arg[3] = regs.r10;
  s->arg[4] = regs.r8;
  s->arg[5] = regs.r9;

  return 0;
}

// Fetch response
static int extract_syscall_response(syscall_t *s)
{
  struct user_regs_struct regs;
  if (peek_regs(target_pid,&regs)) { 
    ERROR("can't get registers\n");
    return -1;
  }
  s->rc = regs.rax;
  return 0;
}

// Update a request
static int update_syscall_request(syscall_t *s)
{
  struct user_regs_struct regs;

  if (peek_regs(target_pid,&regs)) { 
    ERROR("can't get registers\n");
    return -1;
  }

  regs.orig_rax = s->number;
  regs.rdi = s->arg[0];
  regs.rsi = s->arg[1];
  regs.rdx = s->arg[2];
  regs.r10 = s->arg[3];
  regs.r8 =  s->arg[4];
  regs.r9 =  s->arg[5];

  if (poke_regs(target_pid,&regs)) { 
    ERROR("can't set registers\n");
    return -1;
  }

  return 0;
}


static int update_syscall_response(syscall_t *s)
{
  struct user_regs_struct regs;

  if (peek_regs(target_pid,&regs)) { 
    ERROR("can't get registers\n");
    return -1;
  }

  regs.rax = s->rc;

  if (poke_regs(target_pid,&regs)) { 
    ERROR("can't set registers\n");
    return -1;
  }

  return 0;
}



static int print_syscall(syscall_t *s) 
{
  INFO("syscall number=0x%lx\n", s->number);
  INFO("syscall arg[0]=0x%lx\n", s->arg[0]);
  INFO("syscall arg[1]=0x%lx\n", s->arg[1]);
  INFO("syscall arg[2]=0x%lx\n", s->arg[2]);
  INFO("syscall arg[3]=0x%lx\n", s->arg[3]);
  INFO("syscall arg[4]=0x%lx\n", s->arg[4]);
  INFO("syscall arg[5]=0x%lx\n", s->arg[5]);
  INFO("syscall     rc=0x%lx\n", s->rc);
  
  return 0;
}

static uint64_t last_syscall_num;
static uint64_t last_len;

/*
  Filter and modify a system call on entry
*/
static int cook_entry()
{
  INFO("entry\n");
  if (extract_syscall_request(&cur_syscall)) {
    ERROR("cannot extract syscall request");
    return -1;
  }
  print_syscall(&cur_syscall);

#if 0
  last_syscall_num=-1;
  last_len=0;
  return 0;
#endif

  // here we would modify it an then
  // invoke update_syscall_request
  // this is a hacky example: convert any write system call to a getpid
  if (cur_syscall.number == 1) { 
    INFO("Replacing write(%lu,%p (%s), %lu) with getpid()\n",
	 cur_syscall.arg[0], 
	 (char*)cur_syscall.arg[1], 
	 (char*)cur_syscall.arg[1], 
	 cur_syscall.arg[2]);


    cur_syscall.number = 39; // getpid
    
    if (update_syscall_request(&cur_syscall)) { 
      ERROR("cannot update system call request\n");
      return -1;
    }

    // stash away the relevant stuff for the entry
    last_len = cur_syscall.arg[2];
    last_syscall_num = 1;
  } else {
    last_syscall_num = -1;
    last_len =0 ;
  }
  
  return 0;
}

static int cook_exit()
{
  INFO("exit\n");
  if (extract_syscall_response(&cur_syscall)) { 
    ERROR("cannot extract syscall response");
    return -1;
  }
  print_syscall(&cur_syscall);
    
  // here we would modify the response
  // and then invoke update_syscall_response;
  // example - replace write with getpid() - now return expected write length
  if (last_syscall_num == 1) { 
    INFO("rc=%ld - pretending to complete write() call with rc=%lu\n",cur_syscall.rc,last_len);

    cur_syscall.rc = last_len;

    if (update_syscall_response(&cur_syscall)) { 
      ERROR("cannot update system call request\n");
      return -1;
    }

    last_syscall_num = -1;
    last_len = 0;
  }
  
  return 0;
}




/*
  Note that monitor needs to be conservative
  in the use of libc, etc, since we cannot assume we are linked
  with the reentrant/thread-safe version
*/
static int monitor(void *x)
{
  int status;
  enum {WAIT_FOR_ENTRY, WAIT_FOR_EXIT} state;

  INFO("monitor pid=%d ppid=%d\n",getpid(),getppid());
  INFO("monitor (pid=%d) running with target pid %d\n",monitor_pid,target_pid);
  
  if (ptrace(PTRACE_ATTACH,
	     target_pid,
	     0, 0)<0) {
    perror("ptrace-attach");
    ERROR("cannot attach\n");
    return -1;
  }

  // wait for target to catch up to us and pause
  waitpid(target_pid,0,0);
  
  if (ptrace(PTRACE_SETOPTIONS,
	     target_pid,
	     0,
	     PTRACE_O_TRACESYSGOOD)<0) { 
    perror("ptrace-set-options");	       
    ERROR("cannot set options\n");
    return -1;
  }

  INFO("attached\n");

  // target is now paused

  inited=1;

  state = WAIT_FOR_ENTRY;

  while (1) { 
    // continue target to next syscall entry or exit or other event
    if (ptrace(PTRACE_SYSCALL,
	       target_pid,
	       0, 0)<0) { 
      perror("ptrace-entry");
      ERROR("cannot trace system call entry\n");
      return -1;
    }

    // find out what event happened
    waitpid(target_pid,&status,0);

    // check explicitly for an exit, since this must not happen
    if (WIFEXITED(status)) { 
      INFO("target exited with rc=%d\n",WEXITSTATUS(status));
      // target is dead - uh cannot happen...
      ERROR("target dead!\n");
      return -1;
    }

    // otherwise, we care only about system calls
    if (!(WIFSTOPPED(status) && (WSTOPSIG(status)&0x80))) {
      INFO("skipping non-syscall\n");
      // target stopped for something other than a syscall, so just continue it;
      continue;
    } 

    // now we are in a system call entry or exit

    if (state==WAIT_FOR_ENTRY) { 
      cook_entry();
      state=WAIT_FOR_EXIT;
    } else if (state==WAIT_FOR_EXIT) {
      cook_exit();
      state=WAIT_FOR_ENTRY;
    } else {
      ERROR("impossible state!\n");
    }

  }

  INFO("monitor exit\n");
  inited=0;
  return 0;
}

/*
static int bringup()
{
  // clone a thread with a distinct pid that will ptrace the parent
  // and its children

  // We will trace the parent
  target_pid = getpid();

  monitor_pid = clone(monitor,
		      monitor_stack+STACK_SIZE,
		      CLONE_FILES | CLONE_FS | CLONE_IO | CLONE_VM | CLONE_SIGHAND | SIGCHLD,
		      (void*)(long)target_pid,
		      0,0,0);

  if (monitor_pid<0) { 
    perror("clone");
    ERROR("Unable to clone monitor\n");
    return -1;
  }

  INFO("monitor (pid=%d) launched by pid %d\n", monitor_pid, target_pid);
  return 0;

}
*/

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

static void show_current_fe_exceptions()
{
  char buf[80];
  stringify_current_fe_exceptions(buf);
  INFO("%s\n", buf);
}

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

static void nop_it(char *ptr, int num)
{
  for (; num>0; ptr++, num--) {
    *ptr=0x90;
  }
}

inline void flip_trap_flag()
{
  __asm__ __volatile__ ("pushfq; "
			"xorq $0x100, (%%rsp); "
			"popfq; "
			: : : "memory"); 
}

inline void set_trap_flag_context(ucontext_t *uc, int val)
{
  if (val) {
    uc->uc_mcontext.gregs[REG_EFL] |= 0x100UL; 
  } else {
    uc->uc_mcontext.gregs[REG_EFL] &= ~0x100UL; 
  }
}

static void sigtrap_handler(int sig, siginfo_t *si, void *priv)
{
  DEBUG("TRAP signo 0x%x errno 0x%x code 0x%x rip %p\n",
	si->si_signo, si->si_errno, si->si_code, si->si_addr);
}

static void sigfpe_handler(int sig, siginfo_t *si,  void *priv)
{
  ucontext_t *uc = (ucontext_t *)priv;
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

  DEBUG("FPE signo 0x%x errno 0x%x code 0x%x rip %p %s\n",
	si->si_signo, si->si_errno, si->si_code, si->si_addr,buf);
    
  set_trap_flag_context(uc,1);

  //orig_fedisableexcept(FE_ALL_EXCEPT);
  //xs  nop_it(si->si_addr,4);
    //  abort();
}

static int bringup()
{
  if (setup_shims()) { 
    ERROR("Cannot setup shims\n");
    return -1;
  }
  orig_feclearexcept(FE_ALL_EXCEPT);
  if (mode==INDIVIDUAL) {
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
  INFO("deinit\n");
  if (inited && !aborted) { 
    if (mode==AGGREGATE) {
      INFO("FP exceptions seen during run are:\n");
      show_current_fe_exceptions();
    } else {
      INFO("Dumping FPE exceptions to file (NOT DONE YET)\n");
    }
  }
  inited=0;
  INFO("done\n");
}
