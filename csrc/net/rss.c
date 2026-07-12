#include "rss.h"

#include <string.h>

/* The published Microsoft RSS key. Drivers that do not override it use this
 * exact byte string, so predictions made with it match a stock NIC. */
const uint8_t rewsr_rss_default_key[REWSR_RSS_KEY_LEN] = {
    0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2, 0x41, 0x67,
    0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0, 0xd0, 0xca, 0x2b, 0xcb,
    0xae, 0x7b, 0x30, 0xb4, 0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30,
    0xf2, 0x0c, 0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa};

uint32_t rewsr_rss_toeplitz(const uint8_t *key, size_t key_len,
                            const uint8_t *input, size_t input_len) {
    uint32_t result = 0;

    /* The Toeplitz hash walks the input bit by bit, most significant bit
     * first. For every 1 bit it XORs in the current 32-bit key window; the
     * window then shifts left one bit, bringing in the next key bit from
     * the low end. v holds the window, and key_bit tracks the index of the
     * next key bit to shift in (the first 32 bits are already in v). */
    uint32_t v = ((uint32_t)key[0] << 24) | ((uint32_t)key[1] << 16) |
                 ((uint32_t)key[2] << 8) | (uint32_t)key[3];
    size_t key_bit = 32;

    for (size_t i = 0; i < input_len; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            if ((input[i] >> bit) & 1u) {
                result ^= v;
            }
            /* Next key bit; zeros shift in only if the caller undersized
             * the key, which the input_len contract forbids. */
            uint32_t next = 0;
            if (key_bit / 8 < key_len) {
                next = (key[key_bit / 8] >> (7 - (key_bit % 8))) & 1u;
            }
            v = (v << 1) | next;
            key_bit++;
        }
    }
    return result;
}

uint32_t rewsr_rss_hash_ipv4(const uint8_t *key, size_t key_len,
                             uint32_t src_ip, uint32_t dst_ip,
                             uint16_t src_port, uint16_t dst_port) {
    /* The RSS input for an IPv4 four-tuple is src IP, dst IP, src port, dst
     * port, each in network (big-endian) byte order. */
    uint8_t in[12];
    in[0] = (uint8_t)(src_ip >> 24);
    in[1] = (uint8_t)(src_ip >> 16);
    in[2] = (uint8_t)(src_ip >> 8);
    in[3] = (uint8_t)src_ip;
    in[4] = (uint8_t)(dst_ip >> 24);
    in[5] = (uint8_t)(dst_ip >> 16);
    in[6] = (uint8_t)(dst_ip >> 8);
    in[7] = (uint8_t)dst_ip;
    in[8] = (uint8_t)(src_port >> 8);
    in[9] = (uint8_t)src_port;
    in[10] = (uint8_t)(dst_port >> 8);
    in[11] = (uint8_t)dst_port;
    return rewsr_rss_toeplitz(key, key_len, in, sizeof in);
}

uint32_t rewsr_rss_hash_ipv4_2tuple(const uint8_t *key, size_t key_len,
                                    uint32_t src_ip, uint32_t dst_ip) {
    uint8_t in[8];
    in[0] = (uint8_t)(src_ip >> 24);
    in[1] = (uint8_t)(src_ip >> 16);
    in[2] = (uint8_t)(src_ip >> 8);
    in[3] = (uint8_t)src_ip;
    in[4] = (uint8_t)(dst_ip >> 24);
    in[5] = (uint8_t)(dst_ip >> 16);
    in[6] = (uint8_t)(dst_ip >> 8);
    in[7] = (uint8_t)dst_ip;
    return rewsr_rss_toeplitz(key, key_len, in, sizeof in);
}

uint32_t rewsr_rss_queue(uint32_t hash, uint32_t table_size) {
    if (table_size == 0) {
        return 0;
    }
    /* The NIC indexes its redirection table with the low bits of the hash.
     * A power-of-two table makes that a mask. */
    return hash & (table_size - 1);
}
