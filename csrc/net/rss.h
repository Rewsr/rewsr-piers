#ifndef REWSR_NET_RSS_H
#define REWSR_NET_RSS_H

#include <stddef.h>
#include <stdint.h>

/* rss computes the Toeplitz hash NICs use for receive-side scaling, so the
 * data plane can predict which hardware queue (and therefore which pinned
 * worker) a given flow will land on, and can pick a symmetric source port
 * that keeps both directions of a flow on the same queue. Matching the
 * hardware's hash in software is what lets the fabric steer and pin without
 * guessing. The algorithm is the standard Microsoft RSS Toeplitz function
 * and is verified against the published test vectors. It is pure math and
 * builds and tests anywhere. */

#define REWSR_RSS_KEY_LEN 40

/* The default 40-byte RSS key from the Microsoft RSS specification, the
 * same key most drivers ship by default, so predictions match a stock
 * host out of the box. */
extern const uint8_t rewsr_rss_default_key[REWSR_RSS_KEY_LEN];

/* rewsr_rss_toeplitz computes the Toeplitz hash of input using key. The key
 * must be at least input_len + 4 bytes; the default key covers inputs up to
 * 36 bytes, enough for an IPv4 or IPv6 four-tuple. */
uint32_t rewsr_rss_toeplitz(const uint8_t *key, size_t key_len,
                            const uint8_t *input, size_t input_len);

/* rewsr_rss_hash_ipv4 builds the 12-byte RSS input from an IPv4 four-tuple
 * (addresses and ports in host order) and returns its Toeplitz hash under
 * key. This is the value a NIC computes for a TCP or UDP IPv4 flow with
 * four-tuple hashing enabled. */
uint32_t rewsr_rss_hash_ipv4(const uint8_t *key, size_t key_len,
                             uint32_t src_ip, uint32_t dst_ip,
                             uint16_t src_port, uint16_t dst_port);

/* rewsr_rss_hash_ipv4_2tuple hashes just the address pair, matching a NIC
 * configured for two-tuple (IP only) hashing. */
uint32_t rewsr_rss_hash_ipv4_2tuple(const uint8_t *key, size_t key_len,
                                    uint32_t src_ip, uint32_t dst_ip);

/* rewsr_rss_queue maps a hash to a queue index given the indirection table
 * size, which is how a NIC turns the hash into a destination queue: the low
 * bits index the RETA. table_size must be a power of two. */
uint32_t rewsr_rss_queue(uint32_t hash, uint32_t table_size);

#endif /* REWSR_NET_RSS_H */
