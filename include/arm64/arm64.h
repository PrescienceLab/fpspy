#ifndef __ARM64
#define __ARM64

typedef union  {
  uint32_t val;
  struct {
    uint8_t ioc:1;  // detected nan
    uint8_t dzc:1;  // detected divide by zero
    uint8_t ofc:1;  // detected overflow (infinity)
    uint8_t ufc:1;  // detected underflow (zero)
    uint8_t ixc:1;  // detected precision (rounding)
    uint8_t dnm1:2; // do not modify bits (UNK/SBZP)
    uint8_t idc:1;  // detected input denormals
    uint8_t ioe:1;  // enable nan exceptions
    uint8_t dze:1;  // enable divide by zero traps
    uint8_t ofe:1;  // enable overflow traps
    uint8_t ufe:1;  // enable underflow traps
    uint8_t ixe:1;  // enable precision (rounding) traps
    uint8_t dnm2:2; // do not modify bits (UNK/SBZP)
    uint8_t ide:1;  // enable input denormal traps
    uint8_t len:3;  // length (ignored by ASIMD+)
    uint8_t dnm3:1; // do not modify bits (UNK/SBZP)
    uint8_t stride:2; // stride (ignored by ASIMD+)
    uint8_t rounding:2; // rounding (toward 00=>nearest,01=>positive,10=>negative,11=>zero)
                        // note RP/RN reversed compared to intel, because why not
    uint8_t fz:1;   // flush to zero (denormals are zeros) (ignored by ASIMD+)
    uint8_t dn:1;   // nan propagation mode 0=>normal, 1=>produce default nan if fed nans
                    // (ignored by ASIMD+)
    uint8_t ahp:1;  // alt half precision 0=>use IEEE F16, 1=>use whatever we tossed in
    uint8_t qc:1;   // cumulative saturation (ASIMD sticky cc for integer saturating arithmetic)
                    // the following comparison outputs would be put into
                    // rflags on x64 - note that you can use ioc to detect unordered
    uint8_t v:1;    // overflow condition on comparison
    uint8_t c:1;    // carry condition on comparsion
    uint8_t z:1;    // zero condition on comparison
    uint8_t n:1;    // negative condition on comparison
  } __attribute__((packed));
} __attribute__((packed)) fpscr_t;

typedef fpscr_t arch_fp_csr_t;

// this is the "pstate", which is provided by Linux, but
// is actually an amalgam of fields from different registers internally
typedef union {
  uint32_t val;
  struct {
    uint8_t m:4;   // mode?
    uint8_t m2:1;  // more mode?
    uint8_t res1:1;
    uint8_t f:1;   // fiq mask
    uint8_t i:1;   // irq mask
    uint8_t a:1;   // SError mask (?)
    uint8_t d:1;   // debug mask
    uint16_t res2:10;
    uint8_t il:1;  // illegal execution state (?)
    uint8_t ss:1;  // software single-step (trap mode)
    uint8_t res3:6;
    uint8_t v:1;   // overflow cc
    uint8_t c:1;   // carry cc
    uint8_t z:1;   // zero cc
    uint8_t n:1;   // negative cc
  } __attribute__((packed));
} __attribute__((packed)) pstate_t;

typedef pstate_t arch_gp_csr_t;


static inline uint64_t __attribute__((always_inline)) arch_cycle_count(void)
{
  ERROR("cycle count is not implemented yet\n");
  return -1;
}

// the DENORM trap is also available on arm64
#define  FE_DENORM 0x1000
void arch_clear_trap_mask(void);
void arch_set_trap_mask(int which);
void arch_reset_trap_mask(int which);


uint64_t arch_cycle_count(void);
void     arch_get_machine_fp_csr(arch_fp_csr_t *f);
void     arch_set_machine_fp_csr(const arch_fp_csr_t *f);

void     arch_config_machine_fp_csr_for_local(arch_fp_csr_t *old);

// detects only FE_DENORM (within the HW state)
int      arch_have_special_fp_csr_exception(int which);

void     arch_dump_gp_csr(const char *pre, const ucontext_t *uc);
void     arch_dump_fp_csr(const char *pre, const ucontext_t *uc);

void arch_set_trap(ucontext_t *uc, uint64_t *state);
void arch_reset_trap(ucontext_t *uc, uint64_t *state);

void arch_clear_fp_exceptions(ucontext_t *uc);

void arch_mask_fp_traps(ucontext_t *uc);
void arch_unmask_fp_traps(ucontext_t *uc);

fpspy_round_config_t arch_get_machine_round_config(void);

fpspy_round_config_t arch_get_round_config(ucontext_t *uc);
void                 arch_set_round_config(ucontext_t *uc, fpspy_round_config_t config);

fpspy_round_mode_t   arch_get_round_mode(fpspy_round_config_t config);
void                 arch_set_round_mode(fpspy_round_config_t  *config, fpspy_round_mode_t mode);

fpspy_dazftz_mode_t  arch_get_dazftz_mode(fpspy_round_config_t *config);
void                 arch_set_dazftz_mode(fpspy_round_config_t *config, fpspy_dazftz_mode_t mode);


uint64_t arch_get_fp_csr(const ucontext_t *uc);
uint64_t arch_get_gp_csr(const ucontext_t *uc);
uint64_t arch_get_ip(const ucontext_t *uc);
uint64_t arch_get_sp(const ucontext_t *uc);


int  arch_process_init(void);
void arch_process_deinit(void);

int  arch_thread_init(ucontext_t *uc);
void arch_thread_deinit(void);


#endif
