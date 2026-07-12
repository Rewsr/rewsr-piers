#ifndef REWSR_CTEST_H
#define REWSR_CTEST_H

/* ctest is a dependency-free test harness for the portable C in this
 * repo. It is header only so each test file is a self-contained
 * executable: define tests as functions returning int (0 on success,
 * nonzero on failure), list them in a REWSR_TEST_MAIN block, and the
 * generated main runs them all, prints a summary, and exits nonzero if
 * any failed. There is no fork per test and no global registration, so
 * the whole thing stays legible and links with nothing but libc. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* rewsr_ctest_fail_count is bumped by the assert macros. It is a plain
 * file-scope counter reset at the start of every test function by the
 * RUN_TEST driver, so each test reports its own pass/fail independently. */
static int rewsr_ctest_fail_count;

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("    ASSERT_TRUE failed: %s (%s:%d)\n", #cond, __FILE__,  \
                   __LINE__);                                                \
            rewsr_ctest_fail_count++;                                        \
        }                                                                    \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ_INT(want, got)                                             \
    do {                                                                     \
        long long w_ = (long long)(want);                                    \
        long long g_ = (long long)(got);                                     \
        if (w_ != g_) {                                                      \
            printf("    ASSERT_EQ_INT failed: %s == %s (want %lld, got "     \
                   "%lld) (%s:%d)\n",                                        \
                   #want, #got, w_, g_, __FILE__, __LINE__);                 \
            rewsr_ctest_fail_count++;                                        \
        }                                                                    \
    } while (0)

#define ASSERT_EQ_U64(want, got)                                             \
    do {                                                                     \
        uint64_t w_ = (uint64_t)(want);                                      \
        uint64_t g_ = (uint64_t)(got);                                       \
        if (w_ != g_) {                                                      \
            printf("    ASSERT_EQ_U64 failed: %s == %s (want %llu, got "     \
                   "%llu) (%s:%d)\n",                                        \
                   #want, #got, (unsigned long long)w_,                      \
                   (unsigned long long)g_, __FILE__, __LINE__);              \
            rewsr_ctest_fail_count++;                                        \
        }                                                                    \
    } while (0)

#define ASSERT_EQ_STR(want, got)                                             \
    do {                                                                     \
        const char *w_ = (want);                                            \
        const char *g_ = (got);                                             \
        if (w_ == NULL || g_ == NULL || strcmp(w_, g_) != 0) {               \
            printf("    ASSERT_EQ_STR failed: want \"%s\", got \"%s\" "      \
                   "(%s:%d)\n",                                              \
                   w_ ? w_ : "(null)", g_ ? g_ : "(null)", __FILE__,         \
                   __LINE__);                                                \
            rewsr_ctest_fail_count++;                                        \
        }                                                                    \
    } while (0)

#define ASSERT_MEM_EQ(want, got, n)                                          \
    do {                                                                     \
        if (memcmp((want), (got), (n)) != 0) {                               \
            printf("    ASSERT_MEM_EQ failed over %zu bytes (%s:%d)\n",      \
                   (size_t)(n), __FILE__, __LINE__);                         \
            rewsr_ctest_fail_count++;                                        \
        }                                                                    \
    } while (0)

/* rewsr_ctest_run invokes one test, printing its name and outcome. It is
 * used by the REWSR_TEST_MAIN generator, not called directly. */
static inline int rewsr_ctest_run(const char *name, int (*fn)(void),
                                  int *total, int *failed) {
    rewsr_ctest_fail_count = 0;
    (*total)++;
    int rc = fn();
    if (rc != 0 || rewsr_ctest_fail_count != 0) {
        printf("  FAIL %s\n", name);
        (*failed)++;
        return 1;
    }
    printf("  ok   %s\n", name);
    return 0;
}

#define RUN_TEST(fn) rewsr_ctest_run(#fn, fn, &rewsr_ctest_total, &rewsr_ctest_failed)

/* REWSR_TEST_MAIN(suite_name, body) generates a main that runs the
 * RUN_TEST(...) calls in body and exits nonzero if any test failed. */
#define REWSR_TEST_MAIN(suite, body)                                         \
    int main(void) {                                                         \
        int rewsr_ctest_total = 0;                                           \
        int rewsr_ctest_failed = 0;                                          \
        printf("== %s ==\n", suite);                                         \
        body;                                                                \
        printf("%s: %d run, %d failed\n", suite,                             \
               rewsr_ctest_total, rewsr_ctest_failed);                       \
        return rewsr_ctest_failed == 0 ? 0 : 1;                              \
    }

#endif /* REWSR_CTEST_H */
