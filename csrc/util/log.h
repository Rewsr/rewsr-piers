#ifndef REWSR_UTIL_LOG_H
#define REWSR_UTIL_LOG_H

#include <stdarg.h>

/* log is a minimal leveled logger for the C data plane. It writes to
 * stderr with a level tag and a monotonic-ish millisecond timestamp so a
 * benchmark or a running pier leaves a readable trace without pulling in a
 * logging library. The level threshold is process global and set once at
 * startup from an environment variable, so the hot path pays only a single
 * comparison for a suppressed message. */

enum rewsr_log_level {
    REWSR_LOG_ERROR = 0,
    REWSR_LOG_WARN = 1,
    REWSR_LOG_INFO = 2,
    REWSR_LOG_DEBUG = 3
};

/* rewsr_log_init sets the threshold from the REWSR_LOG_LEVEL environment
 * variable (error, warn, info, debug), defaulting to info. It is safe to
 * call more than once. */
void rewsr_log_init(void);

/* rewsr_log_set_level overrides the threshold directly, for tests and for
 * callers that configure logging themselves. */
void rewsr_log_set_level(enum rewsr_log_level level);

/* rewsr_log_level_get returns the current threshold. */
enum rewsr_log_level rewsr_log_level_get(void);

/* rewsr_logf emits a message at the given level if it meets the threshold.
 * The format is printf style. A newline is appended automatically. */
void rewsr_logf(enum rewsr_log_level level, const char *fmt, ...);

#define REWSR_ERROR(...) rewsr_logf(REWSR_LOG_ERROR, __VA_ARGS__)
#define REWSR_WARN(...) rewsr_logf(REWSR_LOG_WARN, __VA_ARGS__)
#define REWSR_INFO(...) rewsr_logf(REWSR_LOG_INFO, __VA_ARGS__)
#define REWSR_DEBUG(...) rewsr_logf(REWSR_LOG_DEBUG, __VA_ARGS__)

#endif /* REWSR_UTIL_LOG_H */
