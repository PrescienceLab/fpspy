#ifndef __ARCH
#define __ARCH


/*
  This is the architectural interface for fpspy
 */

typedef uint32_t fpspy_round_config_t;

typedef enum {
  FPSPY_ROUND_NEAREST=0,
  FPSPY_ROUND_NEGATIVE=1,
  FPSPY_ROUND_POSITIVE=2,
  FPSPY_ROUND_ZERO=3
} fpspy_round_mode_t;

typedef enum {
  FPSPY_ROUND_NO_DAZ_NO_FTZ=0,
  FPSPY_ROUND_NO_DAZ_FTZ=1,
  FPSPY_ROUND_DAZ_NO_FTZ=2,
  FPSPY_ROUND_DAZ_FTZ=3
} fpspy_dazftz_mode_t;


// arch-specific structures and inline functions 
// see else for what is expected
//
//

#if defined(x64)
#include "x64/x64.h"
#elif defined(arm64)
#include "arm64/arm64.h"
#else
// generic
// generic versions - should be included in the architecture include
union arch_fp_csr {} ;

uint64_t arch_cycle_count(void);
void     arch_get_fp_csr(union arch_fp_csr *f);
void     arch_set_fp_csr(const union arch_fp_csr *f);

// snapshot fp csr and config it for FP *within* FPSpy
// returns old fp csr - later use set to restore
void     arch_config_fp_csr_for_local(union arch_fp_csr *old);

void     arch_dump_fp_csr(FILE *out, const char *prefix, const ucontext_t *uc);
void     arch_dump_gp_csr(FILE *out, const char *prefix, const ucontext_t *uc);

// trap on the next instruction after the one in the ucontext
// state points to a location that information can be stored in
// state can be used to stash an instruction in a patch model,
// for example
void     arch_set_trap(ucontext_t *uc, uint64_t *state);
// disable the trap for the current instruction
// previous state is passed back in
void     arch_reset_trap(ucontext_t *uc, uint64_t *state);

void arch_set_trap(ucontext_t *uc, uint64_t *state)
{
  uc->uc_mcontext.gregs[REG_EFL] |= 0x100UL;
}

void arch_reset_trap(ucontext_t *uc, uint64_t *state)
{
  uc->uc_mcontext.gregs[REG_EFL] &= ~0x100UL; 
}


void arch_clear_fp_exceptions(ucontext_t *uc);
void arch_mask_fp_traps(ucontext_t *uc);
void arch_unmask_fp_traps(ucontext_t *uc);

fpspy_round_config_t arch_get_round_config(ucontext_t *uc);
void                 arch_set_round_config(ucontext_t *uc, fpspy_round_config_t newconfig);


fpspy_round_mode_t     arch_get_round_mode(fpspy_round_config_t config);
void                   arch_set_round_mode(fpspy_round_config_t  *config, fpspy_round_mode_t mode);
fpspy_dazftz_mode_t    arch_get_daz_ftz_mode(fpspy_round_config_t config);
void                   arch_set_dazftz_mode(fpspy_round_config_t *config, fpspy_dazftz_mode_t mode);


#endif

#endif

