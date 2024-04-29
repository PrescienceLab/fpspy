static int quiet = 0;

#if DEBUG_OUTPUT
#define DEBUG(S, ...) if (!quiet) {fprintf(stderr, "fpspy: debug(%8d): " S, gettid(), ##__VA_ARGS__);}
#else 
#define DEBUG(S, ...) 
#endif

#if NO_OUTPUT
#define INFO(S, ...) 
#define ERROR(S, ...)
#else
#define INFO(S, ...) if (!quiet) {fprintf(stderr,  "fpspy: info(%8d): " S, gettid(), ##__VA_ARGS__);}
#define ERROR(S, ...) if (!quiet) {fprintf(stderr, "fpspy: ERROR(%8d): " S, gettid(), ##__VA_ARGS__);}
#endif
