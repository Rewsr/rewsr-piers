#ifndef REWSR_SYS_AFFINITY_H
#define REWSR_SYS_AFFINITY_H

#include <stddef.h>
#include <stdint.h>

/* affinity pins the data-plane threads to specific CPUs. A packet path that
 * migrates between cores loses its warm caches and, worse, may drift onto a
 * core on the wrong NUMA node from the NIC, so pinning is not optional for
 * predictable latency. The CPU-set bitmap and the cpulist parser are pure
 * and unit tested; the sched_setaffinity call that applies a set is Linux
 * only and gated at runtime.
 *
 * The set is a fixed-size bitmap over REWSR_CPUSET_MAX logical CPUs, which
 * covers the largest hosts this fabric runs on without a heap allocation. */

#define REWSR_CPUSET_MAX 1024
#define REWSR_CPUSET_WORDS (REWSR_CPUSET_MAX / 64)

struct rewsr_cpuset {
    uint64_t bits[REWSR_CPUSET_WORDS];
};

/* rewsr_cpuset_zero clears every CPU from the set. */
void rewsr_cpuset_zero(struct rewsr_cpuset *set);

/* rewsr_cpuset_set / _clr add or remove one CPU. Out-of-range CPUs are
 * ignored. */
void rewsr_cpuset_set(struct rewsr_cpuset *set, int cpu);
void rewsr_cpuset_clr(struct rewsr_cpuset *set, int cpu);

/* rewsr_cpuset_isset reports whether a CPU is in the set. */
int rewsr_cpuset_isset(const struct rewsr_cpuset *set, int cpu);

/* rewsr_cpuset_count returns how many CPUs are in the set. */
int rewsr_cpuset_count(const struct rewsr_cpuset *set);

/* rewsr_cpuset_first returns the lowest CPU in the set, or -1 if empty. */
int rewsr_cpuset_first(const struct rewsr_cpuset *set);

/* rewsr_cpuset_parse fills set from a Linux cpulist string like
 * "0-3,8,12-15". Returns 0 on success, -1 on a malformed list. */
int rewsr_cpuset_parse(struct rewsr_cpuset *set, const char *list);

/* rewsr_affinity_pin restricts the calling thread to the single CPU given.
 * Linux only; returns -ENOTSUP-equivalent elsewhere or a negative errno. */
int rewsr_affinity_pin(int cpu);

/* rewsr_affinity_set applies an arbitrary CPU set to the calling thread.
 * Linux only. */
int rewsr_affinity_set(const struct rewsr_cpuset *set);

/* rewsr_affinity_get reads the calling thread's current CPU set. Linux
 * only. */
int rewsr_affinity_get(struct rewsr_cpuset *set);

#endif /* REWSR_SYS_AFFINITY_H */
