#define _GNU_SOURCE
#include <sys/ptrace.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <ucontext.h>
#include <fenv.h>
#include <string.h>

#include "config.h"
#include "debug.h"
#include "arch.h"



// Which traps to enable - default all
// note that these are ENABLES instead of MASKS, hence the ~
//
// bits 9..13 are the default IEEE ones, then bit 16 is the denorm
//
//        1 0011 1110 0000 0000 => 
static int fpscr_enable_base = 0x13e00; 

#define FPSCR_FLAG_MASK     (fpscr_enable_base>>9)
#define FPSCR_ENABLE_MASK   fpscr_enable_base

// clearing the mask => enable all
void arch_clear_trap_mask(void)
{
  fpscr_enable_base = 0x13e00;
}

void arch_set_trap_mask(int which)
{
  switch (which) {
  case FE_INVALID:
    fpscr_enable_base &= ~0x200;   // bit 9  IOE
    break;
  case FE_DENORM:
    fpscr_enable_base &= ~0x10000; // bit 16 IDE
    break;
  case FE_DIVBYZERO:
    fpscr_enable_base &= ~0x400;   // bit 10 DZE
    break;
  case FE_OVERFLOW:
    fpscr_enable_base &= ~0x800;   // bit 11 OFE
    break;
  case FE_UNDERFLOW:
    fpscr_enable_base &= ~0x1000;  // bit 12 UFE
    break;
  case FE_INEXACT:
    fpscr_enable_base &= ~0x2000;  // bit 13 IXE
    break;
  }
}

void arch_reset_trap_mask(int which)
{
  switch (which) {
  case FE_INVALID:
    fpscr_enable_base |= 0x200;   // bit 9  IOE
    break;
  case FE_DENORM:
    fpscr_enable_base |= 0x10000; // bit 16 IDE
    break;
  case FE_DIVBYZERO:
    fpscr_enable_base |= 0x400;   // bit 10 DZE
    break;
  case FE_OVERFLOW:
    fpscr_enable_base |= 0x800;   // bit 11 OFE
    break;
  case FE_UNDERFLOW:
    fpscr_enable_base |= 0x1000;  // bit 12 UFE
    break;
  case FE_INEXACT:
    fpscr_enable_base |= 0x2000;  // bit 13 IXE
    break;
  }
}



// FPCSR used when *we* are executing floating point code
// All masked, flags zeroed, round nearest, special features off
#define FPSCR_OURS   0x0


static uint32_t get_fpscr()
{
  uint32_t val=0;
  __asm__ __volatile__ ("vmrs %0, fpscr" : "=r"(val) : : "memory" );
  return val;
}

static void set_fpscr(uint32_t val)
{
  __asm__ __volatile__ ("vmsr fpscr, %0" : : "r"(val) : "memory" );
}

void arch_get_machine_fp_csr(arch_fp_csr_t *f)
{
  f->val = get_fpscr();
}

void arch_set_machine_fp_csr(const arch_fp_csr_t *f)
{
  set_fpscr(f->val);
}

void arch_config_machine_fp_csr_for_local(arch_fp_csr_t *old)
{
  arch_get_machine_fp_csr(old);
  set_fpscr(FPSCR_OURS);
}

int      arch_have_special_fp_csr_exception(int which)
{
  if (which==FE_DENORM) {
    return !!(get_fpscr() & 0x100); // bit 8, IDC
  } else {
    return 0;
  }
}

void arch_dump_gp_csr(const char *prefix, const ucontext_t *uc)
{
  char buf[256];
  
  pstate_t *p = (pstate_t *)&(uc->uc_mcontext.pstate);
  
  sprintf(buf, "pstate = %08x", p->val);
  
#define EF(x,y) if (p->x) { strcat(buf, " " #y); }
  
  EF(z,zero);
  EF(n,neg);
  EF(c,carry);
  EF(v,over);
  EF(ss,singlestep);
  EF(a,serror);
  EF(d,debug);
  EF(f,fiqmask);
  EF(i,irqmask);
  
  DEBUG("%s: %s\n",prefix,buf);
}


#warning PAD NEEDS TO FIND WHERE THIS IS FOR REAL
#warning PAD THIS IS HACKED TOGETHER JUST TO COMPILE FOR NOW
#define FPSCR(uc) ((uint32_t*)(uc->uc_mcontext.__reserved+8))

void arch_dump_fp_csr(const char *pre, const ucontext_t *uc)
{
  char buf[256];
  
  fpscr_t *f = (fpscr_t *)FPSCR(uc);
  
  sprintf(buf,"fpcsr = %08x flags:", f->val);
  
#define MF(x,y) if (f->x) { strcat(buf, " " #y); }
  
  MF(ioc,NAN);
  MF(idc,DENORM);
  MF(dzc,ZERO);
  MF(ofc,OVER);
  MF(ufc,UNDER);
  MF(ixc,PRECISION);

  strcat(buf," enables:");

  MF(ioe,nan);
  MF(ide,denorm);
  MF(dze,zero);
  MF(ofe,over);
  MF(ufe,under);
  MF(ixe,precision);

  strcat(buf," compares:");

  MF(z,zero);
  MF(n,neg);
  MF(c,carry);
  MF(v,over);
    
  DEBUG("%s: %s rounding: %s %s\n",pre,buf,
	f->rounding == 0 ? "nearest" :
	f->rounding == 1 ? "negative" :
	f->rounding == 2 ? "positive" : "zero",
	f->fz ? "FTZ" : "");

}
 
 
//  brk	#23
#define BRK_INSTR 0xd42002e0


#define ENCODE(p,inst,data) (*(uint64_t*)(p)) = ((((uint64_t)(inst))<<32)|((uint32_t)(data)))
#define DECODE(p,inst,data) (inst) = (uint32_t)((*(uint64_t*)(p))>>32); (data) = (uint32_t)((*(uint64_t*)(p)));
 
void arch_set_trap(ucontext_t *uc, uint64_t *state)
{
  uint32_t *target = (uint32_t*)(uc->uc_mcontext.pc + 4); // all instructions are 4 bytes

  if (state) { 
    ENCODE(state,*target,2);  // "2"=> we are stashing the old instruction
    *target = BRK_INSTR;
  } else {
    ERROR("no state on reset trap - just ignoring\n");
  } 
}

void arch_reset_trap(ucontext_t *uc, uint64_t *state)
{
  uint32_t *target = (uint32_t*)(uc->uc_mcontext.pc);

  if (state) {
    uint32_t flag;
    uint32_t instr;
    DECODE(state,instr,flag);

    if (flag!=2) {
      ERROR("Surprise state flag %x in reset trap\n",flag);
      return;
    } else {
      *target = instr;
    }
  } else {
    ERROR("no state on reset trap - just ignoring\n");
  }
}

void arch_clear_fp_exceptions(ucontext_t *uc)
{
  *(FPSCR(uc)) &= ~FPSCR_FLAG_MASK;
}

void arch_mask_fp_traps(ucontext_t *uc)
{
  *(FPSCR(uc)) &= ~FPSCR_ENABLE_MASK;
}

void arch_unmask_fp_traps(ucontext_t *uc)
{
  *(FPSCR(uc)) &= FPSCR_ENABLE_MASK;
}  


// RM = bits 23-24
// FTZ = bit 25
// there is no DAZ
#define FPSCR_ROUND_DAZ_FTZ_MASK ((0x380000))

fpspy_round_config_t arch_get_machine_round_config(void)
{
  uint32_t fpscr =  get_fpscr();
  uint32_t fpscr_round = fpscr & FPSCR_ROUND_DAZ_FTZ_MASK;
  return fpscr_round;
}

fpspy_round_config_t arch_get_round_config(ucontext_t *uc)
{
  uint32_t fpscr =  *(FPSCR(uc));
  uint32_t fpscr_round = fpscr & FPSCR_ROUND_DAZ_FTZ_MASK;
  DEBUG("fpscr (0x%08x) round faz dtz at 0x%08x\n", fpscr, fpscr_round);
  arch_dump_fp_csr("arch_get_round_config", uc);
  return fpscr_round;
}

void arch_set_round_config(ucontext_t *uc, fpspy_round_config_t config)
{
  *(FPSCR(uc)) &= ~FPSCR_ROUND_DAZ_FTZ_MASK;
  *(FPSCR(uc)) |= config;
  DEBUG("fpscr masked to 0x%08x after round daz ftz update (0x%08x)\n",*(FPSCR(uc)), config);
  arch_dump_fp_csr("arch_set_round_config", uc);
}

fpspy_round_mode_t     arch_get_round_mode(fpspy_round_config_t config)
{
  switch ((config>>22) & 0x3) {
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
  }
}

void                   arch_set_round_mode(fpspy_round_config_t  *config, fpspy_round_mode_t mode)
{
  *config &= (~0xc00000);
  switch (mode) {
  case FPSPY_ROUND_NEAREST:
    *config |= 0x0; // zero
    break;
  case FPSPY_ROUND_POSITIVE:
    *config |= 0x400000; // one
    break;
  case FPSPY_ROUND_NEGATIVE:
    *config |= 0x800000; // two
    break;
  case FPSPY_ROUND_ZERO:
    *config |= 0xc00000; // three
    break;
  }
}

fpspy_dazftz_mode_t    arch_get_dazftz_mode(fpspy_round_config_t *config)
{
  if (*config & 0x1000000) {
    return FPSPY_ROUND_NO_DAZ_FTZ;
  } else {
    return FPSPY_ROUND_NO_DAZ_NO_FTZ;
  }
}

void arch_set_dazftz_mode(fpspy_round_config_t *config, fpspy_dazftz_mode_t mode)
{
  *config &= ~0x1000000;
  if (mode==FPSPY_ROUND_DAZ_FTZ || mode==FPSPY_ROUND_NO_DAZ_FTZ) {
    *config |= 0x1000000;
  }
}


uint64_t arch_get_ip(const ucontext_t *uc)
{
  return uc->uc_mcontext.pc;
}

uint64_t arch_get_sp(const ucontext_t *uc)
{
  return uc->uc_mcontext.sp;
}

uint64_t arch_get_gp_csr(const ucontext_t *uc)
{
  return uc->uc_mcontext.pstate;
}

uint64_t arch_get_fp_csr(const ucontext_t *uc)
{
  return *(FPSCR(uc));
}


int  arch_process_init(void)
{
  DEBUG("arm64 process init\n");
  return 0;
}

void arch_process_deinit(void)
{
  DEBUG("arm64 process deinit\n");
}

int  arch_thread_init(ucontext_t *uc)
{
  DEBUG("arm64 thread init\n");
  return 0;
}

void arch_thread_deinit(void)
{
  DEBUG("arm64 thread deinit\n");
}
