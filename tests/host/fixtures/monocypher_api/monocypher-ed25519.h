#ifndef OPENTRAIL_MONOCYPHER_API_FAKE_ED25519_H
#define OPENTRAIL_MONOCYPHER_API_FAKE_ED25519_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void crypto_ed25519_sign(uint8_t[64], const uint8_t[64], const uint8_t *, size_t);
int crypto_ed25519_check(const uint8_t[64], const uint8_t[32], const uint8_t *, size_t);

#ifdef __cplusplus
}
#endif

#endif
