/* Compact SHA-256 and HMAC-SHA256 (FIPS 180-4, RFC 2104) for the Secure world. */
#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

struct sha256 {
    uint32_t h[8];
    uint64_t len;
    uint8_t buf[64];
    size_t fill;
};

void sha256_init(struct sha256 *s);
void sha256_update(struct sha256 *s, const void *data, size_t len);
void sha256_final(struct sha256 *s, uint8_t out[32]);

void hmac_sha256(const uint8_t *key, size_t key_len, const void *msg, size_t msg_len,
                 uint8_t out[32]);

#endif
