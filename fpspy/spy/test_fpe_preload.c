/*

  Test code for floating point exception interception

  Copyright (c) 2017 Peter A. Dinda

*/

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#define __GNU_SOURCE
#include <fenv.h>

#define NUM_THREADS 1

// does show up in unistd for some reason...
int execvpe(char *, char **, char **); 

double foo(double x)
{
  return sin(x);
} 

void use(double x);


void divzero()
{
  volatile double x,y,z;
  
  x=99.0;
  y=0.0;
  printf("Doing divide by zero\n");
  z = x/y;
  use(z);
  //printf("%.18le / %.18le = %.18le\n", x,y,z);
}

void nanny()
{
  volatile double x,y,z;
  
  x=0.0;
  y=0.0;
  printf("Doing NAN\n");
  z = x/y;
  use(z);
  //printf("%.18le / %.18le = %.18le\n", x,y,z);
}


void denorm()
{
  volatile double x,y,z;
  unsigned long val;
  // smallest normal number
  // 0 00000000001 00000000000....1
  // all zero except for bit 52 and bit 0 
  val =  0x0010000000000001ULL;
  x=*(double*)&val;
  y=4.0;
  printf("Doing denorm\n");
  z = x/y;
  use(z);
  //printf("%.18le / %.18le = %.18le\n", x,y,z);
}

void underflow()
{
  volatile double x,y,z;
  unsigned long val;
  // smallest denormal number
  // 0 00000000000 00000000000....1
  // all zero except forand bit 0 
  val =  0x0000000000000001ULL;
  x=*(double*)&val;
  y=4.0;
  printf("Doing underflow\n");
  z = x/y;
  use(z);
  //printf("%.18le / %.18le = %.18le\n", x,y,z);
}

void overflow()
{
  volatile double x,y,z;
  unsigned long val;
  // largest normal number
  // 0 1111111110 11111111...
  // all 1s except for bit 52 
  val =  0x7fefffffffffffffULL;
  x=*(double*)&val;
  y=4.0;
  printf("Doing overflow\n");
  z = x*y;
  use(z);
  //  printf("%.18le * %.18le = %.18le\n", x,y,z);
}  

void inexact()
{
  volatile double x,y,z;
  unsigned long val;
  // largest normal number
  // 0 1111111110 11111111...
  // all 1s except for bit 52 
  val =  0x7fefffffffffffffULL;
  x=*(double*)&val;
  // normal number with smallest exponent, biggest mantissa
  // 0 00000000001 11111....
  // all 0s except for bit 52..0 
  val =  0x001fffffffffffffULL;
  y=*(double*)&val;
  printf("Doing inexact\n");
  z = x-y;
  use(z);
  //printf("%.18le - %.18le = %.18le\n", x,y,z);
}  


void use(double x)
{
}

void handler(int sig)
{
  printf("Caught my own signal %d and am exiting\n",sig);
  exit(0);
}

int do_work()
{
  divzero();
  nanny();
  denorm();

  // if we abort here, we should have some partial output in the logs
  
  if (getenv("TEST_FPE_BREAK_GENERAL_SIGNAL")) {
    signal(SIGUSR1,handler);
  }
  if (getenv("TEST_FPE_BREAK_FPE_SIGNAL")) {
    signal(SIGFPE,handler);
  }
  if (getenv("TEST_FPE_BREAK_FE_FUNC")) {
    feclearexcept(FE_ALL_EXCEPT);
  }

  underflow();
  overflow();
  inexact();

  return 0;
}



void *thread_start(void *tid)
{
  printf("Running tests in spawned thread %d\n",(int)(long)tid);
  do_work();
  return 0;
}

int main(int argc, char *argv[], char *envp[])
{
  int pid;
  pthread_t tid[NUM_THREADS];
  int rc;
  int am_child = argc>1 && !strcasecmp(argv[1],"child");
  int i;
  
  if (am_child) {
    printf("Forked/execed child running tests\n"); fflush(stdout);
    do_work();
    return 0;
  }

  printf("Hello from test_fpe_preload\n");
  printf("Running tests in normal mode\n");
  do_work();

  printf("Forking child to run tests\n");
  pid = fork();
  if (pid<0) {
    perror("fork failed");
    return -1;
  } else if (pid==0) {
    // child
    printf("Running tests in forked child\n"); fflush(stdout);
    do_work();
    return 0;
  } else { // pid>0 => parent
    do {
      if (waitpid(pid,&rc,0)<0) {
	perror("wait failed");
	return -1;
      }
    } while (!WIFEXITED(rc)); // we only care about signals it caught, just an exit
    printf("forked child done.\n");
  }

  printf("Forking/execing child to run tests\n"); fflush(stdout);
  pid = fork();
  if (pid<0) {
    perror("fork failed");
    return -1;
  } else if (pid==0) {
    // child
    char *argv_child[] = { argv[0], "child", 0 };
    execvpe(argv_child[0],argv_child,envp);  // pass environment to child
    perror("exec failed...");
    return -1;
  } else { // pid>0 => parent
    do {
      if (waitpid(pid,&rc,0)<0) {
	perror("wait failed");
	return -1;
      }
    } while (!WIFEXITED(rc)); // we only care about signals it caught, just an exit
    if (WEXITSTATUS(rc)) {
      printf("forked child failed (rc=%d)\n", WEXITSTATUS(rc));
      return -1;
    }
    printf("forked child with exec done.\n");
  }

  printf("Spawning %d threads to run tests\n", NUM_THREADS); fflush(stdout);
  for (i=0;i<NUM_THREADS;i++) {
    if (pthread_create(&tid[i],0,thread_start,(void*)(long)i)) {
      perror("thread creation failed");
      return -1;
    }
  }
  
  for (i=0;i<NUM_THREADS;i++) {
    pthread_join(tid[i],0);
    printf("Joined thread %d\n", i);
  }

  printf("Goodbye from test_fpe_preload\n");
  return 0;
}
  


