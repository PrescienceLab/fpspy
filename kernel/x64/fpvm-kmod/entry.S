.code64
.section .text
.global _user_fpvm_entry

_user_fpvm_entry:
  pushq %rsp
  pushf
  pushq 16(%rsp)
  pushq 24(%rsp)
  pushq %r15
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
  call user_fpvm_core_handler
  
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
  popq %r15
  popq 24(%rsp)
  popq 16(%rsp)
  popf
  popq %rsp

  jmp *(%rsp)


