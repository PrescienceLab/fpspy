static unsigned char log_level = 2;

#define DEBUG(S, ...) do { if (log_level > 1) {fprintf(stderr, "fpspy: debug(%8d): " S, gettid(), ##__VA_ARGS__);} } while (0)
#define INFO(S, ...) fprintf(stderr,  "fpspy: info(%8d): " S, gettid(), ##__VA_ARGS__)
#define ERROR(S, ...) fprintf(stderr, "fpspy: ERROR(%8d): " S, gettid(), ##__VA_ARGS__)
