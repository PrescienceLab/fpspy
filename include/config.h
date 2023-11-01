// whether DEBUG()s print or not
#define DEBUG_OUTPUT 1
// whether to eliminate all output
#define NO_OUTPUT 0

// limits on the timing-based sampler, to make sure
// that we don't generate random intervals that make no sense
// given the limits of the kernel
#define MAX_US_ON 10000
#define MAX_US_OFF 1000000

// maximum number of contexts (threads) that we can support in a process
#define MAX_CONTEXTS 1024
