#define _GNU_SOURCE
#include <sys/ptrace.h>
#include <sys/reg.h>
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



static int mxcsrmask_base = 0x3f;  // which sse exceptions to handle, default all (using base zero)

#define MXCSR_FLAG_MASK (mxcsrmask_base << 0)
#define MXCSR_MASK_MASK (mxcsrmask_base << 7)

void arch_clear_trap_mask(void) { mxcsrmask_base = 0; }

void arch_set_trap_mask(int which) {
  switch (which) {
    case FE_INVALID:
      mxcsrmask_base |= 0x1;
      break;
    case FE_DENORM:
      mxcsrmask_base |= 0x2;
      break;
    case FE_DIVBYZERO:
      mxcsrmask_base |= 0x4;
      break;
    case FE_OVERFLOW:
      mxcsrmask_base |= 0x8;
      break;
    case FE_UNDERFLOW:
      mxcsrmask_base |= 0x10;
      break;
    case FE_INEXACT:
      mxcsrmask_base |= 0x20;
      break;
  }
}

void arch_reset_trap_mask(int which) {
  switch (which) {
    case FE_INVALID:
      mxcsrmask_base &= ~0x1;
      break;
    case FE_DENORM:
      mxcsrmask_base &= ~0x2;
      break;
    case FE_DIVBYZERO:
      mxcsrmask_base &= ~0x4;
      break;
    case FE_OVERFLOW:
      mxcsrmask_base &= ~0x8;
      break;
    case FE_UNDERFLOW:
      mxcsrmask_base &= ~0x10;
      break;
    case FE_INEXACT:
      mxcsrmask_base &= ~0x20;
      break;
  }
}



// MXCSR used when *we* are executing floating point code
// All masked, flags zeroed, round nearest, special features off
#define MXCSR_OURS 0x1f80


static uint32_t get_mxcsr() {
  uint32_t val = 0;
  __asm__ __volatile__("stmxcsr %0" : "=m"(val) : : "memory");
  return val;
}

static void set_mxcsr(uint32_t val) { __asm__ __volatile__("ldmxcsr %0" : : "m"(val) : "memory"); }

void arch_get_machine_fp_csr(arch_fp_csr_t *f) { f->val = get_mxcsr(); }

void arch_set_machine_fp_csr(const arch_fp_csr_t *f) { set_mxcsr(f->val); }

int arch_machine_supports_fp_traps(void) { return 1; }

void arch_config_machine_fp_csr_for_local(arch_fp_csr_t *old) {
  arch_get_machine_fp_csr(old);
  set_mxcsr(MXCSR_OURS);
}

int arch_have_special_fp_csr_exception(int which) {
  if (which == FE_DENORM) {
    return !!(get_mxcsr() & 0x2);
  } else {
    return 0;
  }
}

void arch_dump_gp_csr(const char *prefix, const ucontext_t *uc) {
  char buf[256];

  rflags_t *r = (rflags_t *)&(uc->uc_mcontext.gregs[REG_EFL]);

  sprintf(buf, "rflags = %016lx", r->val);

#define EF(x, y)         \
  if (r->x) {            \
    strcat(buf, " " #y); \
  }

  EF(zf, zero);
  EF(sf, neg);
  EF(cf, carry);
  EF(of, over);
  EF(pf, parity);
  EF(af, adjust);
  EF(tf, TRAP);
  EF(intf, interrupt);
  EF(ac, alignment);
  EF(df, down);

  DEBUG("%s: %s\n", prefix, buf);
}

void arch_dump_fp_csr(const char *pre, const ucontext_t *uc) {
  char buf[256];

  mxcsr_t *m = (mxcsr_t *)&uc->uc_mcontext.fpregs->mxcsr;

  sprintf(buf, "mxcsr = %08x flags:", m->val);

#define MF(x, y)         \
  if (m->x) {            \
    strcat(buf, " " #y); \
  }

  MF(ie, NAN);
  MF(de, DENORM);
  MF(ze, ZERO);
  MF(oe, OVER);
  MF(ue, UNDER);
  MF(pe, PRECISION);

  strcat(buf, " masking:");

  MF(im, nan);
  MF(dm, denorm);
  MF(zm, zero);
  MF(om, over);
  MF(um, under);
  MF(pm, precision);

  DEBUG("%s: %s rounding: %s %s %s\n", pre, buf,
      m->rounding == 0   ? "nearest"
      : m->rounding == 1 ? "negative"
      : m->rounding == 2 ? "positive"
                         : "zero",
      m->daz ? "DAZ" : "", m->fz ? "FTZ" : "");
}

// for trap mode on x64, we simply manipulate
// the actual hardware trap mode
#define TRAP_MODE_INIT 0
#define TRAP_MODE_OFF  1
#define TRAP_MODE_ON   2

// simply turn on trap mode
void arch_set_trap_mode(ucontext_t *uc, uint64_t *state) {
  // should be TRAP_MODE_OFF
  uc->uc_mcontext.gregs[REG_EFL] |= 0x100UL;
  if (state) {
    *state = TRAP_MODE_ON;
  }
}

// simply turn off trap mode
void arch_reset_trap_mode(ucontext_t *uc, uint64_t *state) {
  // should be in TRAP_MODE_ON or TRAP_MODE_INIT
  uc->uc_mcontext.gregs[REG_EFL] &= ~0x100UL;
  if (state) {
    *state = TRAP_MODE_OFF;
  }
}


void arch_clear_fp_exceptions(ucontext_t *uc) { uc->uc_mcontext.fpregs->mxcsr &= ~MXCSR_FLAG_MASK; }

void arch_mask_fp_traps(ucontext_t *uc) { uc->uc_mcontext.fpregs->mxcsr |= MXCSR_MASK_MASK; }

void arch_unmask_fp_traps(ucontext_t *uc) { uc->uc_mcontext.fpregs->mxcsr &= ~MXCSR_MASK_MASK; }


#define MXCSR_ROUND_DAZ_FTZ_MASK 0xe040UL

fpspy_round_config_t arch_get_machine_round_config(void) {
  uint32_t mxcsr = get_mxcsr();
  uint32_t mxcsr_round = mxcsr & MXCSR_ROUND_DAZ_FTZ_MASK;
  return mxcsr_round;
}

fpspy_round_config_t arch_get_round_config(ucontext_t *uc) {
  uint32_t mxcsr = uc->uc_mcontext.fpregs->mxcsr;
  uint32_t mxcsr_round = mxcsr & MXCSR_ROUND_DAZ_FTZ_MASK;
  DEBUG("mxcsr (0x%08x) round faz dtz at 0x%08x\n", mxcsr, mxcsr_round);
  arch_dump_fp_csr("arch_get_round_config", uc);
  return mxcsr_round;
}

void arch_set_round_config(ucontext_t *uc, fpspy_round_config_t config) {
  uc->uc_mcontext.fpregs->mxcsr &= ~MXCSR_ROUND_DAZ_FTZ_MASK;
  uc->uc_mcontext.fpregs->mxcsr |= config;
  DEBUG("mxcsr masked to 0x%08x after round daz ftz update (0x%08x)\n",
      uc->uc_mcontext.fpregs->mxcsr, config);
  arch_dump_fp_csr("arch_set_round_config", uc);
}

fpspy_round_mode_t arch_get_round_mode(fpspy_round_config_t config) { return (config >> 13) & 0x3; }

void arch_set_round_mode(fpspy_round_config_t *config, fpspy_round_mode_t mode) {
  *config &= (~0x6000);
  *config |= (mode & 0x3) << 13;
}

fpspy_dazftz_mode_t arch_get_dazftz_mode(fpspy_round_config_t *config) {
  switch (*config & 0x8040) {
    case 0x8040:
      return FPSPY_ROUND_DAZ_FTZ;
      break;
    case 0x8000:
      return FPSPY_ROUND_NO_DAZ_FTZ;
      break;
    case 0x0040:
      return FPSPY_ROUND_DAZ_NO_FTZ;
      break;
    case 0x0000:
    default:
      return FPSPY_ROUND_NO_DAZ_NO_FTZ;
      break;
  }
}

void arch_set_dazftz_mode(fpspy_round_config_t *config, fpspy_dazftz_mode_t mode) {
  *config &= ~0x8040;
  mode &= 0x3;
  switch (mode) {
    case FPSPY_ROUND_DAZ_FTZ:
      *config |= 0x8040;
      break;
    case FPSPY_ROUND_NO_DAZ_FTZ:
      *config |= 0x8000;
      break;
    case FPSPY_ROUND_DAZ_NO_FTZ:
      *config |= 0x0040;
      break;
    case FPSPY_ROUND_NO_DAZ_NO_FTZ:
    default:
      // leave at 0x0000
      break;
  }
}


uint64_t arch_get_ip(const ucontext_t *uc) { return uc->uc_mcontext.gregs[REG_RIP]; }

uint64_t arch_get_sp(const ucontext_t *uc) { return uc->uc_mcontext.gregs[REG_RSP]; }

uint64_t arch_get_gp_csr(const ucontext_t *uc) { return uc->uc_mcontext.gregs[REG_EFL]; }

uint64_t arch_get_fp_csr(const ucontext_t *uc) { return uc->uc_mcontext.fpregs->mxcsr; }


int arch_get_instr_bytes(const ucontext_t *uc, uint8_t *dest, int size) {
  int len = size > 15 ? 15 : size;

  if (size < 0) {
    return -1;
  } else {
    memcpy(dest, (const void *)uc->uc_mcontext.gregs[REG_RIP], len);
    return len;
  }
}


int arch_process_init(void) {
  DEBUG("x64 process init\n");
  return 0;
}

void arch_process_deinit(void) { DEBUG("x64 process deinit\n"); }

int arch_thread_init(ucontext_t *uc) {
  DEBUG("x64 thread init\n");
  return 0;
}

void arch_thread_deinit(void) { DEBUG("x64 thread deinit\n"); }
