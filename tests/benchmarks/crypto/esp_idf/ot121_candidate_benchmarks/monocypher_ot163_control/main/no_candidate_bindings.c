/* Accounting-only replacements: no cryptography and no benchmark acceptance. */
#include <stddef.h>
#include <stdint.h>
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include "monocypher_benchmark_api.h"

static volatile uint32_t control_state;
#define UNUSED(value) ((void)(value))

void crypto_wipe(void *secret, size_t size)
{
    volatile uint8_t *bytes = secret;
    while (size-- != 0U) { *bytes++ = 0U; }
}

void crypto_ed25519_key_pair(uint8_t secret[64], uint8_t public_key[32], uint8_t seed[32])
{
    UNUSED(secret); UNUSED(public_key); UNUSED(seed); control_state++;
}

void crypto_x25519_public_key(uint8_t public_key[32], const uint8_t secret[32])
{
    UNUSED(public_key); UNUSED(secret); control_state++;
}

int ot_monocypher_ed25519_sign(uint8_t signature[64], const uint8_t secret[64],
                              const uint8_t *message, size_t size)
{
    UNUSED(signature); UNUSED(secret); UNUSED(message); UNUSED(size);
    control_state++; return -1;
}

int ot_monocypher_ed25519_verify(const uint8_t signature[64], const uint8_t public_key[32],
                                const uint8_t *message, size_t size)
{
    UNUSED(signature); UNUSED(public_key); UNUSED(message); UNUSED(size);
    control_state++; return -1;
}

int ot_monocypher_x25519(uint8_t shared[32], const uint8_t secret[32], const uint8_t public_key[32])
{
    UNUSED(shared); UNUSED(secret); UNUSED(public_key); control_state++; return -1;
}

int ot_monocypher_chacha20poly1305_ietf_encrypt(
    uint8_t *output, size_t capacity, size_t *output_size,
    const uint8_t key[32], const uint8_t nonce[12],
    const uint8_t *ad, size_t ad_size, const uint8_t *input, size_t input_size)
{
    UNUSED(output); UNUSED(capacity); UNUSED(key); UNUSED(nonce); UNUSED(ad);
    UNUSED(ad_size); UNUSED(input); UNUSED(input_size);
    if (output_size != NULL) { *output_size = 0U; }
    control_state++; return -1;
}

int ot_monocypher_chacha20poly1305_ietf_decrypt(
    uint8_t *output, size_t capacity, size_t *output_size,
    const uint8_t key[32], const uint8_t nonce[12],
    const uint8_t *ad, size_t ad_size, const uint8_t *input, size_t input_size)
{
    UNUSED(output); UNUSED(capacity); UNUSED(key); UNUSED(nonce); UNUSED(ad);
    UNUSED(ad_size); UNUSED(input); UNUSED(input_size);
    if (output_size != NULL) { *output_size = 0U; }
    control_state++; return -1;
}
