#include "config.h"

.code64
.section .text

#if CONFIG_TRAP_SHORT_CIRCUITING
/*
  The kernel module drops us here with this on the stack:

  rsp + 8 rip of faulting instruction
  rsp + 0 rsp of faulting instruction   

  We will construct a partial gregset_t from this, for registers r8..rflags and then a temp:

  rsp + 136  rflags   ^
             rip      |
             rsp      |  like gregset
             rcx      |
             ...      |
  rsp + 0    r8       v  

  The user should modify the gregset_t to indicate where to go next
  We will make that happen

*/	
.global _user_fpspy_entry
_user_fpspy_entry:
	pushf                   // rflags of faulting instruction
	pushq 16(%rsp)          // rip of faulting instruction
	pushq 16(%rsp)          // rsp of faulting instruction

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
	
	movq %rsp, %rdi   	// argument to handler (looks like pointer to gregset_t up through rflags)
	
	call *fpspy_short_circuit_handler@GOTPCREL(%rip)

return_from_handler:	
	
	popq %r8		// now unwind the registers
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

/*	

stack now looks like:

 rsp +16  rflags   
 rsp + 8  rip      
 rsp + 0  rsp       

and we will build this:
 rsp - 8   => temporary stash for rax
 ...
 rsp - 88  =>  
 rsp - 96  =>  [16 bit return SS] [fill]
 rsp - 104 =>  [return rsp]
 rsp - 112 =>  [return rflags]
 rsp - 120 =>  [16 bit return CS] [fill]
 rsp - 128 =>  [return rip]  (start of constructed frame)

The following code uses only rax

*/

iret_to_dest_user_64:
	movq %rax, -8(%rsp) 	// stash rax for later use

	movq $0, -128(%rsp)	// zero out the interrupt frame we are constructing
	movq $0, -120(%rsp)
	movq $0, -112(%rsp)
	movq $0, -104(%rsp)
	movq $0, -96(%rsp)

	movw %cs, -120(%rsp)	// we return to the same code segment	

	movw %ss, -96(%rsp)  	// we return to same stack segment

	movq +8(%rsp), %rax
	movq %rax, -128(%rsp)   // set up return rip

	movq +16(%rsp), %rax
	movq %rax, -112(%rsp)   // set up return rflags

	movq +0(%rsp), %rax
	movq %rax, -104(%rsp)   // set up return rsp

	movq -8(%rsp), %rax    // restore rax
	
	leaq -128(%rsp), %rsp	// point rsp to the interrupt frame w/o modifying rflags

	iretq			// let the games begin

#endif

#if 0
	
/*
 General purpose, compilable example of a user to user iret follows
*/

/*	
 iret_to_dest_user_64

 do a user to user iret on Linux as specified by the values
 on the current stack

 assumes stack is usable below rsp
 assumes caller has already restored all gprs and fprs 
 assumes we will never return

 Stack format on entry (8 byte alignment of rsp)

 rsp + 16 =>   return rsp
 rsp + 8  =>   return rflags
 rsp + 0  =>   return rip

 stack format as we play
 rsp - 8   =>  rax save area
 ...           just in case
 rsp - 88  =>  
 rsp - 96  =>  [16 bit return SS] [fill]
 rsp - 104 =>  [return rsp]
 rsp - 112 =>  [return rflags]
 rsp - 120 =>  [16 bit return CS] [fill]
 rsp - 128 =>  [return rip]  (start of constructed frame)

*/
.global iret_to_dest_user_64
iret_to_dest_user_64:
	movq %rax, -8(%rsp) 	// stash rax for later use

	movq $0, -128(%rsp)	// zero out the interrupt frame we are constructing
	movq $0, -120(%rsp)
	movq $0, -112(%rsp)
	movq $0, -104(%rsp)
	movq $0, -96(%rsp)

	movw %cs, -120(%rsp)	// we return to the same code segment	

	movw %ss, -96(%rsp)  	// we return to same stack segment

	movq (%rsp), %rax
	movq %rax, -128(%rsp)   // set up return rip

	movq +8(%rsp), %rax
	movq %rax, -112(%rsp)   // set up return rflags

	movq +16(%rsp), %rax
	movq %rax, -104(%rsp)   // set up return rsp

	movq -8(%rsp), %rax     // restore rax
	
	leaq -128(%rsp), %rsp	// point rsp to the interrupt frame w/o modifying rflags

	iretq			// let the games begin


.global test
test:	
	pushq %rsp
	pushf
	movabs $foo, %rax
	pushq %rax
	jmp iret_to_dest_user_64


.global foo
foo:
	mov $1, %rax
	mov $1, %rdi
	movabs $hellostr, %rsi
	mov $7, %rdx
	syscall
	jmp .
	
.global _start
_start:
	jmp test
	jmp .

.global hellostr
hellostr:
	.asciz "Hello!\n"  // 7 bytes with zero

#endif
