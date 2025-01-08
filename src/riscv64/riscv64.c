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
#include "debug.h"
#include "arch.h"


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

static uint64_t get_fcsr_machine(void)
{
  uint64_t fcsr;
  uint64_t ften;
  __asm__ __volatile__ ("frcsr %0" : "=r"(fcsr) : :);
  __asm__ __volatile__ ("csrr %0, 0x880" : "=r"(ften) : :);
  return (ften<<32) | (fcsr & 0xffffffffUL);
}

static void set_fcsr_machine(uint64_t f)
{
  uint64_t fcsr = f & 0xffffffffUL;
  uint64_t ften = f >> 32;
  // technically this will also modify the register, writing
  // the old value to it, so better safe than sorry
  __asm__ __volatile__ ("fscsr %0" : : "r"(fcsr));
  __asm__ __volatile__ ("csrw 0x880, %0" : : "r"(ften));
}



// Which traps to enable - default all
// bits 0..4 in upper half of fake csr are all 1
static uint64_t ften_base = 0x1f00000000UL; 

#define FLAG_MASK     (ften_base>>32)
#define ENABLE_MASK   ften_base

// clearing the mask => enable all
void arch_clear_trap_mask(void)
{
  ften_base = 0x1f00000000;
}

void arch_set_trap_mask(int which)
{
  switch (which) {
  case FE_INVALID:
    ften_base &= ~(0x1000000000UL);   // bit 4 upper half
    break;
  case FE_DENORM:  // PAD BOGUS DO NOT HAVE ON RISC-V
    ften_base &= ~(0x0UL);   // BOGUS DO NOTHING
    break;
  case FE_DIVBYZERO:
    ften_base &= ~(0x0800000000UL);   // bit 3 upper half
    break;
  case FE_OVERFLOW:
    ften_base &= ~(0x0400000000UL);   // bit 2 upper half
    break;
  case FE_UNDERFLOW:
    ften_base &= ~(0x0200000000UL);   // bit 1 upper half
    break;
  case FE_INEXACT:
    ften_base &= ~(0x0100000000UL);   // bit 0 upper half
    break;
  }
}

void arch_reset_trap_mask(int which)
{
  switch (which) {
  case FE_INVALID:
    ften_base |= (0x1000000000UL);   // bit 4 upper half
    break;
  case FE_DENORM:  // PAD BOGUS DO NOT HAVE ON RISC-V
    ften_base |= (0x0UL);   // BOGUS DO NOTHING
    break;
  case FE_DIVBYZERO:
    ften_base |= (0x0800000000UL);   // bit 3 upper half
    break;
  case FE_OVERFLOW:
    ften_base |= (0x0400000000UL);   // bit 2 upper half
    break;
  case FE_UNDERFLOW:
    ften_base |= (0x0200000000UL);   // bit 1 upper half
    break;
  case FE_INEXACT:
    ften_base |= (0x0100000000UL);   // bit 0 upper half
    break;
  }
}

// FCSR used when *we* are executing floating point code
// All masked, flags zeroed, round nearest, special features off
#define FCSR_OURS   0x0000000000UL

void arch_get_machine_fp_csr(arch_fp_csr_t *f)
{
  f->val = get_fcsr_machine();
}

void arch_set_machine_fp_csr(const arch_fp_csr_t *f)
{
  set_fcsr_machine(f->val);
}

int      arch_machine_supports_fp_traps(void)
{
#if CONFIG_RISCV_HAVE_FP_TRAPS
  return 1;
#else
  return 0;
#endif
}
  


void arch_config_machine_fp_csr_for_local(arch_fp_csr_t *old)
{
  arch_get_machine_fp_csr(old);
  set_fcsr_machine(FCSR_OURS);
}

int      arch_have_special_fp_csr_exception(int which)
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

static int get_fpcsr(const ucontext_t *uc, arch_fp_csr_t *f)
{
  const uint32_t *fpcsr = get_fpcsr_ptr((ucontext_t*)uc);

  if (fpcsr) { 
    uint32_t ften;
    __asm__ __volatile__ ("csrr %0, 0x880" : "=r"(ften) : :);
    f->val = ((uint64_t) *fpcsr) | ((uint64_t) ften << 32);
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
    uint32_t __attribute__((unused)) upper = (uint32_t)(f->val >> 32);
    *fpcsr = lower;

    __asm__ __volatile__ ("csrw 0x880, %0" : : "r"(lower));
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
 
 
// the break instruction is 16 bits:  0x9002 - ebreak
// we will place two of these in a row
// there is no real reason for this other than wanting
// to just reuse the arm64 logic without changes
#define BRK_INSTR 0x90029002


#define ENCODE(p,inst,data) (*(uint64_t*)(p)) = ((((uint64_t)(inst))<<32)|((uint32_t)(data)))
#define DECODE(p,inst,data) (inst) = (uint32_t)((*(uint64_t*)(p))>>32); (data) = (uint32_t)((*(uint64_t*)(p)));
 
void arch_set_trap(ucontext_t *uc, uint64_t *state)
{
  // we assume that all relevant instructions are 4 bytes
  // otherwise this will end badly
  uint32_t *target = (uint32_t*)(uc->uc_mcontext.__gregs[REG_PC] + 4); 

  if (state) {
    uint32_t old = *target;
    ENCODE(state,*target,2);  // "2"=> we are stashing the old instruction
    *target = BRK_INSTR;
    __builtin___clear_cache(target,((void*)target)+4);
    DEBUG("breakpoint instruction (%08x) inserted at %p overwriting %08x (state %016lx)\n",*target, target,old,*state);
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
  arch_fp_csr_t f;

  if (get_fpcsr(uc,&f)) {
    ERROR("failed to get fpcsr from context\n");
    return;
  }

  f.val &= ~FLAG_MASK;

  if (set_fpcsr(uc,&f)) {
    ERROR("failed to set fpcsr from context\n");
    return;
  }
}

void arch_mask_fp_traps(ucontext_t *uc)
{
  arch_fp_csr_t f;

  if (get_fpcsr(uc,&f)) {
    ERROR("failed to get fpcsr from context\n");
    return;
  }

  f.val &= ~ENABLE_MASK;

  if (set_fpcsr(uc,&f)) {
    ERROR("failed to set fpcsr from context\n");
    return;
  }
}

void arch_unmask_fp_traps(ucontext_t *uc)
{
  arch_fp_csr_t f;

  if (get_fpcsr(uc,&f)) {
    ERROR("failed to get fpcsr from context\n");
    return;
  }

  f.val |= ENABLE_MASK;

  if (set_fpcsr(uc,&f)) {
    ERROR("failed to set fpcsr from context\n");
    return;
  }

}  

#define FCSR_ROUND_MASK (0x70UL)

fpspy_round_config_t arch_get_machine_round_config(void)
{
  uint64_t fcsr =  get_fcsr_machine();
  uint32_t fcsr_round = fcsr &  FCSR_ROUND_MASK;
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
  // do better than this guess
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
