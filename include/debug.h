#pragma once

extern unsigned char log_level;

#undef DO_DEBUG
#undef FORCE_DEBUG
#if defined(CONFIG_NO_OUTPUT)
  #if CONFIG_NO_OUTPUT
    #define DO_DEBUG    0
    #define FORCE_DEBUG 0
  #else
    #define DO_DEBUG    1
    #if defined(CONFIG_FORCE_DEBUG_OUTPUT)
      #define FORCE_DEBUG CONFIG_FORCE_DEBUG_OUTPUT
    #else
      #define FORCE_DEBUG 0
    #endif
  #endif
#else
  #if defined(CONFIG_NO_DEBUG_OUTPUT)
    #define DO_DEBUG (!CONFIG_NO_DEBUG_OUTPUT)
    #if defined(CONFIG_FORCE_DEBUG_OUTPUT)
      #define FORCE_DEBUG CONFIG_FORCE_DEBUG_OUTPUT
    #else
      #define FORCE_DEBUG 0
    #endif
  #endif
#endif

#if CONFIG_NO_OUTPUT
#define DEBUG(S, ...)
#define INFO(S, ...)
#define ERROR(S, ...)
#else
#define DEBUG(S, ...)                                                    \
  do {                                                                   \
    if (FORCE_DEBUG || (DO_DEBUG && log_level >= 1)) {                   \
      fprintf(stderr, "fpspy: debug(%8d): " S, gettid(), ##__VA_ARGS__); \
    }                                                                    \
  } while (0)
#define INFO(S, ...) fprintf(stderr, "fpspy: info(%8d): " S, gettid(), ##__VA_ARGS__)
#define ERROR(S, ...) fprintf(stderr, "fpspy: ERROR(%8d): " S, gettid(), ##__VA_ARGS__)
#endif
