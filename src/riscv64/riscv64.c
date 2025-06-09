#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <ucontext.h>
#include <fenv.h>
#include <string.h>

#include "config.h"
#include "fpspy.h"
#include "debug.h"
#include "arch.h"


#if defined(riscv64) && CONFIG_TRAP_PIPELINED_EXCEPTIONS
#include <fcntl.h>
#include "riscv64.h"
#include <sys/ioctl.h>
#define PIPELINED_DELEGATE_HELLO_WORLD 0x4630
#define PIPELINED_DELEGATE_INSTALL_HANDLER_TARGET 0x80084631
#define PIPELINED_DELEGATE_DELEGATE_TRAPS 0x80084632
#define PIPELINED_DELEGATE_CSR_STATUS 0x4633
#define PIPELINED_DELEGATE_FILE "/dev/pipelined-delegate"
#endif

extern void trap_entry(void);

/*
  We will handle only 64 bit riscv, though this should 
  work fine for 32 bit as well.

  Support for individual mode depends on having our
  extensions to the F and D mode extensions that support
  traps, and are currently just stubbed out. Ideally,
  these traps are delivered by our pipeline exceptions
  extension.

  The (non-vector) floating point state consists of 
  32 FP registers of FLEN width (32 (F) or 64 (D)) and
  a single 32 bit FCSR register.  

  FCSR[0] = NX (Inexact)
  FCSR[1] = UF (Underflow)
  FCSR[2] = OF (Overflow)
  FCSR[3] = DZ (Divide by Zero)
  FCSR[4] = NV (Invalid)
 
  Note there is apparently no way to differentiate 
  a subnormal from a zero result.  

  Note that there is no DAZ/FTZ equivalent

  FCSR[5..7] = rounding mode
               000 = RNE round to nearest, ties to even
               001 = RTZ round toward zero
               010 = RDN round down (towards -inf)
               011 = RUP round up (towards +inf)
               100 = RMM round to nearest, ties to maximum magnitude
               101 = reserved
               110 = reserved
               111 = DYN dynamic (chosen by instructions rm field?)
 
  FCSR[8..31] = reserved

  We treat this as a 64 bit register with upper 32 bits being our
  magical trap control register

  Note that THERE ARE NO TRAPS IN THE DEFAULT SETUP
  TRAPS ARE A FEATURE ADDED BY PRESCIENCE LAB TO SPECIFIC RISC-V BUILDS

  The new custom CSR 0x880 enables traps for the associated bits in fflags.

  Unclear how vector extensions fit into this.  
*/

uint32_t riscv_get_fcsr(void)
{
  uint32_t fcsr;
  __asm__ __volatile__ ("frcsr %0" : "=r"(fcsr) : :);
  return fcsr;
}

uint32_t riscv_get_fflags_mask(void)
{
  uint32_t ften;
  __asm__ __volatile__ ("csrr %0, 0x880" : "=r"(ften) : :);
  return ften;
}

void riscv_set_fcsr(uint32_t f)
{
  uint64_t fcsr = f & 0xffffffffUL;
  // technically this will also modify the register, writing
  // the old value to it, so better safe than sorry
  __asm__ __volatile__ ("fscsr %0" : : "r"(fcsr));
}

void riscv_set_fflags_mask(uint32_t fflags_mask)
{
  __asm__ __volatile__ ("csrw 0x880, %0" : : "r"(fflags_mask));
}



// Which traps to enable - default all
// bits 0..4 in upper half of fake csr are all 1
static uint64_t ften_base = 0x1fUL;

#define FLAG_MASK     ften_base
#define ENABLE_MASK   ften_base

// clearing the mask => enable all
void arch_clear_trap_mask(void)
{
  ften_base = 0x1FUL;
}

/* Set trap mask means the provided WHICH FP trap is DISABLED. */
void arch_set_trap_mask(int which)
{
  switch (which) {
  case FE_INVALID:
    ften_base &= ~(0x10UL);   // bit 4 upper half
    break;
  case FE_DENORM:  // PAD BOGUS DO NOT HAVE ON RISC-V
    ften_base &= ~(0x0UL);   // BOGUS DO NOTHING
    break;
  case FE_DIVBYZERO:
    ften_base &= ~(0x08UL);   // bit 3 upper half
    break;
  case FE_OVERFLOW:
    ften_base &= ~(0x04UL);   // bit 2 upper half
    break;
  case FE_UNDERFLOW:
    ften_base &= ~(0x02UL);   // bit 1 upper half
    break;
  case FE_INEXACT:
    ften_base &= ~(0x01UL);   // bit 0 upper half
    break;
  }
}

/* Reset trap mask means the provided WHICH FP trap is ENABLED. */
void arch_reset_trap_mask(int which)
{
  switch (which) {
  case FE_INVALID:
    ften_base |= (0x10UL);   // bit 4 upper half
    break;
  case FE_DENORM:  // PAD BOGUS DO NOT HAVE ON RISC-V
    ften_base |= (0x0UL);   // BOGUS DO NOTHING
    break;
  case FE_DIVBYZERO:
    ften_base |= (0x08UL);   // bit 3 upper half
    break;
  case FE_OVERFLOW:
    ften_base |= (0x04UL);   // bit 2 upper half
    break;
  case FE_UNDERFLOW:
    ften_base |= (0x02UL);   // bit 1 upper half
    break;
  case FE_INEXACT:
    ften_base |= (0x01UL);   // bit 0 upper half
    break;
  }
}

// FCSR used when *we* are executing floating point code
// All masked, flags zeroed, round nearest, special features off
#define FCSR_OURS   0x0000000000UL

void arch_get_machine_fp_csr(arch_fp_csr_t *f)
{
  uint64_t fflags_mask = riscv_get_fflags_mask();
  f->val = (fflags_mask << 32) | riscv_get_fcsr();
}

void arch_set_machine_fp_csr(const arch_fp_csr_t *f)
{
  uint32_t fcsr = (uint32_t) (f->val & 0xFFFFFFFFUL);
  uint32_t fflags_mask = (uint32_t) (f->val >> 32);
  riscv_set_fcsr(fcsr);
  riscv_set_fflags_mask(fflags_mask);
}

int arch_machine_supports_fp_traps(void)
{
#if CONFIG_RISCV_HAVE_FP_TRAPS
  return 1;
#else
  return 0;
#endif
}


/* Do some FP compute, set up machine state to reflect that. */
void arch_config_machine_fp_csr_for_local(arch_fp_csr_t *old)
{
  arch_get_machine_fp_csr(old);
  riscv_set_fcsr(FCSR_OURS);
}

int arch_have_special_fp_csr_exception(int which)
{
  // RISC-V does not have denorm...
  return 0;
}

// Linux's GP state is basically just the PC (masqurading as x0)
// and the GPRs (x1..x31), with special callouts for
// REG_PC 0, REG_RA 1, REG_SP 2, REG_TP 4, REG_S0 8, REG_S1 9
// REG_A0 10, REG_S2 18, REG_NARGS 8.  Note that
// branches of the form compare fpr, fpr, and branch target
// and there are no condition codes to track
void arch_dump_gp_csr(const char *prefix, const ucontext_t *uc)
{
  DEBUG("%s: [riscv has no relevant gp csr]\n", prefix);
}

/* What floating-point extension is the core using at the time of process init?
 * NOTE: We chose a default case of having NO FP support. */
static enum { HAVE_NO_FP, HAVE_F_FP, HAVE_D_FP, HAVE_Q_FP } what_fp=HAVE_NO_FP;

//
// FPR state is a union of f, d, and q state, where
// each state consits of the 32 registers, followed by
// the __fcsr.
//
// Presumably which of f, d, q, to use depends on
// whether d and q are supported in the specific architecture
// which we should figure out at process creation time
//
// We will pretend that the trap mode part of fcsr is
// included until we figure out how to handle it in
// our RISC-V implementation
//
//

static uint32_t *get_fpcsr_ptr(ucontext_t *uc)
{
  /* TODO: Determine which FP extension the core we are running on currently
   * supports. */
  switch (what_fp) {
  case HAVE_F_FP:
    return &uc->uc_mcontext.__fpregs.__f.__fcsr;
    break;
  case HAVE_D_FP:
    return &uc->uc_mcontext.__fpregs.__d.__fcsr;
    break;
  case HAVE_Q_FP:
    return &uc->uc_mcontext.__fpregs.__q.__fcsr;
    break;
  default:
    ERROR("cannot get fpcsr on machine without FP\n");
    return 0;
  }
}

/* Get fpcsr from the provided ucontext. */
static int get_fpcsr(const ucontext_t *uc, arch_fp_csr_t *f)
{
  const uint32_t *fpcsr = get_fpcsr_ptr((ucontext_t*)uc);

  if (fpcsr) {
    uint32_t ften = riscv_get_fflags_mask();
    f->val = ((uint64_t) ften << 32) | ((uint64_t) *fpcsr);
    return 0;
  } else {
    return -1;
  }
}

static int set_fpcsr(ucontext_t *uc, const arch_fp_csr_t *f)
{
  uint32_t *fpcsr = get_fpcsr_ptr(uc);

  if (fpcsr) {
    uint32_t lower = (uint32_t)f->val;
    uint32_t upper = (uint32_t)(f->val >> 32);
    *fpcsr = lower;
    riscv_set_fflags_mask(upper);
    return 0;
  } else {
    return -1;
  }
}
  

void arch_dump_fp_csr(const char *pre, const ucontext_t *uc)
{
  char buf[256];

  arch_fp_csr_t f;

  if (get_fpcsr(uc,&f)) {
    ERROR("failed to get fpcsr from context\n");
    return;
  }
  
  sprintf(buf,"fpcsr = %016lx", f.val);
  
#define SF(x,y) if (f.x) { strcat(buf, " " #y); }
  
  SF(nv,NAN);
  // SF(idc,DENORM); // does not exist...
  SF(dz,ZERO);
  SF(of,OVER);
  SF(uf,UNDER);
  SF(nx,PRECISION);

  strcat(buf," enables:");

#define CF(x,y) if (f.x) { strcat(buf, " " #y); }
  
  CF(nve,nan);
  //CF(dene,denorm); // does not exist
  CF(dze,zero);
  CF(ofe,over);
  CF(ufe,under);
  CF(nxe,precision);

  DEBUG("%s: %s rmode: %s\n",pre,buf,
	f.rm == 0 ? "nearest" :
	f.rm == 1 ? "zero" :
	f.rm == 2 ? "negative" :
	f.rm == 3 ? "positive" :
	f.rm == 4 ? "nearest-maxmag" :
	f.rm == 7 ? "dynamic" : "UNKNOWN");
}
 
 
#if CONFIG_RISCV_USE_ESTEP
// When using PPE, we can place a new "estep" instruction
// which will cause a trap that is delivered via PPE
// this instruction is 32 bits.
#define BRK_INSTR 0x00300073
#else
// In regular operation, we will place an instruction
// that produces a standard trap which is delivered
// to us using SIGTRAP.  This is the ebreak instruction.
// the break instruction is 16 bits:  0x9002 - ebreak
// we will place two of these in a row
// there is no real reason for this other than wanting
// to just reuse the arm64 logic without changes
#define BRK_INSTR 0x90029002
#endif

#define ENCODE(p,inst,data) (*(uint64_t*)(p)) = ((((uint64_t)(inst))<<32)|((uint32_t)(data)))
#define DECODE(p,inst,data) (inst) = (uint32_t)((*(uint64_t*)(p))>>32); (data) = (uint32_t)((*(uint64_t*)(p)));

static inline uint64_t insn_len(uintptr_t pc) {
    uint32_t inst = *(uint32_t*)pc;
    return (inst & 3) ? 4 : 2;
}

void arch_set_trap(ucontext_t *uc, uint64_t *state)
{
  DEBUG("%s (0x%016lx): mcontext PC: 0x%016lx\n",
        __func__, (uintptr_t)arch_set_trap,
        uc->uc_mcontext.__gregs[REG_PC]);
  // Figure out how long this instruction was so we can move our trap target on
  // the proper next instruction.
  uint64_t fp_pc = uc->uc_mcontext.__gregs[REG_PC];
  uint32_t fp_inst_width = insn_len(fp_pc);
  uint32_t *next_inst = (uint32_t*)(fp_pc + fp_inst_width);

  if (state) {
    uint32_t orig_next_inst = *next_inst;
    ENCODE(state, orig_next_inst, 2); // "2" => Stash original next inst
    *next_inst = BRK_INSTR;
    /* NOTE: Even if the target instruction (the instruction AFTER the one we
     * patched a breakpoint onto) is a compressed instruction, clearing the full
     * 4 bytes is not a real problem. */
    __builtin___clear_cache(next_inst, ((void*)next_inst) + 4);
    DEBUG("breakpoint instruction (%08x) inserted at %p overwriting %08x (state %016lx)\n",*next_inst, next_inst,orig_next_inst,*state);
  } else {
    ERROR("no state on set trap - just ignoring\n");
  }
}

void arch_reset_trap(ucontext_t *uc, uint64_t *state)
{
  uint32_t *target = (uint32_t*)(uc->uc_mcontext.__gregs[REG_PC]);

  if (state) {
    uint32_t flag;
    uint32_t instr;

    DECODE(state,instr,flag);

    switch (flag) {
    case 0:    // flag 0 = 1st trap to kick off machine
      DEBUG("skipping rewrite of instruction on first trap\n");
      break;
    case 2:    // flag 2 = trap due to inserted breakpoint instruction
      *target = instr;
      __builtin___clear_cache(target,((void*)target)+4);
      DEBUG("target at %p has been restored to original instruction %08x\n",target,instr);
      break;
    default:
      ERROR("Surprise state flag %x in reset trap\n",flag);
      break;
    }
  } else {
    ERROR("no state on reset trap - just ignoring\n");
  }
  
}

void arch_clear_fp_exceptions(ucontext_t *uc)
{
  uint32_t *fpcsr = get_fpcsr_ptr(uc);
  if (fpcsr) {
    *fpcsr &= ~FLAG_MASK;
  }
}

/* NOTE: For masking and unmasking traps, we manipulate the "fflags_care" mask
 * CSR directly. We handle the fflags (fcsr) register through the ucontext
 * structure because Linux already has all the save & restoration infrastructure
 * set up for us.
 * TODO: Does Linux handle the fflags_care CSR? */

void arch_mask_fp_traps(ucontext_t *uc)
{
  uint32_t fflags = riscv_get_fflags_mask();
  fflags &= ~ENABLE_MASK;
  riscv_set_fflags_mask(fflags);
}

void arch_unmask_fp_traps(ucontext_t *uc)
{
  uint32_t fflags = riscv_get_fflags_mask();
  fflags |= ENABLE_MASK;
  riscv_set_fflags_mask(fflags);
}

#define FCSR_ROUND_MASK (0x70UL)

fpspy_round_config_t arch_get_machine_round_config(void)
{
  uint32_t fcsr = riscv_get_fcsr();
  uint32_t fcsr_round = fcsr & FCSR_ROUND_MASK;
  return fcsr_round;
}

fpspy_round_config_t arch_get_round_config(ucontext_t *uc)
{
  arch_fp_csr_t f;

  if (get_fpcsr(uc,&f)) {
    ERROR("failed to retrieve fpcsr from uc\n");
    return -1;
  }
  
  uint32_t fpcr_round = f.val & FCSR_ROUND_MASK;
  DEBUG("fpcsr (0x%016lx) round config at 0x%08x\n", f.val, fpcr_round);
  arch_dump_fp_csr("arch_get_round_config", uc);
  return fpcr_round;
}

void arch_set_round_config(ucontext_t *uc, fpspy_round_config_t config)
{
  arch_fp_csr_t f;

  if (get_fpcsr(uc,&f)) {
    ERROR("failed to retrieve fpcsr from uc\n");
    return ;
  }
  
  f.val &= ~FCSR_ROUND_MASK;
  f.val |= config;

  if (set_fpcsr(uc,&f)) {
    ERROR("failed to set fpcsr from context\n");
    return;
  }
  DEBUG("fcsr masked to 0x%016lx after round config update (0x%08x)\n",f.val, config);
  arch_dump_fp_csr("arch_set_round_config", uc);
}

fpspy_round_mode_t     arch_get_round_mode(fpspy_round_config_t config)
{
  switch ((config>>5) & 0x7) {
  case 0:
    return FPSPY_ROUND_NEAREST;
    break;
  case 1:
    return FPSPY_ROUND_ZERO;
    break;
  case 2:
    return FPSPY_ROUND_NEGATIVE;
    break;
  case 3:
    return FPSPY_ROUND_POSITIVE;
    break;
  case 4:
    return FPSPY_ROUND_NEAREST_MAXMAG;
    break;
  case 7:
    return FPSPY_ROUND_DYNAMIC;
    break;
  default:
    return -1;
    break;
  }
}

void                   arch_set_round_mode(fpspy_round_config_t  *config, fpspy_round_mode_t mode)
{
  *config &= (~0x70);
  switch (mode) {
  case FPSPY_ROUND_NEAREST:
    *config |= 0x00;     // zero
    break;
  case FPSPY_ROUND_ZERO:
    *config |= 0x20;    // one
    break;
  case FPSPY_ROUND_NEGATIVE:
    *config |= 0x40;   // two
    break;
  case FPSPY_ROUND_POSITIVE:
    *config |= 0x60; // three
    break;
  case FPSPY_ROUND_NEAREST_MAXMAG:
    *config |= 0x80; // four
    break;
  case FPSPY_ROUND_DYNAMIC:
    *config |= 0xe0;  // seven
  }
}


fpspy_dazftz_mode_t    arch_get_dazftz_mode(fpspy_round_config_t *config)
{
  // not supported
  return FPSPY_ROUND_NO_DAZ_NO_FTZ;
}

void arch_set_dazftz_mode(fpspy_round_config_t *config, fpspy_dazftz_mode_t mode)
{
  if (mode!=FPSPY_ROUND_NO_DAZ_NO_FTZ) {
    ERROR("risc-v does not support DAZ or FTZ behavior! (asking for mode %d)\n",mode);
  }
}


uint64_t arch_get_ip(const ucontext_t *uc)
{
  return uc->uc_mcontext.__gregs[REG_PC];
}

uint64_t arch_get_sp(const ucontext_t *uc)
{
  return uc->uc_mcontext.__gregs[REG_SP];
}

uint64_t arch_get_gp_csr(const ucontext_t *uc)
{
  DEBUG("there is no gp csr on risc-v, returning 0\n");
  return 0;
}

int arch_get_instr_bytes(const ucontext_t *uc, uint8_t *dest, int size)
{
  if (size<4) {
    return -1;
  } else {
    memcpy(dest,(const void *)uc->uc_mcontext.__gregs[REG_PC],4);
    return 4;
  }
}

// representation is as the FCSR from the architecture
// with "our" extensions added to the front
uint64_t arch_get_fp_csr(const ucontext_t *uc)
{
  arch_fp_csr_t f;

  if (get_fpcsr(uc,&f)) {
    ERROR("failed to get fpcsr from context\n");
    return -1;
  }

  return f.val;
}

//
// Entry point for FP Trap with pipelined exceptions on RISC-V
//
#if CONFIG_TRAP_PIPELINED_EXCEPTIONS
// this is currently completely riscv64-specific

extern void trap_entry(void);

struct delegate_config_t {
  unsigned int en_flag;
  unsigned long trap_mask;
};

void init_pipelined_exceptions(void) {
  int fd = open(PIPELINED_DELEGATE_FILE, O_RDWR);
  struct delegate_config_t config = {
      .en_flag = 1,
      .trap_mask = (1 << 0x18) | (1 << 0x19),
  };

  DEBUG("Installing %s (0x%016lx) as pipelined delegation handler\n",
        "trap_entry", (uintptr_t) trap_entry);

  ioctl(fd, PIPELINED_DELEGATE_INSTALL_HANDLER_TARGET, trap_entry);
  ioctl(fd, PIPELINED_DELEGATE_DELEGATE_TRAPS, &config);
  /* NOTE: You must leave the pipelined delegation character device open for the
   * ENTIRE lifetime of the process. Closing the character device resets the
   * core's delegation registers to a default state! */
}

// note that unlike FPVM, the handler WILL NOT and MUST NOT
// change any state except for possibly changing
// rflags.TF and mxcsr.trap bits
//
// See src/riscv64/user_fpspy_entry.S for a layout of
// the stack and what priv points to on entry.  The summary is
// that priv is pointing to all of the int registers that have
// been saved on the stack on entry into the handler.
// return value is the PC of next instruction
static uintptr_t ppe_fpe_handler(void *priv, uintptr_t epc)
{
  // Build up a sufficiently detailed ucontext_t and
  // call the shared handler.  Copy in/out the FP and GP
  // state
  DEBUG("%s (0x%016lx): PPE Handling FPE! Building fake siginfo & ucontext\n",
        __func__, (uintptr_t)ppe_fpe_handler);

  siginfo_t fake_siginfo = {0};
  ucontext_t fake_ucontext = {0};
  arch_fp_csr_t old_fcsr;

  arch_get_machine_fp_csr(&old_fcsr);
  uint64_t fcsr = old_fcsr.val & 0xffffffffUL;

  uint32_t err = fcsr & 0x1f;
  if (err == 0x10) {	/* Invalid op (NaN)*/
    fake_siginfo.si_code = FPE_FLTINV;
  } else if (err == 0x08) { /* Divide by Zero */
    fake_siginfo.si_code = FPE_FLTDIV;
  } else if (err == 0x05) { /* Overflow */
    fake_siginfo.si_code = FPE_FLTOVF;
  } else if (err == 0x03) { /* Underflow */
    fake_siginfo.si_code = FPE_FLTUND;
  } else if (err == 0x01) { /* Precision */
    fake_siginfo.si_code = FPE_FLTRES;
  }

  siginfo_t *si = (siginfo_t *)&fake_siginfo;

#if USE_MEMCPY
  memcpy(fake_ucontext.uc_mcontext.__gregs, priv, NGREG * sizeof(fake_ucontext.uc_mcontext.__gregs[0]));
#else
  for (int i = REG_PC; i < REG_PC + NGREG; i++) {
      fake_ucontext.uc_mcontext.__gregs[i] = ((greg_t*)priv)[i];
  }
#endif

  /* XXX: We assume RISC-V D extension here! */
  fake_ucontext.uc_mcontext.__fpregs.__d.__fcsr = fcsr;

  ucontext_t *uc = (ucontext_t *)&fake_ucontext;

  uint8_t *pc = (uint8_t*) uc->uc_mcontext.__gregs[REG_PC];

  DEBUG(
  "PPE-FPE signo 0x%x errno 0x%x code 0x%x pc %p 0x%08x\n",
  si->si_signo, si->si_errno, si->si_code, si->si_addr, *(uint32_t*)pc);
  DEBUG("PPE-FPE PC=%p SP=%p\n", pc, (void *)uc->uc_mcontext.__gregs[REG_SP]);

  if (log_level > 1) {
    char buf[80];

    switch (si->si_code) {
      case FPE_FLTDIV : strcpy(buf, "FPE_FLTDIV"); break;
      case FPE_FLTINV : strcpy(buf, "FPE_FLTINV"); break;
      case FPE_FLTOVF : strcpy(buf, "FPE_FLTOVF"); break;
      case FPE_FLTUND : strcpy(buf, "FPE_FLTUND"); break;
      case FPE_FLTRES : strcpy(buf, "FPE_FLTRES"); break;
      case FPE_FLTSUB : strcpy(buf, "FPE_FLTSUB"); break;
      case FPE_INTDIV : strcpy(buf, "FPE_INTDIV"); break;
      case FPE_INTOVF : strcpy(buf, "FPE_INTOVF"); break;
    default:
      sprintf(buf, "UNKNOWN(0x%x)\n",si->si_code);
      break;
    }

    DEBUG("FPE exceptions %s\n", buf);

  }

  fp_trap_handler(si,uc);

  /* XXX: We assume D extension here! */
  /* Restore the FCSR's FP event bits. */
  riscv_set_fcsr(fake_ucontext.uc_mcontext.__fpregs.__d.__fcsr);

  DEBUG("PPE-FPE  done\n");

  return epc;
}

/* ESTEPs are a pipeline-able exception cause that has been added to RISC-V for
 * the express purpose of being delegable. In theory, breakpoints could be
 * pipeline delegated too, but that would interfere with traditional dbugging
 * tools, like GDB or Valgrind. In an effort to make things behave like people
 * would expect, we introduced ESTEP, which is IDENTICAL to EBREAK, except for
 * the fact that no external software (GDB) will issue an ESTEP instruction. */

/* Like the PPE FPE handler above, we construct a fake siginfo_t and ucontext_t
 * structs so that the arch-independent code works seamlessly.
 * When handling an ESTEP, this was intended to REPLACE the instruction
 * immediately AFTER the FP instruction. So we need to clean up and return the
 * original instruction, along with returning a set of FP flags that make sense
 * for the instruction we just executed. */
static uintptr_t ppe_estep_handler(void *real_gregs, uintptr_t epc) {
  DEBUG("%s (0x%016lx): PPE Handling ESTEP! Building fake siginfo & ucontext\n",
        __func__, (uintptr_t) ppe_estep_handler);
  siginfo_t fake_siginfo = {0};
  ucontext_t fake_ucontext = {0};

  siginfo_t *si = (siginfo_t *)&fake_siginfo;

#if USE_MEMCPY
  memcpy(fake_ucontext.uc_mcontext.__gregs, real_gregs,
         NGREG * sizeof(fake_ucontext.uc_mcontext.__gregs[0]));
#else
  for (int i = REG_PC; i < REG_PC + NGREG; i++) {
    fake_ucontext.uc_mcontext.__gregs[i] = ((greg_t*)real_gregs)[i];
  }
#endif

  fake_ucontext.uc_mcontext.__fpregs.__d.__fcsr = riscv_get_fcsr();

  monitoring_context_t *mc = find_monitoring_context(gettid());

  /* NOTE: skip_estep MUST come before brk_trap_handler! */
  int skip_estep = mc && mc->state==INIT;
  brk_trap_handler(si, &fake_ucontext);

  /* Restore the FCSR's FP event bits. */
  riscv_set_fcsr(fake_ucontext.uc_mcontext.__fpregs.__d.__fcsr);

  DEBUG("PPE ESTEP done\n");

  return skip_estep ? epc + 4 : epc;
}

// this is where the pipelined exception will land, and we will dispatch
// to the fpspy_short_circuit_handler
uintptr_t handle_ppe(uintptr_t cause, uintptr_t epc, uintptr_t regs[32]) {
  DEBUG("%s (0x%016lx): Handling pipelined trap\n",
        __func__, (uintptr_t) handle_ppe);
  void *real_gregs = (void *)regs;
  switch (cause) {
  case EXC_FLOATING_POINT:
    epc = ppe_fpe_handler(real_gregs, epc);
    break;
  case EXC_INSTRUCTION_STEP:
    epc = ppe_estep_handler(real_gregs, epc);
    break;
  default:
    abort_operation("Received unexpected trap cause!");
    break;
  }
  return epc;
}
#endif

/*
  The following is done because single step mode is typically not available for 
  user programs, so, outside of a kernel module that enables it, we need to
  use breakpoint instructions to clean up, and thus we need to be able
  to write executable regions.

  An alternative to this, which would work for post startup loads of code as well, 
  would be to handle SEGV and then edit regions

 */
static int make_my_exec_regions_writeable()
{
  DEBUG("making executable regions of memory map writeable to allow breakpoint insertion...\n");
  DEBUG("yes, this is as hideous as it sounds...\n");

  FILE *f = fopen("/proc/self/maps", "r");

  if (!f) {
    ERROR("cannot open /proc/self/maps\n");
    return -1;
  }
  
  char line_buf[256];
  
  while (!feof(f)) {
    off_t start, end;
    char flags[5];  // "rwxp\0"
    if (fgets(line_buf, 256, f) == 0) {
      //DEBUG("cannot fetch line... (soft failure)\n");
      break;
    }
    int count = sscanf(line_buf, "%lx-%lx %s\n", &start, &end, flags);
    if (count == 3) {
      if (flags[2] == 'x' && flags[0]=='r' && flags[1]!='w') {
	DEBUG("mprotecting this region as rwx: %s", line_buf);
	void *s = (void*)start;
	off_t len = end-start;
	int flags = PROT_READ | PROT_WRITE | PROT_EXEC;
	//	DEBUG("mprotect(%p,0x%lx,0x%x)\n",s,len,flags);
	if (mprotect(s,len,flags)) {
	  ERROR("failed to mptoect this region as rwx: %s",line_buf);
	  fclose(f);
	  return -1;
	}
      } else {
	//DEBUG("ignoring this region: %s",line_buf);
      }
    } else {
      DEBUG("unparseable region: %s\n",line_buf);
    }
  }
  DEBUG("completed mprotects\n");
  fclose(f);
  return 0;
}  

int  arch_process_init(void)
{
  DEBUG("riscv64 process init\n");
  // TODO: Actually figure out the FP extension in-use
  what_fp = HAVE_D_FP;
  return make_my_exec_regions_writeable();
}

void arch_process_deinit(void)
{
  DEBUG("riscv64 process deinit\n");
}

int  arch_thread_init(ucontext_t *uc)
{
  DEBUG("riscv64 thread init\n");
  return 0;
}

void arch_thread_deinit(void)
{
  DEBUG("riscv64 thread deinit\n");
}
