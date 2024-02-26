#include "config.h"

.code64
.section .text

#if CONFIG_TRAP_SHORT_CIRCUITING
.global _user_fpspy_entry
_user_fpspy_entry:
  pushf
  pushq 16(%rsp)  		
  pushq 16(%rsp) 		
  pushq %rcx
  pushq %rax
  pushq %rdx
  pushq %rbx
  pushq %rbp
  pushq %rsi
  pushq %rdi
  pushq %r15
  pushq %r14
  pushq %r13
  pushq %r12
  pushq %r11
  pushq %r10
  pushq %r9
  pushq %r8
  movq %rsp, %rdi

  call *fpspy_short_circuit_handler@GOTPCREL(%rip)
	
  popq %r8
  popq %r9
  popq %r10
  popq %r11
  popq %r12
  popq %r13
  popq %r14
  popq %r15
  popq %rdi
  popq %rsi
  popq %rbp
  popq %rbx
  popq %rdx
  popq %rax
  popq %rcx
  popq 24(%rsp)
  // You wanna see something nasty? >.<
  popq 24(%rsp)
  leaq -0x8(%rsp), %rsp
  popq 32(%rsp)
  leaq -0x8(%rsp), %rsp
  popq 40(%rsp)
  // You're welcome :P
  popf
  popq %rsp
  jmp *-152(%rsp)
#endif

