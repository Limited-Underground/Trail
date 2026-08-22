#ifndef OPENTRAIL_MONOCYPHER_API_FAKE_MONOCYPHER_H
#define OPENTRAIL_MONOCYPHER_API_FAKE_MONOCYPHER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct crypto_aead_ctx {
    uint8_t key[32];
    uint8_t nonce[12];
} crypto_aead_ctx;

void crypto_wipe(void *, size_t);
void crypto_x25519(uint8_t[32], const uint8_t[32], const uint8_t[32]);
void crypto_aead_init_ietf(crypto_aead_ctx *, const uint8_t[32], const uint8_t[12]);
void crypto_aead_write(crypto_aead_ctx *, uint8_t *, uint8_t[16],
                       const uint8_t *, size_t, const uint8_t *, size_t);
int crypto_aead_read(crypto_aead_ctx *, uint8_t *, const uint8_t[16],
                     const uint8_t *, size_t, const uint8_t *, size_t);

#ifdef __cplusplus
}
#endif

#endif
