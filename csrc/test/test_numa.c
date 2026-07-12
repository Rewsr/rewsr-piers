#include "ctest.h"

#include "../sys/numa.h"

static int test_parse_distance_row(void) {
    uint8_t out[REWSR_NUMA_MAX_NODES];
    /* A two-socket host's node 0 row: local 10, cross-socket 32. */
    int n = rewsr_numa_parse_distance("10 32", out, REWSR_NUMA_MAX_NODES);
    ASSERT_EQ_INT(2, n);
    ASSERT_EQ_INT(10, out[0]);
    ASSERT_EQ_INT(32, out[1]);

    /* A four-node row with a trailing newline. */
    n = rewsr_numa_parse_distance("10 16 32 32\n", out, REWSR_NUMA_MAX_NODES);
    ASSERT_EQ_INT(4, n);
    ASSERT_EQ_INT(16, out[1]);
    return 0;
}

static int test_parse_distance_rejects_garbage(void) {
    uint8_t out[REWSR_NUMA_MAX_NODES];
    ASSERT_EQ_INT(-1,
                  rewsr_numa_parse_distance("10 abc", out, REWSR_NUMA_MAX_NODES));
    ASSERT_EQ_INT(-1, rewsr_numa_parse_distance(NULL, out, 4));
    return 0;
}

static int test_count_cpus(void) {
    ASSERT_EQ_INT(32, rewsr_numa_count_cpus("0-15,64-79"));
    ASSERT_EQ_INT(1, rewsr_numa_count_cpus("7"));
    ASSERT_EQ_INT(128, rewsr_numa_count_cpus("0-63,128-191"));
    ASSERT_EQ_INT(-1, rewsr_numa_count_cpus("3-1"));
    ASSERT_EQ_INT(-1, rewsr_numa_count_cpus("x"));
    return 0;
}

static int test_nearest_node(void) {
    struct rewsr_numa_topology topo;
    memset(&topo, 0, sizeof topo);
    topo.num_nodes = 4;
    /* Node 0 is closest to node 1 (16), farther from 2 and 3 (32). */
    uint8_t rows[4][4] = {
        {10, 16, 32, 32},
        {16, 10, 32, 32},
        {32, 32, 10, 16},
        {32, 32, 16, 10},
    };
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            topo.distance[i][j] = rows[i][j];
        }
    }
    ASSERT_EQ_INT(1, rewsr_numa_nearest_node(&topo, 0));
    ASSERT_EQ_INT(3, rewsr_numa_nearest_node(&topo, 2));
    return 0;
}

static int test_nearest_node_single(void) {
    struct rewsr_numa_topology topo;
    memset(&topo, 0, sizeof topo);
    topo.num_nodes = 1;
    /* A single-node host has no other node to spill to. */
    ASSERT_EQ_INT(-1, rewsr_numa_nearest_node(&topo, 0));
    return 0;
}

REWSR_TEST_MAIN("numa", {
    RUN_TEST(test_parse_distance_row);
    RUN_TEST(test_parse_distance_rejects_garbage);
    RUN_TEST(test_count_cpus);
    RUN_TEST(test_nearest_node);
    RUN_TEST(test_nearest_node_single);
})
