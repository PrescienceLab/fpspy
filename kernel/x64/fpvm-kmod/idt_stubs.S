.code64
.extern the_fpvm_hook
.extern fpvm_error_entry
.extern fpvm_error_return

.section .text
.global _fpvm_idt_entry


_fpvm_idt_entry:
    clac
    cld
    push $0xFFFFFFFFFFFFFFFF
    call *fpvm_error_entry
    mov %rax, %rsp
    mov %rsp, %rdi
    call the_fpvm_hook
    jmp *fpvm_error_return 
    nop
    nop
    nop
    nop
    nop

/* INT FRAME
    32: SS
    24: RSP
    16: RFLAGS
    8:  CS
    0:  RIP (<--- RSP)
*/

.global _fpvm_hw_timing_idt_entry
_fpvm_hw_timing_idt_entry:
    rdtsc
    shl $32, %rdx
    orq %rdx, %rax  // Now RDTSC is in RAX
    addq $4, (%rsp) // Move RIP up 4 (skip faulting instruction)
    iretq           // Should use the same INT FRAME for IRET
    nop
    nop


