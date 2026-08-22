#ifndef OPENTRAIL_LIBSODIUM_NOISE_XK_FAKE_SODIUM_H
#define OPENTRAIL_LIBSODIUM_NOISE_XK_FAKE_SODIUM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define crypto_kdf_hkdf_sha256_KEYBYTES 32U
#define crypto_aead_chacha20poly1305_ietf_NPUBBYTES 12U

typedef struct crypto_hash_sha256_state {
    uint8_t value[32];
    uint64_t count;
} crypto_hash_sha256_state;

int crypto_hash_sha256(uint8_t *, const uint8_t *, unsigned long long);
int crypto_hash_sha256_init(crypto_hash_sha256_state *);
int crypto_hash_sha256_update(crypto_hash_sha256_state *, const uint8_t *, unsigned long long);
int crypto_hash_sha256_final(crypto_hash_sha256_state *, uint8_t *);
int crypto_kdf_hkdf_sha256_extract(uint8_t *, const uint8_t *, size_t,
                                   const uint8_t *, size_t);
int crypto_kdf_hkdf_sha256_expand(uint8_t *, size_t, const char *, size_t,
                                  const uint8_t *);
int crypto_scalarmult_curve25519(uint8_t *, const uint8_t *, const uint8_t *);
int crypto_aead_chacha20poly1305_ietf_encrypt(
    uint8_t *, unsigned long long *, const uint8_t *, unsigned long long,
    const uint8_t *, unsigned long long, const uint8_t *, const uint8_t *,
    const uint8_t *);
int crypto_aead_chacha20poly1305_ietf_decrypt(
    uint8_t *, unsigned long long *, uint8_t *, const uint8_t *,
    unsigned long long, const uint8_t *, unsigned long long, const uint8_t *,
    const uint8_t *);
void sodium_memzero(void *, size_t);

#ifdef __cplusplus
}
#endif

#endif
