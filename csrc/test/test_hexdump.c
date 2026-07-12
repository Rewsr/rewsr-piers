#include "ctest.h"

#include "../util/hexdump.h"

#include <string.h>

static int test_full_line(void) {
    uint8_t data[16];
    for (int i = 0; i < 16; i++) {
        data[i] = (uint8_t)(0x30 + i); /* '0'..'?' printable ASCII */
    }
    char line[REWSR_HEXDUMP_LINE_MAX];
    rewsr_hexdump_line(line, 0, data, 16);

    /* Offset, hex bytes with the mid-line gap, and the ASCII gutter. */
    ASSERT_EQ_STR(
        "00000000  30 31 32 33 34 35 36 37  38 39 3a 3b 3c 3d 3e 3f "
        "|0123456789:;<=>?|",
        line);
    return 0;
}

static int test_offset_rendered(void) {
    uint8_t data[1] = {0xff};
    char line[REWSR_HEXDUMP_LINE_MAX];
    rewsr_hexdump_line(line, 0x12345678, data, 1);
    ASSERT_TRUE(strncmp(line, "12345678  ", 10) == 0);
    return 0;
}

static int test_short_line_pads_and_dots(void) {
    /* A three-byte line: two printable, one control. The hex columns for
     * the missing bytes must be spaces so the gutter still aligns, and the
     * non-printable byte shows as a dot. */
    uint8_t data[3] = {'A', 0x01, 'B'};
    char line[REWSR_HEXDUMP_LINE_MAX];
    rewsr_hexdump_line(line, 0, data, 3);
    /* The ASCII gutter is the final |...| section. */
    const char *bar = strchr(line, '|');
    ASSERT_TRUE(bar != NULL);
    ASSERT_EQ_STR("|A.B|", bar);
    return 0;
}

REWSR_TEST_MAIN("hexdump", {
    RUN_TEST(test_full_line);
    RUN_TEST(test_offset_rendered);
    RUN_TEST(test_short_line_pads_and_dots);
})
