#ifndef __X64
#define __X64

typedef union  {
    uint32_t val;
    struct {
	uint8_t ie:1;  // detected nan
	uint8_t de:1;  // detected denormal
	uint8_t ze:1;  // detected divide by zero
	uint8_t oe:1;  // detected overflow (infinity)
	uint8_t ue:1;  // detected underflow (zero)
	uint8_t pe:1;  // detected precision (rounding)
	uint8_t daz:1; // denormals become zeros
	uint8_t im:1;  // mask nan exceptions
	uint8_t dm:1;  // mask denorm exceptions
	uint8_t zm:1;  // mask zero exceptions
	uint8_t om:1;  // mask overflow exceptions
	uint8_t um:1;  // mask underflow exceptions
	uint8_t pm:1;  // mask precision exceptions
	uint8_t rounding:2; // rounding (toward 00=>nearest,01=>negative,10=>positive,11=>zero)
	uint8_t fz:1;  // flush to zero (denormals are zeros)
	uint16_t rest;  
    } __attribute__((packed));
} __attribute__((packed)) mxcsr_t;

typedef mxcsr_t arch_fp_csr_t;

typedef union {
    uint64_t val;
    struct {
	// note that not all of these are visible in user mode
	uint8_t cf:1;    // detected carry
	uint8_t res1:1;  // reserved MB1
	uint8_t pf:1;    // detected parity
	uint8_t res2:1;  // reserved
	uint8_t af:1;    // detected adjust (BCD math)
	uint8_t res3:1;  // resered
	uint8_t zf:1;    // detected zero
	uint8_t sf:1;    // detected negative
	uint8_t tf:1;    // trap enable flag (single stepping)
	uint8_t intf:1;    // interrupt enable flag
	uint8_t df:1;    // direction flag (1=down);
	uint8_t of:1;    // detected overflow
	uint8_t iopl:2;  // I/O privilege level (ring)
	uint8_t nt:1;    // nested task
	uint8_t res4:1;  // reserved
	uint8_t rf:1;    // resume flag;
	uint8_t vm:1;    // virtual 8086 mode
	uint8_t ac:1;    // alignment check enable
	uint8_t vif:1;   // virtual interrupt flag
	uint8_t vip:1;   // virtual interrupt pending;
	uint8_t id:1;    // have cpuid instruction
	uint16_t res5:10; // reserved
	uint32_t res6;    // nothing in top half of rflags yet
    } __attribute__((packed));
} __attribute__((packed)) rflags_t;

typedef rflags_t arch_gp_csr_t;


static inline uint64_t __attribute__((always_inline)) arch_cycle_count(void)
{
  uint32_t lo, hi;
  asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return lo | ((uint64_t)(hi) << 32);
}

void arch_clear_except_mask(void);
void arch_set_except_mask(int which);
void arch_reset_except_mask(int which);


uint64_t arch_cycle_count(void);
void     arch_get_machine_fp_csr(arch_fp_csr_t *f);
void     arch_set_machine_fp_csr(const arch_fp_csr_t *f);

void     arch_config_machine_fp_csr_for_local(arch_fp_csr_t *old);

// currently only the denorm exception is special
#define  FE_DENORM 0x1000
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

fpspy_dazftz_mode_t  arch_get_daz_ftz_mode(fpspy_round_config_t *config);
void                 arch_set_dazftz_mode(fpspy_round_config_t *config, fpspy_dazftz_mode_t mode);

uint64_t arch_get_fp_csr(const ucontext_t *uc);
uint64_t arch_get_ip(const ucontext_t *uc);
uint64_t arch_get_sp(const ucontext_t *uc);
  

#endif
