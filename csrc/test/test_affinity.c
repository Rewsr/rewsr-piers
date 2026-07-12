#include "ctest.h"

#include "../sys/affinity.h"

static int test_set_and_query(void) {
    struct rewsr_cpuset s;
    rewsr_cpuset_zero(&s);
    ASSERT_EQ_INT(0, rewsr_cpuset_count(&s));
    ASSERT_EQ_INT(-1, rewsr_cpuset_first(&s));

    rewsr_cpuset_set(&s, 0);
    rewsr_cpuset_set(&s, 63);
    rewsr_cpuset_set(&s, 64);  /* crosses into the second word */
    rewsr_cpuset_set(&s, 500);
    ASSERT_EQ_INT(4, rewsr_cpuset_count(&s));
    ASSERT_EQ_INT(0, rewsr_cpuset_first(&s));
    ASSERT_TRUE(rewsr_cpuset_isset(&s, 64));
    ASSERT_FALSE(rewsr_cpuset_isset(&s, 1));

    rewsr_cpuset_clr(&s, 0);
    ASSERT_EQ_INT(63, rewsr_cpuset_first(&s));
    ASSERT_EQ_INT(3, rewsr_cpuset_count(&s));
    return 0;
}

static int test_out_of_range_ignored(void) {
    struct rewsr_cpuset s;
    rewsr_cpuset_zero(&s);
    rewsr_cpuset_set(&s, -1);
    rewsr_cpuset_set(&s, REWSR_CPUSET_MAX);
    rewsr_cpuset_set(&s, REWSR_CPUSET_MAX + 100);
    ASSERT_EQ_INT(0, rewsr_cpuset_count(&s));
    ASSERT_FALSE(rewsr_cpuset_isset(&s, -1));
    ASSERT_FALSE(rewsr_cpuset_isset(&s, REWSR_CPUSET_MAX));
    return 0;
}

static int test_parse_cpulist(void) {
    struct rewsr_cpuset s;

    ASSERT_EQ_INT(0, rewsr_cpuset_parse(&s, "0-3,8,12-15"));
    ASSERT_EQ_INT(9, rewsr_cpuset_count(&s));
    for (int c = 0; c <= 3; c++) {
        ASSERT_TRUE(rewsr_cpuset_isset(&s, c));
    }
    ASSERT_TRUE(rewsr_cpuset_isset(&s, 8));
    ASSERT_FALSE(rewsr_cpuset_isset(&s, 9));
    for (int c = 12; c <= 15; c++) {
        ASSERT_TRUE(rewsr_cpuset_isset(&s, c));
    }

    ASSERT_EQ_INT(0, rewsr_cpuset_parse(&s, "7"));
    ASSERT_EQ_INT(1, rewsr_cpuset_count(&s));
    ASSERT_TRUE(rewsr_cpuset_isset(&s, 7));

    /* A NUMA-node-sized range on a big host. */
    ASSERT_EQ_INT(0, rewsr_cpuset_parse(&s, "0-63,128-191"));
    ASSERT_EQ_INT(128, rewsr_cpuset_count(&s));
    return 0;
}

static int test_parse_rejects_malformed(void) {
    struct rewsr_cpuset s;
    ASSERT_EQ_INT(-1, rewsr_cpuset_parse(&s, "abc"));
    ASSERT_EQ_INT(-1, rewsr_cpuset_parse(&s, "3-1"));   /* reversed range */
    ASSERT_EQ_INT(-1, rewsr_cpuset_parse(&s, "0,,2"));  /* empty token */
    ASSERT_EQ_INT(-1, rewsr_cpuset_parse(&s, "0-"));    /* dangling range */
    ASSERT_EQ_INT(-1, rewsr_cpuset_parse(&s, NULL));
    return 0;
}

REWSR_TEST_MAIN("affinity", {
    RUN_TEST(test_set_and_query);
    RUN_TEST(test_out_of_range_ignored);
    RUN_TEST(test_parse_cpulist);
    RUN_TEST(test_parse_rejects_malformed);
})
