#include "ctest.h"

#include "../net/ethtool_ioctl.h"

static int test_speed_names_round_values(void) {
    ASSERT_EQ_STR("1G", rewsr_ethtool_speed_name(1000));
    ASSERT_EQ_STR("25G", rewsr_ethtool_speed_name(25000));
    ASSERT_EQ_STR("100G", rewsr_ethtool_speed_name(100000));
    ASSERT_EQ_STR("400G", rewsr_ethtool_speed_name(400000));
    ASSERT_EQ_STR("2.5G", rewsr_ethtool_speed_name(2500));
    return 0;
}

static int test_speed_names_unknown_and_odd(void) {
    ASSERT_EQ_STR("unknown", rewsr_ethtool_speed_name(REWSR_SPEED_UNKNOWN));
    /* A non-catalogued round multiple of 1000 renders as "<n>G". */
    ASSERT_EQ_STR("8G", rewsr_ethtool_speed_name(8000));
    /* A sub-gigabit or non-round speed renders as "<n>M". */
    ASSERT_EQ_STR("100M", rewsr_ethtool_speed_name(100));
    return 0;
}

static int test_duplex_names(void) {
    ASSERT_EQ_STR("full", rewsr_ethtool_duplex_name(REWSR_DUPLEX_FULL));
    ASSERT_EQ_STR("half", rewsr_ethtool_duplex_name(REWSR_DUPLEX_HALF));
    ASSERT_EQ_STR("unknown", rewsr_ethtool_duplex_name(REWSR_DUPLEX_UNKNOWN));
    return 0;
}

REWSR_TEST_MAIN("ethtool", {
    RUN_TEST(test_speed_names_round_values);
    RUN_TEST(test_speed_names_unknown_and_odd);
    RUN_TEST(test_duplex_names);
})
