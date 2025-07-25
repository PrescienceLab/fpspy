# See LICENSE for license details.

#include "encoding.h"

#if __riscv_xlen == 64
# define LREG ld
# define SREG sd
# define REGBYTES 8
#else
# define LREG lw
# define SREG sw
# define REGBYTES 4
#endif

  .section ".text.init"
  .globl _start
_start:
  li  x1, 0
  li  x2, 0
  li  x3, 0
  li  x4, 0
  li  x5, 0
  li  x6, 0
  li  x7, 0
  li  x8, 0
  li  x9, 0
  li  x10,0
  li  x11,0
  li  x12,0
  li  x13,0
  li  x14,0
  li  x15,0
  li  x16,0
  li  x17,0
  li  x18,0
  li  x19,0
  li  x20,0
  li  x21,0
  li  x22,0
  li  x23,0
  li  x24,0
  li  x25,0
  li  x26,0
  li  x27,0
  li  x28,0
  li  x29,0
  li  x30,0
  li  x31,0

  # enable FPU and accelerator if present
  li t0, MSTATUS_FS | MSTATUS_XS
  csrs mstatus, t0

  # make sure XLEN agrees with compilation choice
  li t0, 1
  slli t0, t0, 31
#if __riscv_xlen == 64
  bgez t0, 1f
#else
  bltz t0, 1f
#endif
2:
  li a0, 1
  sw a0, tohost, t0
  j 2b
1:

#ifdef __riscv_flen
  # initialize FPU if we have one
  la t0, 1f
  csrw mtvec, t0

  fssr    x0
  fmv.s.x f0, x0
  fmv.s.x f1, x0
  fmv.s.x f2, x0
  fmv.s.x f3, x0
  fmv.s.x f4, x0
  fmv.s.x f5, x0
  fmv.s.x f6, x0
  fmv.s.x f7, x0
  fmv.s.x f8, x0
  fmv.s.x f9, x0
  fmv.s.x f10,x0
  fmv.s.x f11,x0
  fmv.s.x f12,x0
  fmv.s.x f13,x0
  fmv.s.x f14,x0
  fmv.s.x f15,x0
  fmv.s.x f16,x0
  fmv.s.x f17,x0
  fmv.s.x f18,x0
  fmv.s.x f19,x0
  fmv.s.x f20,x0
  fmv.s.x f21,x0
  fmv.s.x f22,x0
  fmv.s.x f23,x0
  fmv.s.x f24,x0
  fmv.s.x f25,x0
  fmv.s.x f26,x0
  fmv.s.x f27,x0
  fmv.s.x f28,x0
  fmv.s.x f29,x0
  fmv.s.x f30,x0
  fmv.s.x f31,x0
1:
#endif

  # initialize trap vectors
  # With the current method of hacking in "U-mode delegation", the processor
  # delegates to S-mode, but we hack in a change where ONLY a set of CSRs that
  # we need to jump back to U-mode are changed. This means that unless the
  # design is significantly reworked to add proper U-mode delegation CSRs, we
  # must set both mtvec and stvec!
  la t0, trap_entry
  csrw mtvec, t0
  csrw stvec, t0

#define ROCC_INTERRUPT_BIT 0x2000
  # Enable RoCC interrupts
  csrr t0, mie
  li t1, ROCC_INTERRUPT_BIT
  or t0, t0, t1
  csrw mie, t0
  csrr t0, mie

  # Enable Machine Interrupts
  csrr t0, mstatus
  li t1, MSTATUS_MIE
  or t0, t0, t1
  csrw mstatus, t0
  csrr t0, mstatus

  # initialize global pointer
.option push
.option norelax
  la gp, __global_pointer$
.option pop

  la  tp, _end + 63
  and tp, tp, -64

  # get core id
  csrr a0, mhartid
  # for now, assume only 1 core
  li a1, 1
1:bgeu a0, a1, 1b

  # give each core 128KB of stack + TLS
#define STKSHIFT 17
  add sp, a0, 1
  sll sp, sp, STKSHIFT
  add sp, sp, tp
  sll a2, a0, STKSHIFT
  add tp, tp, a2

  .align 2
  # Set M Previous Privilege to User, so mret goes back to user.
  # Reset bits 12:11 to 00
#define MSTATUS_MPP_U 0xFFFFFFFFFFFFE7FF
  csrr t0, mstatus
  li t1, MSTATUS_MPP_U
  and t0, t0, t1
  csrw mstatus, t0

  # Delegate the illegal instruction exception
#define FLOATING_POINT_XCPT 0x1000000
  li t0, FLOATING_POINT_XCPT
  csrw medeleg, t0
  # Do not delegate interrupts
  li t0, 0
  csrw mideleg, t0

  /* Set up Pipelined Interrupts */
  li t0, FLOATING_POINT_XCPT
  csrw sedeleg, t0
  csrr t1, sedeleg
  csrw sideleg, zero

  /* NOTE: Set pipelined interrupt target in starget CSR */
  la t0, user_deleg_xcpt_handler
  csrw 0x5c0, t0
  csrr t1, 0x5c0

  /* Set Rocket's UIE to 1 so make Pipelined Interrupts behave right.
   * NOTE: This is a SUPER MASSIVE hack right now! */
  csrr t0, mstatus
  ori t0, t0, MSTATUS_UIE
  csrw mstatus, t0

  // We expect to jump to handle_trap and exit.
  // jal handle_trap // Should make us exit.

  # Set up Physical Memory Protection, letting the user have access to the entire
  # physical address space.
  li t0, 0x3fffffffffffff
  csrw pmpaddr0, t0
  li t0, 0xf
  csrw pmpcfg0, t0

  # Clear mcause before we do anything, so we see exceptions exactly as they
  # happen.
  csrw mcause, zero
  
  # go to main on the mret
  la t0, _init
  csrw mepc, t0
  

  # mret to User-level, using the value in mepc to set the PC where the CPU should
  # begin executing after the switch.
  # Nothing directly after the mret will execute!
  mret


  .align 2

/* Create a user-level illegal instruction exception handler. All this will do
 * is clobber a register we do not use and return. The point of this test is to
 * test the URET instruction. */
  .align 2
user_deleg_xcpt_handler:
  li a6, 0x1234567898765432

  /* Read the UEPC register so we can skip the failing FP instruction */
  csrr t0, 0x841

  /* FP instructions are 4 bytes */
  addi t0, t0, 4
  csrw 0x841, t0

  uret
// END user_deleg_xcpt_handler

init_entry:
  j _init
// END init_entry

  .align 2
trap_entry:
  addi sp, sp, -272

  # Use an M-mode-only instruction to see if we are actually in U-mode after our
  # delegation hack.
  # Chose t0 register because we already used it as a garbage register.
  csrr t0, marchid

  SREG x1, 1*REGBYTES(sp)
  SREG x2, 2*REGBYTES(sp)
  SREG x3, 3*REGBYTES(sp)
  SREG x4, 4*REGBYTES(sp)
  SREG x5, 5*REGBYTES(sp)
  SREG x6, 6*REGBYTES(sp)
  SREG x7, 7*REGBYTES(sp)
  SREG x8, 8*REGBYTES(sp)
  SREG x9, 9*REGBYTES(sp)
  SREG x10, 10*REGBYTES(sp)
  SREG x11, 11*REGBYTES(sp)
  SREG x12, 12*REGBYTES(sp)
  SREG x13, 13*REGBYTES(sp)
  SREG x14, 14*REGBYTES(sp)
  SREG x15, 15*REGBYTES(sp)
  SREG x16, 16*REGBYTES(sp)
  SREG x17, 17*REGBYTES(sp)
  SREG x18, 18*REGBYTES(sp)
  SREG x19, 19*REGBYTES(sp)
  SREG x20, 20*REGBYTES(sp)
  SREG x21, 21*REGBYTES(sp)
  SREG x22, 22*REGBYTES(sp)
  SREG x23, 23*REGBYTES(sp)
  SREG x24, 24*REGBYTES(sp)
  SREG x25, 25*REGBYTES(sp)
  SREG x26, 26*REGBYTES(sp)
  SREG x27, 27*REGBYTES(sp)
  SREG x28, 28*REGBYTES(sp)
  SREG x29, 29*REGBYTES(sp)
  SREG x30, 30*REGBYTES(sp)
  SREG x31, 31*REGBYTES(sp)

  csrr a0, mcause
  csrr a1, mepc
  mv a2, sp
  jal handle_trap
  csrw mepc, a0

  # Remain in M-mode after eret
  li t0, MSTATUS_MPP
  csrs mstatus, t0

  LREG x1, 1*REGBYTES(sp)
  LREG x2, 2*REGBYTES(sp)
  LREG x3, 3*REGBYTES(sp)
  LREG x4, 4*REGBYTES(sp)
  LREG x5, 5*REGBYTES(sp)
  LREG x6, 6*REGBYTES(sp)
  LREG x7, 7*REGBYTES(sp)
  LREG x8, 8*REGBYTES(sp)
  LREG x9, 9*REGBYTES(sp)
  LREG x10, 10*REGBYTES(sp)
  LREG x11, 11*REGBYTES(sp)
  LREG x12, 12*REGBYTES(sp)
  LREG x13, 13*REGBYTES(sp)
  LREG x14, 14*REGBYTES(sp)
  LREG x15, 15*REGBYTES(sp)
  LREG x16, 16*REGBYTES(sp)
  LREG x17, 17*REGBYTES(sp)
  LREG x18, 18*REGBYTES(sp)
  LREG x19, 19*REGBYTES(sp)
  LREG x20, 20*REGBYTES(sp)
  LREG x21, 21*REGBYTES(sp)
  LREG x22, 22*REGBYTES(sp)
  LREG x23, 23*REGBYTES(sp)
  LREG x24, 24*REGBYTES(sp)
  LREG x25, 25*REGBYTES(sp)
  LREG x26, 26*REGBYTES(sp)
  LREG x27, 27*REGBYTES(sp)
  LREG x28, 28*REGBYTES(sp)
  LREG x29, 29*REGBYTES(sp)
  LREG x30, 30*REGBYTES(sp)
  LREG x31, 31*REGBYTES(sp)

  addi sp, sp, 272
  mret

.section ".tohost","aw",@progbits
.align 6
.globl tohost
tohost: .dword 0
.align 6
.globl fromhost
fromhost: .dword 0
