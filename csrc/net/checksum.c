#include "checksum.h"

/* The accumulator is kept as a 32-bit sum of 16-bit big-endian words.
 * Carries pile up in the high half and are folded down at the end, which
 * is both faster and easier to get right than folding on every word. */

uint32_t rewsr_cksum_partial(const void *data, size_t len, uint32_t seed) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = seed;

    /* Sum complete 16-bit words in network byte order. */
    while (len > 1) {
        sum += (uint32_t)((p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }

    /* A trailing odd byte is treated as the high byte of a final word
     * whose low byte is zero, which is how the checksum is defined for
     * odd-length data. */
    if (len == 1) {
        sum += (uint32_t)(p[0] << 8);
    }

    return sum;
}

uint16_t rewsr_cksum_fold(uint32_t sum) {
    /* Fold the carry bits (bits 16 and up) back into the low 16 bits.
     * Two rounds are always enough for a 32-bit input: the first fold can
     * itself produce one more carry. */
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return (uint16_t)sum;
}

uint16_t rewsr_cksum(const void *data, size_t len) {
    uint32_t sum = rewsr_cksum_partial(data, len, 0);
    return (uint16_t)(~rewsr_cksum_fold(sum) & 0xffff);
}

uint32_t rewsr_cksum_ipv4_pseudo(uint32_t src_be, uint32_t dst_be,
                                 uint8_t proto, uint16_t l4_len) {
    uint32_t sum = 0;

    /* The addresses are already network byte order; sum them as two
     * 16-bit words each, high word first, to match the wire layout. */
    sum += (src_be >> 16) & 0xffff;
    sum += src_be & 0xffff;
    sum += (dst_be >> 16) & 0xffff;
    sum += dst_be & 0xffff;

    /* Protocol occupies the low byte of a 16-bit word whose high byte is
     * the zero pad byte of the pseudo-header. */
    sum += (uint32_t)proto;
    sum += (uint32_t)l4_len;

    return sum;
}

uint16_t rewsr_cksum_update(uint16_t old_check, uint16_t old_field,
                            uint16_t new_field) {
    /* RFC 1624 eqn 3: HC' = ~(~HC + ~m + m'), all in one's complement.
     * Working in 32 bits and folding avoids the end-around-borrow traps of
     * the naive form. */
    uint32_t sum = (uint32_t)((~old_check) & 0xffff);
    sum += (uint32_t)((~old_field) & 0xffff);
    sum += (uint32_t)new_field;
    return (uint16_t)(~rewsr_cksum_fold(sum) & 0xffff);
}
