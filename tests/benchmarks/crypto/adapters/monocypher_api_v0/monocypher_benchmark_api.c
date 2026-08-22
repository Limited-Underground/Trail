#include "monocypher_benchmark_api.h"

#include <monocypher-ed25519.h>
#include <monocypher.h>

static int valid_input(const uint8_t *value, size_t size)
{
    return value != NULL || size == 0U;
}

int ot_monocypher_ed25519_sign(uint8_t signature[OT_MONOCYPHER_ED25519_SIGNATURE_BYTES],
                               const uint8_t secret_key[OT_MONOCYPHER_ED25519_SECRET_BYTES],
                               const uint8_t *message, size_t message_size)
{
    if (signature == NULL || secret_key == NULL || !valid_input(message, message_size)) {
        return -1;
    }
    crypto_ed25519_sign(signature, secret_key, message, message_size);
    return 0;
}

int ot_monocypher_ed25519_verify(
    const uint8_t signature[OT_MONOCYPHER_ED25519_SIGNATURE_BYTES],
    const uint8_t public_key[OT_MONOCYPHER_KEY_BYTES],
    const uint8_t *message, size_t message_size)
{
    if (signature == NULL || public_key == NULL || !valid_input(message, message_size)) {
        return -1;
    }
    return crypto_ed25519_check(signature, public_key, message, message_size);
}

int ot_monocypher_x25519(uint8_t shared_secret[OT_MONOCYPHER_KEY_BYTES],
                         const uint8_t local_secret[OT_MONOCYPHER_KEY_BYTES],
                         const uint8_t remote_public[OT_MONOCYPHER_KEY_BYTES])
{
    if (shared_secret == NULL || local_secret == NULL || remote_public == NULL) {
        return -1;
    }
    crypto_x25519(shared_secret, local_secret, remote_public);
    return 0;
}

int ot_monocypher_chacha20poly1305_ietf_encrypt(
    uint8_t *cipher_and_tag, size_t cipher_and_tag_capacity,
    size_t *cipher_and_tag_size, const uint8_t key[OT_MONOCYPHER_KEY_BYTES],
    const uint8_t nonce[OT_MONOCYPHER_IETF_NONCE_BYTES],
    const uint8_t *associated_data, size_t associated_data_size,
    const uint8_t *plain_text, size_t plain_text_size)
{
    crypto_aead_ctx context;
    if (cipher_and_tag_size != NULL) *cipher_and_tag_size = 0U;
    if (cipher_and_tag == NULL || cipher_and_tag_size == NULL || key == NULL ||
        nonce == NULL || !valid_input(associated_data, associated_data_size) ||
        !valid_input(plain_text, plain_text_size) ||
        plain_text_size > SIZE_MAX - OT_MONOCYPHER_AEAD_TAG_BYTES ||
        cipher_and_tag_capacity < plain_text_size + OT_MONOCYPHER_AEAD_TAG_BYTES) {
        return -1;
    }
    crypto_aead_init_ietf(&context, key, nonce);
    crypto_aead_write(&context, cipher_and_tag,
                      cipher_and_tag + plain_text_size,
                      associated_data, associated_data_size,
                      plain_text, plain_text_size);
    crypto_wipe(&context, sizeof context);
    *cipher_and_tag_size = plain_text_size + OT_MONOCYPHER_AEAD_TAG_BYTES;
    return 0;
}

int ot_monocypher_chacha20poly1305_ietf_decrypt(
    uint8_t *plain_text, size_t plain_text_capacity, size_t *plain_text_size,
    const uint8_t key[OT_MONOCYPHER_KEY_BYTES],
    const uint8_t nonce[OT_MONOCYPHER_IETF_NONCE_BYTES],
    const uint8_t *associated_data, size_t associated_data_size,
    const uint8_t *cipher_and_tag, size_t cipher_and_tag_size)
{
    crypto_aead_ctx context;
    size_t cipher_size;
    int result;
    if (plain_text_size != NULL) *plain_text_size = 0U;
    if (plain_text == NULL || plain_text_size == NULL || key == NULL ||
        nonce == NULL || !valid_input(associated_data, associated_data_size) ||
        cipher_and_tag == NULL || cipher_and_tag_size < OT_MONOCYPHER_AEAD_TAG_BYTES) {
        return -1;
    }
    cipher_size = cipher_and_tag_size - OT_MONOCYPHER_AEAD_TAG_BYTES;
    if (plain_text_capacity < cipher_size) return -1;
    crypto_aead_init_ietf(&context, key, nonce);
    result = crypto_aead_read(&context, plain_text,
                              cipher_and_tag + cipher_size,
                              associated_data, associated_data_size,
                              cipher_and_tag, cipher_size);
    crypto_wipe(&context, sizeof context);
    if (result != 0) {
        crypto_wipe(plain_text, cipher_size);
        return -1;
    }
    *plain_text_size = cipher_size;
    return 0;
}
