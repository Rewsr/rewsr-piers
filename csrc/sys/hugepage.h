#ifndef REWSR_SYS_HUGEPAGE_H
#define REWSR_SYS_HUGEPAGE_H

#include <stddef.h>
#include <stdint.h>

/* hugepage backs the UMEM and other large data-plane buffers with huge
 * pages when the host has them. Huge pages cut TLB pressure on the packet
 * buffer, which is one of the few knobs that visibly moves small-packet
 * throughput. The meminfo parser (how many huge pages exist and their
 * size) is pure string work and is tested; the actual MAP_HUGETLB mapping
 * is Linux only and gated at runtime, with a plain anonymous mapping as the
 * portable fallback so callers always get usable memory. */

struct rewsr_hugepage_info {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t page_size_kb;
};

/* rewsr_hugepage_parse_meminfo pulls the HugePages_Total, HugePages_Free,
 * and Hugepagesize fields out of a /proc/meminfo buffer into out. Returns 0
 * on success (even if some fields were absent, which leaves them zero), -1
 * on a NULL buffer. */
int rewsr_hugepage_parse_meminfo(const char *buf, size_t len,
                                 struct rewsr_hugepage_info *out);

/* rewsr_hugepage_default_size_kb returns the huge page size in KiB from
 * /proc/meminfo, or 0 if it cannot be read (non-Linux, or no huge page
 * support). */
uint64_t rewsr_hugepage_default_size_kb(void);

/* rewsr_hugepage_alloc maps len bytes backed by huge pages when available,
 * falling back to a normal anonymous mapping otherwise. *used_hugepages is
 * set to 1 if the huge-page mapping succeeded, 0 if the fallback was used.
 * Returns the mapping, or NULL on failure. Free with rewsr_hugepage_free. */
void *rewsr_hugepage_alloc(size_t len, int *used_hugepages);

/* rewsr_hugepage_free unmaps a region from rewsr_hugepage_alloc. */
void rewsr_hugepage_free(void *addr, size_t len);

#endif /* REWSR_SYS_HUGEPAGE_H */
