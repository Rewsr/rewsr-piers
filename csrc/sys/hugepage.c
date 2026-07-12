#include "hugepage.h"

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

/* parse_kv_u64 reads the first unsigned integer after the "key:" prefix on
 * a /proc/meminfo line, ignoring a trailing unit like " kB". Returns 1 if
 * the key matched and a value was read. */
static int parse_kv_u64(const char *line, const char *key, uint64_t *out) {
    size_t klen = strlen(key);
    if (strncmp(line, key, klen) != 0 || line[klen] != ':') {
        return 0;
    }
    const char *p = line + klen + 1;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    uint64_t v = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (uint64_t)(*p - '0');
        p++;
        digits++;
    }
    if (!digits) {
        return 0;
    }
    *out = v;
    return 1;
}

int rewsr_hugepage_parse_meminfo(const char *buf, size_t len,
                                 struct rewsr_hugepage_info *out) {
    if (buf == NULL) {
        return -1;
    }
    memset(out, 0, sizeof *out);

    /* Walk line by line. meminfo is small and well formed, but bound every
     * line copy by the buffer length so a stream without a trailing newline
     * is still safe. */
    size_t start = 0;
    char line[256];
    for (size_t i = 0; i <= len; i++) {
        if (i == len || buf[i] == '\n') {
            size_t n = i - start;
            if (n >= sizeof line) {
                n = sizeof line - 1;
            }
            memcpy(line, buf + start, n);
            line[n] = '\0';

            uint64_t v;
            if (parse_kv_u64(line, "HugePages_Total", &v)) {
                out->total_pages = v;
            } else if (parse_kv_u64(line, "HugePages_Free", &v)) {
                out->free_pages = v;
            } else if (parse_kv_u64(line, "Hugepagesize", &v)) {
                out->page_size_kb = v;
            }
            start = i + 1;
        }
    }
    return 0;
}

uint64_t rewsr_hugepage_default_size_kb(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (f == NULL) {
        return 0;
    }
    char buf[8192];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';

    struct rewsr_hugepage_info info;
    rewsr_hugepage_parse_meminfo(buf, n, &info);
    return info.page_size_kb;
}

/* MAP_HUGETLB is Linux only. Off Linux the symbol is undefined, so guard
 * the huge-page attempt and fall straight through to the anonymous
 * mapping, which every POSIX host supports. */
#ifndef MAP_ANONYMOUS
#ifdef MAP_ANON
#define MAP_ANONYMOUS MAP_ANON
#else
#define MAP_ANONYMOUS 0
#endif
#endif

void *rewsr_hugepage_alloc(size_t len, int *used_hugepages) {
    if (used_hugepages != NULL) {
        *used_hugepages = 0;
    }

#ifdef MAP_HUGETLB
    void *hp = mmap(NULL, len, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (hp != MAP_FAILED) {
        if (used_hugepages != NULL) {
            *used_hugepages = 1;
        }
        return hp;
    }
    /* Huge pages unavailable or exhausted: fall back rather than fail, so a
     * host without a reserved pool still runs, just without the TLB win. */
#endif

    void *area = mmap(NULL, len, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (area == MAP_FAILED) {
        return NULL;
    }
    return area;
}

void rewsr_hugepage_free(void *addr, size_t len) {
    if (addr != NULL) {
        munmap(addr, len);
    }
}
