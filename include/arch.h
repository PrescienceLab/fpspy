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

//
// This is the "abstract" interface
//

// Implementation must let us define the set of exceptions that
// we want to have trap.   "which" is indicated using the regular
// fenv values (e.g., FE_DIVZERO, FE_INVALID, ..) plus others
// that might be supported, like FE_DENORM
//
// The idea is that these functions will be used at startup time
// to build an except/trap mask that then will be applied when we
// invoke arch_(un)mask_fp_traps() (see below)
#define  FE_DENORM 0x1000
void arch_clear_trap_mask(void);
void arch_set_trap_mask(int which);
void arch_reset_trap_mask(int which);


// Implementation needs to define a type for the FP control/status reg
typedef union arch_fp_csr {} arch_fp_csr_t;

// Implementation must let us get at raw machine state (opaque is fine)
uint64_t arch_cycle_count(void);
void     arch_get_machine_fp_csr(arch_fp_csr_t *f);
void     arch_set_machine_fp_csr(const arch_fp_csr_t *f);

// Implementation must tell us if it supports FP traps or not
int      arch_machine_supports_fp_traps(void);

// Implementation must let us disable all traps, etc, and set FP defaults
// so that we can perform FP ourselves within FPSpy when absolutely needed
void     arch_config_fp_csr_for_local(arch_fp_csr_t *old);

// Implementation should be able to tell us if any special exception
// (other than the fenv ones) has been noted.  For example FE_DENORM
int      arch_have_special_fp_csr_exception(int which);

// Implementation must let us dump FP and GP control/status regs
// and should dump them using the DEBUG() macro
void     arch_dump_gp_csr(const char *pre, const ucontext_t *uc);
void     arch_dump_fp_csr(const char *pre, const ucontext_t *uc);

// Implementation must let us trap on the *next* instruction after the
// current one in the ucontext.
// state points to a location where the implementation can
// stash state on a "set_trap" and then see it again on "reset_trap".
// If state==NULL, then the implementation should do the best it can
// If this happens, it is because of a surprise abort in FPSpy in which
// we cannot find the monitoring context of the thread.
void     arch_set_trap(ucontext_t *uc, uint64_t *state);
// disable the trap for the *current* instruction
void     arch_reset_trap(ucontext_t *uc, uint64_t *state);

// Implementation must allow us to clear all FP exceptions in the ucontext
void arch_clear_fp_exceptions(ucontext_t *uc);

// Implementation must allow us to mask and unmask FP traps in the ucontext
// The traps to use are set previously (see "trap_mask" above)
void arch_mask_fp_traps(ucontext_t *uc);
void arch_unmask_fp_traps(ucontext_t *uc);

// Implementation must allow us to get the FP rounding configuration from the
// hardware. This is opaque
fpspy_round_config_t arch_get_machine_round_config(void);

// Implementation must allow us to get/set the FP rounding configuration
// of the ucontext.  This is opaque
fpspy_round_config_t arch_get_round_config(ucontext_t *uc);
void                 arch_set_round_config(ucontext_t *uc, fpspy_round_config_t config);

// Implementation must allow us to interogate the opaque rounding config
// to get at the IEEE rounding mode.
fpspy_round_mode_t   arch_get_round_mode(fpspy_round_config_t config);
void                 arch_set_round_mode(fpspy_round_config_t  *config, fpspy_round_mode_t mode);

// Implementation must allow us to interogate the opaque rounding config
// to get at the DAZ and FTZ features of the hardware, if they are supported
fpspy_dazftz_mode_t  arch_get_dazftz_mode(fpspy_round_config_t *config);
void                 arch_set_dazftz_mode(fpspy_round_config_t *config, fpspy_dazftz_mode_t mode);

// Implementation must allow us to get at the raw FP and FP CSRs of the ucontext
// As well as the instruction pointer and stack pointer
uint64_t arch_get_fp_csr(const ucontext_t *uc);
uint64_t arch_get_gp_csr(const ucontext_t *uc);
uint64_t arch_get_ip(const ucontext_t *uc);
uint64_t arch_get_sp(const ucontext_t *uc);


// fill in dest with up to min(size,instruction size) instruction bytes
// then return the number of number of bytes read, or negative on error
int arch_get_instr_bytes(const ucontext_t *uc, uint8_t *dest, int size);


// Implementation is initialized at start of process.  It can
// veto by returning non-zero.   Implementation is also
// initialized/deinitialized on each thread.
int  arch_process_init(void);
void arch_process_deinit(void);

int  arch_thread_init(ucontext_t *uc); // uc can be null (for aggregate mode)
void arch_thread_deinit(void);

#endif

#endif

