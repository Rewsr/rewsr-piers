#include "affinity.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

void rewsr_cpuset_zero(struct rewsr_cpuset *set) {
    memset(set->bits, 0, sizeof set->bits);
}

void rewsr_cpuset_set(struct rewsr_cpuset *set, int cpu) {
    if (cpu < 0 || cpu >= REWSR_CPUSET_MAX) {
        return;
    }
    set->bits[cpu / 64] |= (uint64_t)1 << (cpu % 64);
}

void rewsr_cpuset_clr(struct rewsr_cpuset *set, int cpu) {
    if (cpu < 0 || cpu >= REWSR_CPUSET_MAX) {
        return;
    }
    set->bits[cpu / 64] &= ~((uint64_t)1 << (cpu % 64));
}

int rewsr_cpuset_isset(const struct rewsr_cpuset *set, int cpu) {
    if (cpu < 0 || cpu >= REWSR_CPUSET_MAX) {
        return 0;
    }
    return (set->bits[cpu / 64] >> (cpu % 64)) & 1;
}

int rewsr_cpuset_count(const struct rewsr_cpuset *set) {
    int count = 0;
    for (int w = 0; w < REWSR_CPUSET_WORDS; w++) {
        uint64_t b = set->bits[w];
        while (b) {
            b &= b - 1; /* clear the lowest set bit */
            count++;
        }
    }
    return count;
}

int rewsr_cpuset_first(const struct rewsr_cpuset *set) {
    for (int w = 0; w < REWSR_CPUSET_WORDS; w++) {
        if (set->bits[w] != 0) {
            for (int b = 0; b < 64; b++) {
                if ((set->bits[w] >> b) & 1) {
                    return w * 64 + b;
                }
            }
        }
    }
    return -1;
}

int rewsr_cpuset_parse(struct rewsr_cpuset *set, const char *list) {
    rewsr_cpuset_zero(set);
    if (list == NULL) {
        return -1;
    }

    const char *p = list;
    while (*p) {
        /* Each comma-separated token is either a single CPU or a lo-hi
         * range. Parse the low bound. */
        char *end;
        long lo = strtol(p, &end, 10);
        if (end == p || lo < 0 || lo >= REWSR_CPUSET_MAX) {
            return -1;
        }
        long hi = lo;
        p = end;
        if (*p == '-') {
            p++;
            hi = strtol(p, &end, 10);
            if (end == p || hi < lo || hi >= REWSR_CPUSET_MAX) {
                return -1;
            }
            p = end;
        }
        for (long c = lo; c <= hi; c++) {
            rewsr_cpuset_set(set, (int)c);
        }
        if (*p == ',') {
            p++;
        } else if (*p != '\0') {
            return -1;
        }
    }
    return 0;
}

#if defined(__linux__)

#define _GNU_SOURCE
#include <sched.h>

/* to_cpu_set copies our fixed bitmap into the glibc cpu_set_t the syscall
 * wrapper expects. */
static void to_cpu_set(const struct rewsr_cpuset *set, cpu_set_t *out) {
    CPU_ZERO(out);
    for (int cpu = 0; cpu < REWSR_CPUSET_MAX; cpu++) {
        if (rewsr_cpuset_isset(set, cpu)) {
            CPU_SET(cpu, out);
        }
    }
}

int rewsr_affinity_set(const struct rewsr_cpuset *set) {
    cpu_set_t cs;
    to_cpu_set(set, &cs);
    if (sched_setaffinity(0, sizeof cs, &cs) != 0) {
        return errno != 0 ? -errno : -1;
    }
    return 0;
}

int rewsr_affinity_pin(int cpu) {
    struct rewsr_cpuset set;
    rewsr_cpuset_zero(&set);
    rewsr_cpuset_set(&set, cpu);
    return rewsr_affinity_set(&set);
}

int rewsr_affinity_get(struct rewsr_cpuset *set) {
    cpu_set_t cs;
    CPU_ZERO(&cs);
    if (sched_getaffinity(0, sizeof cs, &cs) != 0) {
        return errno != 0 ? -errno : -1;
    }
    rewsr_cpuset_zero(set);
    for (int cpu = 0; cpu < REWSR_CPUSET_MAX; cpu++) {
        if (CPU_ISSET(cpu, &cs)) {
            rewsr_cpuset_set(set, cpu);
        }
    }
    return 0;
}

#else /* !__linux__ */

int rewsr_affinity_set(const struct rewsr_cpuset *set) {
    (void)set;
    return -ENOTSUP;
}

int rewsr_affinity_pin(int cpu) {
    (void)cpu;
    return -ENOTSUP;
}

int rewsr_affinity_get(struct rewsr_cpuset *set) {
    (void)set;
    return -ENOTSUP;
}

#endif /* __linux__ */
