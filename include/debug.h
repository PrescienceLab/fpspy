#pragma once

extern unsigned char log_level;

#define DEBUG(S, ...) do { if (!CONFIG_NO_DEBUG_OUTPUT && log_level > 1) {fprintf(stderr, "fpspy: debug(%8d): " S, gettid(), ##__VA_ARGS__);} } while (0)
#define INFO(S, ...) fprintf(stderr,  "fpspy: info(%8d): " S, gettid(), ##__VA_ARGS__)
#define ERROR(S, ...) fprintf(stderr, "fpspy: ERROR(%8d): " S, gettid(), ##__VA_ARGS__)
