#include <stddef.h>

#include "sodium.h"

_Static_assert(crypto_sign_PUBLICKEYBYTES == 32U,
               "The OT-099 candidate requires 32-byte Ed25519 public keys.");
_Static_assert(crypto_scalarmult_curve25519_BYTES == 32U,
               "The OT-099 candidate requires 32-byte X25519 outputs.");
_Static_assert(crypto_hash_sha256_BYTES == 32U,
               "The OT-099 candidate requires SHA-256.");
_Static_assert(crypto_kdf_hkdf_sha256_KEYBYTES == 32U,
               "The OT-099 candidate requires HKDF-SHA-256.");
_Static_assert(crypto_aead_chacha20poly1305_ietf_NPUBBYTES == 12U,
               "The OT-099 candidate requires IETF ChaCha20-Poly1305.");

/*
 * These symbol references are compile/link probes only. The isolated candidate
 * project is never installed or run, and no keys, entropy, or messages exist.
 */
static void ot099_require_candidate_symbols(void) __attribute__((used));
static void ot099_require_candidate_symbols(void)
{
    (void) &crypto_sign_detached;
    (void) &crypto_sign_verify_detached;
    (void) &crypto_scalarmult_curve25519;
    (void) &crypto_hash_sha256;
    (void) &crypto_kdf_hkdf_sha256_extract;
    (void) &crypto_kdf_hkdf_sha256_expand;
    (void) &crypto_aead_chacha20poly1305_ietf_encrypt;
    (void) &crypto_aead_chacha20poly1305_ietf_decrypt;
}

void app_main(void)
{
    /* Intentionally empty: OT-099 authorizes a computer-only build. */
}
