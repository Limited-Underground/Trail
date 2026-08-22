#ifndef OPENTRAIL_TESTS_BENCHMARKS_CRYPTO_MONOCYPHER_API_V0_H
#define OPENTRAIL_TESTS_BENCHMARKS_CRYPTO_MONOCYPHER_API_V0_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OT_MONOCYPHER_API_SCHEMA "OTMAPI0"
#define OT_MONOCYPHER_API_VERSION 0U
#define OT_MONOCYPHER_KEY_BYTES 32U
#define OT_MONOCYPHER_ED25519_SECRET_BYTES 64U
#define OT_MONOCYPHER_ED25519_SIGNATURE_BYTES 64U
#define OT_MONOCYPHER_IETF_NONCE_BYTES 12U
#define OT_MONOCYPHER_AEAD_TAG_BYTES 16U

/*
 * Benchmark-only wrappers around the five operations present in the exact
 * retained Monocypher 4.0.3 source. They generate no keys or entropy and do
 * not claim SHA-256, HKDF-SHA256, Noise XK, production, or execution status.
 */
int ot_monocypher_ed25519_sign(
    uint8_t signature[OT_MONOCYPHER_ED25519_SIGNATURE_BYTES],
    const uint8_t secret_key[OT_MONOCYPHER_ED25519_SECRET_BYTES],
    const uint8_t *message,
    size_t message_size);

int ot_monocypher_ed25519_verify(
    const uint8_t signature[OT_MONOCYPHER_ED25519_SIGNATURE_BYTES],
    const uint8_t public_key[OT_MONOCYPHER_KEY_BYTES],
    const uint8_t *message,
    size_t message_size);

int ot_monocypher_x25519(
    uint8_t shared_secret[OT_MONOCYPHER_KEY_BYTES],
    const uint8_t local_secret[OT_MONOCYPHER_KEY_BYTES],
    const uint8_t remote_public[OT_MONOCYPHER_KEY_BYTES]);

int ot_monocypher_chacha20poly1305_ietf_encrypt(
    uint8_t *cipher_and_tag,
    size_t cipher_and_tag_capacity,
    size_t *cipher_and_tag_size,
    const uint8_t key[OT_MONOCYPHER_KEY_BYTES],
    const uint8_t nonce[OT_MONOCYPHER_IETF_NONCE_BYTES],
    const uint8_t *associated_data,
    size_t associated_data_size,
    const uint8_t *plain_text,
    size_t plain_text_size);

int ot_monocypher_chacha20poly1305_ietf_decrypt(
    uint8_t *plain_text,
    size_t plain_text_capacity,
    size_t *plain_text_size,
    const uint8_t key[OT_MONOCYPHER_KEY_BYTES],
    const uint8_t nonce[OT_MONOCYPHER_IETF_NONCE_BYTES],
    const uint8_t *associated_data,
    size_t associated_data_size,
    const uint8_t *cipher_and_tag,
    size_t cipher_and_tag_size);

#ifdef __cplusplus
}
#endif

#endif
