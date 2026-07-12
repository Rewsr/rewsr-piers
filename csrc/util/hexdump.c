#include "hexdump.h"

#include <string.h>

static char hex_digit(uint8_t nibble) {
    return "0123456789abcdef"[nibble & 0x0f];
}

size_t rewsr_hexdump_line(char *buf, size_t offset, const uint8_t *data,
                          size_t n) {
    if (n > 16) {
        n = 16;
    }
    char *p = buf;

    /* 8-digit hex offset followed by two spaces. */
    for (int shift = 28; shift >= 0; shift -= 4) {
        *p++ = hex_digit((uint8_t)((offset >> shift) & 0xf));
    }
    *p++ = ' ';
    *p++ = ' ';

    /* 16 hex byte columns; an extra space after column 8 splits the line
     * into two eight-byte groups as the canonical format does. Missing
     * bytes in a short final line are rendered as spaces so the ASCII
     * gutter still lines up. */
    for (size_t i = 0; i < 16; i++) {
        if (i < n) {
            *p++ = hex_digit((uint8_t)(data[i] >> 4));
            *p++ = hex_digit(data[i]);
        } else {
            *p++ = ' ';
            *p++ = ' ';
        }
        *p++ = ' ';
        if (i == 7) {
            *p++ = ' ';
        }
    }

    *p++ = '|';
    for (size_t i = 0; i < n; i++) {
        uint8_t c = data[i];
        *p++ = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
    }
    *p++ = '|';
    *p = '\0';
    return (size_t)(p - buf);
}

void rewsr_hexdump(FILE *out, const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    char line[REWSR_HEXDUMP_LINE_MAX];
    for (size_t off = 0; off < len; off += 16) {
        size_t n = len - off;
        if (n > 16) {
            n = 16;
        }
        rewsr_hexdump_line(line, off, bytes + off, n);
        fprintf(out, "%s\n", line);
    }
}
