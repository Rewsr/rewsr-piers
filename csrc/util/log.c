#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static enum rewsr_log_level g_level = REWSR_LOG_INFO;

static const char *level_tag(enum rewsr_log_level level) {
    switch (level) {
    case REWSR_LOG_ERROR:
        return "ERROR";
    case REWSR_LOG_WARN:
        return "WARN";
    case REWSR_LOG_INFO:
        return "INFO";
    case REWSR_LOG_DEBUG:
        return "DEBUG";
    default:
        return "?";
    }
}

void rewsr_log_init(void) {
    const char *env = getenv("REWSR_LOG_LEVEL");
    if (env == NULL) {
        g_level = REWSR_LOG_INFO;
        return;
    }
    if (strcmp(env, "error") == 0) {
        g_level = REWSR_LOG_ERROR;
    } else if (strcmp(env, "warn") == 0) {
        g_level = REWSR_LOG_WARN;
    } else if (strcmp(env, "debug") == 0) {
        g_level = REWSR_LOG_DEBUG;
    } else {
        g_level = REWSR_LOG_INFO;
    }
}

void rewsr_log_set_level(enum rewsr_log_level level) { g_level = level; }

enum rewsr_log_level rewsr_log_level_get(void) { return g_level; }

/* now_millis returns a monotonic millisecond timestamp for log lines. It
 * falls back to the realtime clock if the monotonic clock is unavailable,
 * which only affects the printed timestamp, not any timing logic. */
static long long now_millis(void) {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }
#endif
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }
    return 0;
}

void rewsr_logf(enum rewsr_log_level level, const char *fmt, ...) {
    if (level > g_level) {
        return;
    }
    fprintf(stderr, "[%lld] %-5s ", now_millis(), level_tag(level));

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fputc('\n', stderr);
}
