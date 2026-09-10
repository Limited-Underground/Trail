/* Accounting-only replacements. No cryptography or successful benchmark result. */
#include <stddef.h>
#include <stdint.h>
#include "sodium.h"
#include "noise_xk_libsodium.h"
static volatile uint32_t control_state;

int crypto_aead_chacha20poly1305_ietf_decrypt(unsigned char *m,
                                              unsigned long long *mlen_p,
                                              unsigned char *nsec,
                                              const unsigned char *c,
                                              unsigned long long clen,
                                              const unsigned char *ad,
                                              unsigned long long adlen,
                                              const unsigned char *npub,
                                              const unsigned char *k)
{
    (void) m;
    (void) mlen_p;
    (void) nsec;
    (void) c;
    (void) clen;
    (void) ad;
    (void) adlen;
    (void) npub;
    (void) k;
    control_state++;
    return -1;
}

int crypto_aead_chacha20poly1305_ietf_encrypt(unsigned char *c,
                                              unsigned long long *clen_p,
                                              const unsigned char *m,
                                              unsigned long long mlen,
                                              const unsigned char *ad,
                                              unsigned long long adlen,
                                              const unsigned char *nsec,
                                              const unsigned char *npub,
                                              const unsigned char *k)
{
    (void) c;
    (void) clen_p;
    (void) m;
    (void) mlen;
    (void) ad;
    (void) adlen;
    (void) nsec;
    (void) npub;
    (void) k;
    control_state++;
    return -1;
}

int crypto_hash_sha256(unsigned char *out, const unsigned char *in,
                       unsigned long long inlen)
{
    (void) out;
    (void) in;
    (void) inlen;
    control_state++;
    return -1;
}

int crypto_kdf_hkdf_sha256_expand(unsigned char *out, size_t out_len,
                                  const char *ctx, size_t ctx_len,
                                  const unsigned char prk[crypto_kdf_hkdf_sha256_KEYBYTES])
{
    (void) out;
    (void) out_len;
    (void) ctx;
    (void) ctx_len;
    (void) prk;
    control_state++;
    return -1;
}

int crypto_kdf_hkdf_sha256_extract(unsigned char prk[crypto_kdf_hkdf_sha256_KEYBYTES],
                                   const unsigned char *salt, size_t salt_len,
                                   const unsigned char *ikm, size_t ikm_len)
{
    (void) prk;
    (void) salt;
    (void) salt_len;
    (void) ikm;
    (void) ikm_len;
    control_state++;
    return -1;
}

int crypto_scalarmult_curve25519(unsigned char *q, const unsigned char *n,
                                 const unsigned char *p)
{
    (void) q;
    (void) n;
    (void) p;
    control_state++;
    return -1;
}

int crypto_scalarmult_curve25519_base(unsigned char *q,
                                      const unsigned char *n)
{
    (void) q;
    (void) n;
    control_state++;
    return -1;
}

int crypto_sign_detached(unsigned char *sig, unsigned long long *siglen_p,
                         const unsigned char *m, unsigned long long mlen,
                         const unsigned char *sk)
{
    (void) sig;
    (void) siglen_p;
    (void) m;
    (void) mlen;
    (void) sk;
    control_state++;
    return -1;
}

int crypto_sign_seed_keypair(unsigned char *pk, unsigned char *sk,
                             const unsigned char *seed)
{
    (void) pk;
    (void) sk;
    (void) seed;
    control_state++;
    return -1;
}

int crypto_sign_verify_detached(const unsigned char *sig,
                                const unsigned char *m,
                                unsigned long long mlen,
                                const unsigned char *pk)
{
    (void) sig;
    (void) m;
    (void) mlen;
    (void) pk;
    control_state++;
    return -1;
}

void ot_noise_xk_abort(ot_noise_xk_state *state)
{
    (void) state;
    control_state++;
}

int ot_noise_xk_init_initiator(
    ot_noise_xk_state *state,
    const ot_noise_xk_keypair *initiator_static,
    const ot_noise_xk_keypair *initiator_ephemeral,
    const uint8_t responder_static_public[OT_NOISE_XK_KEY_BYTES],
    const uint8_t *prologue,
    size_t prologue_size)
{
    (void) state;
    (void) initiator_static;
    (void) initiator_ephemeral;
    (void) responder_static_public;
    (void) prologue;
    (void) prologue_size;
    control_state++;
    return -1;
}

int ot_noise_xk_init_responder(
    ot_noise_xk_state *state,
    const ot_noise_xk_keypair *responder_static,
    const ot_noise_xk_keypair *responder_ephemeral,
    const uint8_t *prologue,
    size_t prologue_size)
{
    (void) state;
    (void) responder_static;
    (void) responder_ephemeral;
    (void) prologue;
    (void) prologue_size;
    control_state++;
    return -1;
}

int ot_noise_xk_read_message(
    ot_noise_xk_state *state,
    const uint8_t *message,
    size_t message_size)
{
    (void) state;
    (void) message;
    (void) message_size;
    control_state++;
    return -1;
}

int ot_noise_xk_split(
    ot_noise_xk_state *state,
    uint8_t transmit_key[OT_NOISE_XK_KEY_BYTES],
    uint8_t receive_key[OT_NOISE_XK_KEY_BYTES])
{
    (void) state;
    (void) transmit_key;
    (void) receive_key;
    control_state++;
    return -1;
}

int ot_noise_xk_write_message(
    ot_noise_xk_state *state,
    uint8_t *message,
    size_t message_capacity,
    size_t *message_size)
{
    (void) state;
    (void) message;
    (void) message_capacity;
    (void) message_size;
    control_state++;
    return -1;
}

int sodium_init(void)
{
    control_state++;
    return -1;
}

int sodium_memcmp(const void *const b1_, const void *const b2_, size_t len)
{
    (void) b1_;
    (void) b2_;
    (void) len;
    control_state++;
    return -1;
}

void sodium_memzero(void *const pnt, const size_t len)
{
    volatile unsigned char *bytes = pnt;
    size_t remaining = len;
    while (remaining-- != 0U) { *bytes++ = 0U; }
}
