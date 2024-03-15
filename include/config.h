// whether DEBUG()s print or not
#define DEBUG_OUTPUT 0
// whether to eliminate all output
#define NO_OUTPUT 1

// limits on the timing-based sampler, to make sure
// that we don't generate random intervals that make no sense
// given the limits of the kernel
#define MAX_US_ON 10000
#define MAX_US_OFF 1000000

// maximum number of contexts (threads) that we can support in a process
#define MAX_CONTEXTS 1024


// support for FPVM kernel module
#define CONFIG_TRAP_SHORT_CIRCUITING 1

#if !defined(x64) && CONFIG_TRAP_SHORT_CIRCUITING
#warning Disabling short circuiting as it is not available on this architecture
#undef CONFIG_TRAP_SHORT_CIRCUITING
#define CONFIG_TRAP_SHORT_CIRCUITING 0
#endif
