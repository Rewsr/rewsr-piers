#include "ctest.h"

#include "../sys/hugepage.h"

/* A realistic /proc/meminfo excerpt with a 1 GiB huge page pool. */
static const char MEMINFO[] =
    "MemTotal:       527636480 kB\n"
    "MemFree:        401235968 kB\n"
    "MemAvailable:   488636416 kB\n"
    "HugePages_Total:      64\n"
    "HugePages_Free:       48\n"
    "HugePages_Rsvd:        0\n"
    "HugePages_Surp:        0\n"
    "Hugepagesize:    1048576 kB\n"
    "Hugetlb:        67108864 kB\n";

static int test_parse_meminfo(void) {
    struct rewsr_hugepage_info info;
    ASSERT_EQ_INT(0,
                  rewsr_hugepage_parse_meminfo(MEMINFO, sizeof MEMINFO - 1,
                                               &info));
    ASSERT_EQ_U64(64, info.total_pages);
    ASSERT_EQ_U64(48, info.free_pages);
    ASSERT_EQ_U64(1048576, info.page_size_kb);
    return 0;
}

static int test_parse_meminfo_without_hugepages(void) {
    const char *m = "MemTotal:  1024 kB\nMemFree:  512 kB\n";
    struct rewsr_hugepage_info info;
    ASSERT_EQ_INT(0, rewsr_hugepage_parse_meminfo(m, strlen(m), &info));
    ASSERT_EQ_U64(0, info.total_pages);
    ASSERT_EQ_U64(0, info.page_size_kb);
    return 0;
}

static int test_parse_meminfo_null(void) {
    struct rewsr_hugepage_info info;
    ASSERT_EQ_INT(-1, rewsr_hugepage_parse_meminfo(NULL, 0, &info));
    return 0;
}

static int test_alloc_falls_back_and_is_usable(void) {
    /* On this dev host there is no huge page pool, so alloc must fall back
     * to an ordinary mapping and still return writable memory. */
    int used_hp = -1;
    size_t len = 2 * 1024 * 1024;
    void *p = rewsr_hugepage_alloc(len, &used_hp);
    ASSERT_TRUE(p != NULL);
    ASSERT_TRUE(used_hp == 0 || used_hp == 1);

    /* Touch the whole region to prove it is real, writable memory. */
    unsigned char *bytes = p;
    for (size_t i = 0; i < len; i += 4096) {
        bytes[i] = (unsigned char)(i & 0xff);
    }
    for (size_t i = 0; i < len; i += 4096) {
        ASSERT_EQ_INT((int)(i & 0xff), bytes[i]);
    }
    rewsr_hugepage_free(p, len);
    return 0;
}

REWSR_TEST_MAIN("hugepage", {
    RUN_TEST(test_parse_meminfo);
    RUN_TEST(test_parse_meminfo_without_hugepages);
    RUN_TEST(test_parse_meminfo_null);
    RUN_TEST(test_alloc_falls_back_and_is_usable);
})
