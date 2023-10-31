#if DEBUG_OUTPUT
#define DEBUG(S, ...) fprintf(stderr, "fpspy: debug(%8d): " S, gettid(), ##__VA_ARGS__)
#else 
#define DEBUG(S, ...) 
#endif

#if NO_OUTPUT
#define INFO(S, ...) 
#define ERROR(S, ...)
#else
#define INFO(S, ...) fprintf(stderr,  "fpspy: info(%8d): " S, gettid(), ##__VA_ARGS__)
#define ERROR(S, ...) fprintf(stderr, "fpspy: ERROR(%8d): " S, gettid(), ##__VA_ARGS__)
#endif
