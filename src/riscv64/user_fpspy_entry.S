# See LICENSE for license details.

#include <config.h>

#if __riscv_xlen == 64
# define LREG ld
# define SREG sd
# define REGBYTES 8
#else
# define LREG lw
# define SREG sw
# define REGBYTES 4
#endif

#define CSR_USCRATCH 0x840
#define CSR_UEPC 0x841
#define CSR_UCAUSE 0x842
#define CSR_FFLAGS_CARE 0x880

  .section ".text"

/* Read from the cycle counter and stash it into USCRATCH. We must be careful
 * about which integer register we clobber here and the manner in which we do it
 * so that we do not destroy any state unnecessarily. We MUST clobber an integer
 * register since there are no instructions to transfer a value from one CSR to
 * another CSR in RISC-V.
 * STEPS:
 * 1. uscratch <- x1 for safe keeping
 * 2. x1 <- cycles
 * 3. x1 <- uscratch (stashed x1) AND SIMULTANEOUSLY
 *    uscratch <- x1 (cycle count) */
  .macro CYCLES_TO_USCRATCH
  csrw CSR_USCRATCH, x1
  rdcycle x1
  csrrw x1, CSR_USCRATCH, x1
  .endm

  .global trap_entry
  .align 16
trap_entry:
#if !CONFIG_RISCV_TRAP_BYPASSED_EXCEPTIONS && !CONFIG_RISCV_USE_ESTEP
  // should never get called...
  uret
#else
  /* NOTE: This rdcycle should NOT be in the final handler implementation. We
   * only need it here for timing the inst->handler->next-inst sequence. */
  CYCLES_TO_USCRATCH

  addi sp, sp, -272

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

  /* With all GREGS saved, we also need to save the PC of the instruction that
   * faulted to the 0th index of the stack, to match up with what mcontext's
   * gregs expect. On RISC-V GREG x0 (the zero register) is hardwired to zero,
   * so we do not need to save it. Instead, we can stash PC there instead.
   * FIXME: If we only use PC passed through the GREGS structure on the stack,
   * then we MUST reload this value off the stack, instead of having handle_kbe
   * return a new UEPC.
   * NOTE: This UEPC value in a1 is ALSO passed through to handle_kbe now. */
  csrr a1, CSR_UEPC
  SREG a1, 0*REGBYTES(sp)

  csrr a0, CSR_UCAUSE
  mv a2, sp
  la s3, handle_kbe
  jalr s3
  csrw CSR_UEPC, a0

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

  /* Like at the start of this ASM procedure, we capture the time we leave this
   * handler right before we execute the URET, so that we can measure the time
   * required for us to jump back to the location we specified in uepc above. */
  CYCLES_TO_USCRATCH

  uret
#endif
