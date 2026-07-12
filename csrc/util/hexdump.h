#ifndef REWSR_UTIL_HEXDUMP_H
#define REWSR_UTIL_HEXDUMP_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* hexdump renders a byte buffer as the classic offset / hex / ASCII grid.
 * The data plane uses it to dump a malformed or unexpected frame to the
 * log when a parse fails, which is far more useful than a length and a
 * pointer. It writes to a caller-provided FILE so it composes with the
 * logger or a test harness. */

/* rewsr_hexdump writes the full canonical dump of len bytes at data to out,
 * 16 bytes per line, with a leading offset and a trailing ASCII gutter
 * where non-printable bytes show as '.'. */
void rewsr_hexdump(FILE *out, const void *data, size_t len);

/* rewsr_hexdump_line formats a single 16-byte line into buf (which must be
 * at least REWSR_HEXDUMP_LINE_MAX bytes) and returns its length. offset is
 * the address printed at the start of the line; n is how many of the 16
 * bytes are valid (1..16). This is the testable unit: it produces a
 * deterministic string with no I/O. */
#define REWSR_HEXDUMP_LINE_MAX 80
size_t rewsr_hexdump_line(char *buf, size_t offset, const uint8_t *data,
                          size_t n);

#endif /* REWSR_UTIL_HEXDUMP_H */
