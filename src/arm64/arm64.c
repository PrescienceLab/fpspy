#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <ucontext.h>
#include <asm/sigcontext.h>
#include <fenv.h>
#include <string.h>

#include "config.h"
#include "fpspy.h"
#include "debug.h"
#include "arch.h"


/*
  32 bit ARM has FPSCR - a single, 32 bit status and control register

  64 bit ARM has FPCR - a single, 64 bit control register  AND
                 FPSR - a single, 64 bit status register

  FPCR[63:27] => reserved 0
  FPCR[26:15] => FPSCR[26:15] (AHP, DN, FZ, RMode,Stride,FZ16,Len,IDE)
  FPCR[14:13] => reserved 0
  FPCR[12:8]  => FPSCR[12:8]  (IXE,UFE,OFE,DZE,IOE)
  FPCR[7:0]   => ?  7:3 reserved, then NEP, AH, FIZ

  FPSR[63:32] => reserved 0
  FPSR[31:27] => FPSCR[31:27] (N, Z, C, V, QC)
  FPSR[26:8]  => reserved 0
  FPSR[7]     => FPSCR[7] IDC
  FPSR[6:5]   => reserved 0
  FPSR[4:0]   => FPSCR[4:0] (IXC, UFC, OFC, DZC, IOC)


  Note that when a trap is enabled, this DISABLES recording of the event in the exception bit.
  That is, if fpcr.dze=1, and a divide by zero happens, then fpsr.dzc remains at O!



  Unclear how Neon, etc fit into this.

*/

static uint64_t get_fpcr_machine(void) {
  uint64_t fpcr;
  __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr) : :);
  return fpcr;
}

static void set_fpcr_machine(uint64_t fpcr) { __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr)); }

static uint64_t get_fpsr_machine(void) {
  uint64_t fpsr;
  __asm__ __volatile__("mrs %0, fpsr" : "=r"(fpsr) : :);
  return fpsr;
}

static void set_fpsr_machine(uint64_t fpsr) { __asm__ __volatile__("msr fpsr, %0" : : "r"(fpsr)); }

static void sync_fp(void) { __asm__ __volatile__("dsb ish" : : : "memory"); }

// Which traps to enable - default all
// note that these are ENABLES instead of MASKS, hence the ~
//
// bits 8..12 are the default IEEE ones, then bit 15 is the denorm
//
//         1001 1111 0000 0000 =>
static int fpcr_enable_base = 0x9f00;

#define FPSR_FLAG_MASK (fpcr_enable_base >> 8)
#define FPCR_ENABLE_MASK fpcr_enable_base

// clearing the mask => enable all
void arch_clear_trap_mask(void) { fpcr_enable_base = 0x9f00; }

void arch_set_trap_mask(int which) {
  switch (which) {
    case FE_INVALID:
      fpcr_enable_base &= ~0x0100;  // bit 8  IOE
      break;
    case FE_DENORM:
      fpcr_enable_base &= ~0x8000;  // bit 15 IDE
      break;
    case FE_DIVBYZERO:
      fpcr_enable_base &= ~0x0200;  // bit 9 DZE
      break;
    case FE_OVERFLOW:
      fpcr_enable_base &= ~0x0400;  // bit 10 OFE
      break;
    case FE_UNDERFLOW:
      fpcr_enable_base &= ~0x0800;  // bit 11 UFE
      break;
    case FE_INEXACT:
      fpcr_enable_base &= ~0x1000;  // bit 12 IXE
      break;
  }
}

void arch_reset_trap_mask(int which) {
  switch (which) {
    case FE_INVALID:
      fpcr_enable_base |= 0x0100;  // bit 8  IOE
      break;
    case FE_DENORM:
      fpcr_enable_base |= 0x8000;  // bit 15 IDE
      break;
    case FE_DIVBYZERO:
      fpcr_enable_base |= 0x0200;  // bit 9 DZE
      break;
    case FE_OVERFLOW:
      fpcr_enable_base |= 0x0400;  // bit 10 OFE
      break;
    case FE_UNDERFLOW:
      fpcr_enable_base |= 0x0800;  // bit 11 UFE
      break;
    case FE_INEXACT:
      fpcr_enable_base |= 0x1000;  // bit 12 IXE
      break;
  }
}

// linuxisms which we won't use
#define FPSR_MASK 0xf800009f
#define FPCR_MASK 0x07f79f00

// FPCR used when *we* are executing floating point code
// All masked, flags zeroed, round nearest, special features off
#define FPCR_OURS 0x0
#define FPSR_OURS 0x0

void arch_get_machine_fp_csr(arch_fp_csr_t *f) {
  f->fpcr.val = get_fpcr_machine();
  f->fpsr.val = get_fpsr_machine();
}

void arch_set_machine_fp_csr(const arch_fp_csr_t *f) {
  set_fpcr_machine(f->fpcr.val);
  set_fpsr_machine(f->fpsr.val);
  sync_fp();
}

int arch_machine_supports_fp_traps(void) {
  uint64_t oldfpcr;

  oldfpcr = get_fpcr_machine();

  set_fpcr_machine(-1UL);
  sync_fp();

  uint64_t fpcr = get_fpcr_machine();

  set_fpcr_machine(oldfpcr);
  sync_fp();

  return (fpcr & 0x9f00) == 0x9f00;
}



void arch_config_machine_fp_csr_for_local(arch_fp_csr_t *old) {
  arch_get_machine_fp_csr(old);
  set_fpcr_machine(FPCR_OURS);
  set_fpsr_machine(FPSR_OURS);
  sync_fp();
}

int arch_have_special_fp_csr_exception(int which) {
  if (which == FE_DENORM) {
    return !!(get_fpsr_machine() & 0x100);  // bit 8, IDC
  } else {
    return 0;
  }
}

void arch_dump_gp_csr(const char *prefix, const ucontext_t *uc) {
  char buf[256];

  pstate_t *p = (pstate_t *)&(uc->uc_mcontext.pstate);

  sprintf(buf, "pstate = %08x", p->val);

#define EF(x, y)         \
  if (p->x) {            \
    strcat(buf, " " #y); \
  }

  EF(z, zero);
  EF(n, neg);
  EF(c, carry);
  EF(v, over);
  EF(ss, singlestep);
  EF(a, serror);
  EF(d, debug);
  EF(f, fiqmask);
  EF(i, irqmask);

  DEBUG("%s: %s\n", prefix, buf);
}


static int get_fpsr(const ucontext_t *uc, fpsr_t *f) {
  struct fpsimd_context *c = (struct fpsimd_context *)(uc->uc_mcontext.__reserved);

  if (c->head.magic != FPSIMD_MAGIC) {
    ERROR("Wrong magic: found %08x\n", c->head.magic);
    return -1;
  }

  f->val = c->fpsr;

  // DEBUG("get_fpsr returns %016lx\n",f->val);

  return 0;
}

static int set_fpsr(ucontext_t *uc, const fpsr_t *f) {
  struct fpsimd_context *c = (struct fpsimd_context *)(uc->uc_mcontext.__reserved);

  if (c->head.magic != FPSIMD_MAGIC) {
    ERROR("Wrong magic: found %08x\n", c->head.magic);
    return -1;
  }

  c->fpsr = f->val;

  // DEBUG("set_fpsr(%016lx) succeeds (written value is %08x)\n",f->val,c->fpsr);

  return 0;
}

static int get_fpcr(const ucontext_t *uc, fpcr_t *f) {
  struct fpsimd_context *c = (struct fpsimd_context *)(uc->uc_mcontext.__reserved);

  if (c->head.magic != FPSIMD_MAGIC) {
    ERROR("Wrong magic: found %08x\n", c->head.magic);
    return -1;
  }

  f->val = c->fpcr;

  //  DEBUG("get_fpcr returns %016lx\n",f->val);

  return 0;
}

static int set_fpcr(ucontext_t *uc, const fpcr_t *f) {
  struct fpsimd_context *c = (struct fpsimd_context *)(uc->uc_mcontext.__reserved);

  if (c->head.magic != FPSIMD_MAGIC) {
    ERROR("Wrong magic: found %08x\n", c->head.magic);
    return -1;
  }

  c->fpcr = f->val;

  // DEBUG("set_fpcr(%016lx) succeeds (written value is %08x)\n",f->val,c->fpcr);

  return 0;
}


static int get_fpcsr(const ucontext_t *uc, arch_fp_csr_t *f) {
  return get_fpsr(uc, &f->fpsr) || get_fpcr(uc, &f->fpcr);
}

static int set_fpcsr(ucontext_t *uc, const arch_fp_csr_t *f) {
  return set_fpsr(uc, &f->fpsr) || set_fpcr(uc, &f->fpcr);
}


void arch_dump_fp_csr(const char *pre, const ucontext_t *uc) {
  char buf[256];

  arch_fp_csr_t f;

  if (get_fpcsr(uc, &f)) {
    ERROR("failed to get fpcsr from context\n");
    return;
  }

  sprintf(buf, "fpcr = %016lx fpsr = %016lx flags:", f.fpcr.val, f.fpsr.val);

#define SF(x, y)         \
  if (f.fpsr.x) {        \
    strcat(buf, " " #y); \
  }

  SF(ioc, NAN);
  SF(idc, DENORM);
  SF(dzc, ZERO);
  SF(ofc, OVER);
  SF(ufc, UNDER);
  SF(ixc, PRECISION);

  strcat(buf, " enables:");

#define CF(x, y)         \
  if (f.fpcr.x) {        \
    strcat(buf, " " #y); \
  }

  CF(ioe, nan);
  CF(ide, denorm);
  CF(dze, zero);
  CF(ofe, over);
  CF(ufe, under);
  CF(ixe, precision);

  strcat(buf, " compares:");

  SF(z, zero);
  SF(n, neg);
  SF(c, carry);
  SF(v, over);

  DEBUG("%s: %s rmode: %s %s %s %s\n", pre, buf,
      f.fpcr.rmode == 0   ? "nearest"
      : f.fpcr.rmode == 1 ? "negative"
      : f.fpcr.rmode == 2 ? "positive"
                          : "zero",
      f.fpcr.fiz ? "FIZ" : "", f.fpcr.ah ? "AH" : "", f.fpcr.fz ? "FZ" : "");
}


//  brk	#23
#define BRK_INSTR 0xd42002e0


#define ENCODE(p, inst, data) (*(uint64_t *)(p)) = ((((uint64_t)(inst)) << 32) | ((uint32_t)(data)))
#define DECODE(p, inst, data)                    \
  (inst) = (uint32_t)((*(uint64_t *)(p)) >> 32); \
  (data) = (uint32_t)((*(uint64_t *)(p)));

void arch_set_trap(ucontext_t *uc, uint64_t *state) {
  uint32_t *target = (uint32_t *)(uc->uc_mcontext.pc + 4);  // all instructions are 4 bytes

  if (state) {
    // uint32_t old = *target;
    ENCODE(state, *target, 2);  // "2"=> we are stashing the old instruction
    *target = BRK_INSTR;
    __builtin___clear_cache(target, ((void *)target) + 4);
    // DEBUG("breakpoint instruction (%08x) inserted at %p overwriting %08x (state
    // %016lx)\n",*target, target,old,*state);
  } else {
    ERROR("no state on set trap - just ignoring\n");
  }
}

void arch_reset_trap(ucontext_t *uc, uint64_t *state) {
  uint32_t *target = (uint32_t *)(uc->uc_mcontext.pc);

  if (state) {
    uint32_t flag;
    uint32_t instr;

    DECODE(state, instr, flag);

    switch (flag) {
      case 0:  // flag 0 = 1st trap to kick off machine
        // DEBUG("skipping rewrite of instruction on first trap\n");
        break;
      case 2:  // flag 2 = trap due to inserted breakpoint instruction
        *target = instr;
        __builtin___clear_cache(target, ((void *)target) + 4);
        // DEBUG("target at %p has been restored to original instruction %08x\n",target,instr);
        break;
      default:
        ERROR("Surprise state flag %x in reset trap\n", flag);
        break;
    }
  } else {
    ERROR("no state on reset trap - just ignoring\n");
  }
}

void arch_clear_fp_exceptions(ucontext_t *uc) {
  fpsr_t f;

  if (get_fpsr(uc, &f)) {
    ERROR("failed to get fpsr from context\n");
    return;
  }

  f.val &= ~FPSR_FLAG_MASK;

  if (set_fpsr(uc, &f)) {
    ERROR("failed to set fpsr from context\n");
    return;
  }
}

void arch_mask_fp_traps(ucontext_t *uc) {
  fpcr_t f;

  if (get_fpcr(uc, &f)) {
    ERROR("failed to get fpcr from context\n");
    return;
  }

  f.val &= ~FPCR_ENABLE_MASK;

  if (set_fpcr(uc, &f)) {
    ERROR("failed to set fpcr from context\n");
    return;
  }
}

void arch_unmask_fp_traps(ucontext_t *uc) {
  fpcr_t f;

  if (get_fpcr(uc, &f)) {
    ERROR("failed to get fpcr from context\n");
    return;
  }

  f.val |= FPCR_ENABLE_MASK;

  if (set_fpcr(uc, &f)) {
    ERROR("failed to set fpcr from context\n");
    return;
  }
}

// see notes in arm64.h for how this crazy thing works
// FZ = bit 24
// RM = bits 23-22
// AH = bit 1
// FIZ = bit 0
#define FPCR_ROUND_DAZ_FTZ_MASK ((0x1c00003))

fpspy_round_config_t arch_get_machine_round_config(void) {
  uint64_t fpcr = get_fpcr_machine();
  uint32_t fpcr_round = fpcr & FPCR_ROUND_DAZ_FTZ_MASK;
  return fpcr_round;
}

fpspy_round_config_t arch_get_round_config(ucontext_t *uc) {
  fpcr_t f;

  if (get_fpcr(uc, &f)) {
    ERROR("failed to retrieve fpcr from uc\n");
    return -1;
  }

  uint32_t fpcr_round = (uint64_t)f.val & FPCR_ROUND_DAZ_FTZ_MASK;
  DEBUG("fpcr (0x%016lx) round config at 0x%08x\n", f.val, fpcr_round);
  arch_dump_fp_csr("arch_get_round_config", uc);
  return fpcr_round;
}

void arch_set_round_config(ucontext_t *uc, fpspy_round_config_t config) {
  fpcr_t f;

  if (get_fpcr(uc, &f)) {
    ERROR("failed to retrieve fpcr from uc\n");
    return;
  }

  f.val &= ~FPCR_ROUND_DAZ_FTZ_MASK;
  f.val |= config;

  DEBUG("fpcr masked to 0x%016lx after round config update (0x%08x)\n", f.val, config);
  arch_dump_fp_csr("arch_set_round_config", uc);
}

fpspy_round_mode_t arch_get_round_mode(fpspy_round_config_t config) {
  switch ((config >> 22) & 0x3) {
    case 0:
      return FPSPY_ROUND_NEAREST;
      break;
    case 1:
      return FPSPY_ROUND_POSITIVE;
      break;
    case 2:
      return FPSPY_ROUND_NEGATIVE;
      break;
    case 3:
      return FPSPY_ROUND_ZERO;
      break;
    default:
      return -1;
      break;
  }
}

void arch_set_round_mode(fpspy_round_config_t *config, fpspy_round_mode_t mode) {
  *config &= (~0xc00000);
  switch (mode) {
    case FPSPY_ROUND_NEAREST:
      *config |= 0x0;  // zero
      break;
    case FPSPY_ROUND_POSITIVE:
      *config |= 0x400000;  // one
      break;
    case FPSPY_ROUND_NEGATIVE:
      *config |= 0x800000;  // two
      break;
    case FPSPY_ROUND_ZERO:
      *config |= 0xc00000;  // three
      break;
  }
}

fpspy_dazftz_mode_t arch_get_dazftz_mode(fpspy_round_config_t *config) {
  int daz = 0;
  int ftz = 0;

  if (*config & 0x1) {
    // fiz:
    daz = 1;
  }
  if (*config & 0x2) {
    // alternate handling, now look at fz
    if (*config & 0x1000000) {
      ftz = 1;
      // daz from above
    } else {
      // ftz from above
      daz = 1;
    }
  } else {
    // normal handling, now look at fz
    if (*config & 0x1000000) {
      ftz = 1;
      daz = 1;
    } else {
      ftz = 0;
      // daz from above
    }
  }

  return daz * 2 + ftz;
}

void arch_set_dazftz_mode(fpspy_round_config_t *config, fpspy_dazftz_mode_t mode) {
  *config &= ~0x1000003;
  switch (mode) {
    case FPSPY_ROUND_NO_DAZ_NO_FTZ:
      // fiz=0, ah=0, fz= 0
      // do nothing
      break;
    case FPSPY_ROUND_NO_DAZ_FTZ:
      // fiz=0, ah=1, fz= 1
      *config |= 0x1000002;
      break;
    case FPSPY_ROUND_DAZ_NO_FTZ:
      // fiz=1, ah=1, fz= 0
      *config |= 0x0000003;
      break;
    case FPSPY_ROUND_DAZ_FTZ:
      // fiz=1, ah=0, fz= 1
      *config |= 0x1000001;
      break;
  }
}


uint64_t arch_get_ip(const ucontext_t *uc) { return uc->uc_mcontext.pc; }

uint64_t arch_get_sp(const ucontext_t *uc) { return uc->uc_mcontext.sp; }

uint64_t arch_get_gp_csr(const ucontext_t *uc) { return uc->uc_mcontext.pstate; }

int arch_get_instr_bytes(const ucontext_t *uc, uint8_t *dest, int size) {
  if (size < 4) {
    return -1;
  } else {
    memcpy(dest, (const void *)uc->uc_mcontext.pc, 4);
    return 4;
  }
}


// representation is as 2 back to back 32 bit regs
// FPCR : FPSR
uint64_t arch_get_fp_csr(const ucontext_t *uc) {
  arch_fp_csr_t f;

  if (get_fpcsr(uc, &f)) {
    ERROR("failed to get fpcsr from context\n");
    return -1;
  }

  return (f.fpcr.val << 32) | (f.fpsr.val & 0xffffffff);
}

/*
  The following is done because single step mode is typically not available for
  user programs, so, outside of a kernel module that enables it, we need to
  use breakpoint instructions to clean up, and thus we need to be able
  to write executable regions.

  An alternative to this, which would work for post startup loads of code as well,
  would be to handle SEGV and then edit regions

  A kernel module could also provide us with direct access to the cycle
  counter so that we could have a real arch_cycle_count() - see HAVE_EL0_COUNTER_ACCESS
  in arm64.h.



 */
static int make_my_exec_regions_writeable() {
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
      // DEBUG("cannot fetch line... (soft failure)\n");
      break;
    }
    int count = sscanf(line_buf, "%lx-%lx %s\n", &start, &end, flags);
    if (count == 3) {
      if (flags[2] == 'x' && flags[0] == 'r' && flags[1] != 'w') {
        DEBUG("mprotecting this region as rwx: %s", line_buf);
        void *s = (void *)start;
        off_t len = end - start;
        int flags = PROT_READ | PROT_WRITE | PROT_EXEC;
        //	DEBUG("mprotect(%p,0x%lx,0x%x)\n",s,len,flags);
        if (mprotect(s, len, flags)) {
          ERROR("failed to mptoect this region as rwx: %s", line_buf);
          fclose(f);
          return -1;
        }
      } else {
        // DEBUG("ignoring this region: %s",line_buf);
      }
    } else {
      DEBUG("unparseable region: %s\n", line_buf);
    }
  }
  DEBUG("completed mprotects\n");
  fclose(f);
  return 0;
}

int arch_process_init(void) {
  DEBUG("arm64 process init\n");
  return make_my_exec_regions_writeable();
}

void arch_process_deinit(void) { DEBUG("arm64 process deinit\n"); }

int arch_thread_init(ucontext_t *uc) {
  DEBUG("arm64 thread init\n");
  return 0;
}

void arch_thread_deinit(void) { DEBUG("arm64 thread deinit\n"); }
