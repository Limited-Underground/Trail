#include "noise_xk_libsodium.h"

#include <limits.h>
#include <sodium.h>
#include <string.h>

#define OT_NOISE_XK_OK 0
#define OT_NOISE_XK_ERROR (-1)

static void fail_state(ot_noise_xk_state *state)
{
    if (state != NULL) {
        sodium_memzero(state->chaining_key, sizeof state->chaining_key);
        sodium_memzero(state->cipher_key, sizeof state->cipher_key);
        sodium_memzero(state->local_static_secret, sizeof state->local_static_secret);
        sodium_memzero(state->local_ephemeral_secret, sizeof state->local_ephemeral_secret);
        state->has_cipher_key = 0U;
        state->nonce = 0U;
        state->stage = OT_NOISE_XK_STAGE_FAILED;
    }
}

void ot_noise_xk_abort(ot_noise_xk_state *state)
{
    if (state != NULL) {
        sodium_memzero(state, sizeof *state);
        state->stage = OT_NOISE_XK_STAGE_FAILED;
    }
}

static int mix_hash(ot_noise_xk_state *state, const uint8_t *data, size_t size)
{
    crypto_hash_sha256_state hash_state;
    uint8_t next_hash[OT_NOISE_XK_HASH_BYTES];
    int result = OT_NOISE_XK_ERROR;

    if (state == NULL || (data == NULL && size != 0U)) {
        return OT_NOISE_XK_ERROR;
    }
    if (crypto_hash_sha256_init(&hash_state) == 0 &&
        crypto_hash_sha256_update(&hash_state, state->handshake_hash,
                                  sizeof state->handshake_hash) == 0 &&
        crypto_hash_sha256_update(&hash_state, data, (unsigned long long) size) == 0 &&
        crypto_hash_sha256_final(&hash_state, next_hash) == 0) {
        memcpy(state->handshake_hash, next_hash, sizeof next_hash);
        result = OT_NOISE_XK_OK;
    }
    sodium_memzero(&hash_state, sizeof hash_state);
    sodium_memzero(next_hash, sizeof next_hash);
    return result;
}

static int initialize_symmetric(ot_noise_xk_state *state)
{
    static const uint8_t protocol_name[] = OT_NOISE_XK_PROTOCOL_NAME;

    memset(state->handshake_hash, 0, sizeof state->handshake_hash);
    if (sizeof protocol_name - 1U <= sizeof state->handshake_hash) {
        memcpy(state->handshake_hash, protocol_name, sizeof protocol_name - 1U);
    } else if (crypto_hash_sha256(state->handshake_hash, protocol_name,
                                  (unsigned long long) (sizeof protocol_name - 1U)) != 0) {
        return OT_NOISE_XK_ERROR;
    }
    memcpy(state->chaining_key, state->handshake_hash, sizeof state->chaining_key);
    state->has_cipher_key = 0U;
    state->nonce = 0U;
    return OT_NOISE_XK_OK;
}

static int mix_key(ot_noise_xk_state *state,
                   const uint8_t input_key_material[OT_NOISE_XK_KEY_BYTES])
{
    uint8_t pseudo_random_key[crypto_kdf_hkdf_sha256_KEYBYTES];
    uint8_t outputs[OT_NOISE_XK_KEY_BYTES * 2U];
    int result = OT_NOISE_XK_ERROR;

    if (crypto_kdf_hkdf_sha256_extract(
            pseudo_random_key, state->chaining_key, sizeof state->chaining_key,
            input_key_material, OT_NOISE_XK_KEY_BYTES) == 0 &&
        crypto_kdf_hkdf_sha256_expand(
            outputs, sizeof outputs, "", 0U, pseudo_random_key) == 0) {
        memcpy(state->chaining_key, outputs, OT_NOISE_XK_KEY_BYTES);
        memcpy(state->cipher_key, outputs + OT_NOISE_XK_KEY_BYTES,
               OT_NOISE_XK_KEY_BYTES);
        state->has_cipher_key = 1U;
        state->nonce = 0U;
        result = OT_NOISE_XK_OK;
    }
    sodium_memzero(pseudo_random_key, sizeof pseudo_random_key);
    sodium_memzero(outputs, sizeof outputs);
    return result;
}

static void encode_nonce(uint8_t nonce[crypto_aead_chacha20poly1305_ietf_NPUBBYTES],
                         uint64_t value)
{
    size_t index;
    memset(nonce, 0, crypto_aead_chacha20poly1305_ietf_NPUBBYTES);
    for (index = 0U; index < 8U; ++index) {
        nonce[4U + index] = (uint8_t) (value >> (8U * index));
    }
}

static int encrypt_and_hash(ot_noise_xk_state *state,
                            const uint8_t *plain, size_t plain_size,
                            uint8_t *cipher, size_t cipher_capacity,
                            size_t *cipher_size)
{
    uint8_t nonce[crypto_aead_chacha20poly1305_ietf_NPUBBYTES];
    unsigned long long written = 0U;
    int result = OT_NOISE_XK_ERROR;

    if (state == NULL || !state->has_cipher_key || state->nonce == UINT64_MAX ||
        cipher == NULL || cipher_size == NULL ||
        (plain == NULL && plain_size != 0U) ||
        plain_size > ULLONG_MAX - OT_NOISE_XK_TAG_BYTES ||
        cipher_capacity < plain_size + OT_NOISE_XK_TAG_BYTES) {
        return OT_NOISE_XK_ERROR;
    }
    encode_nonce(nonce, state->nonce);
    if (crypto_aead_chacha20poly1305_ietf_encrypt(
            cipher, &written, plain, (unsigned long long) plain_size,
            state->handshake_hash, sizeof state->handshake_hash, NULL,
            nonce, state->cipher_key) == 0 &&
        written == (unsigned long long) (plain_size + OT_NOISE_XK_TAG_BYTES) &&
        mix_hash(state, cipher, (size_t) written) == 0) {
        state->nonce++;
        *cipher_size = (size_t) written;
        result = OT_NOISE_XK_OK;
    }
    sodium_memzero(nonce, sizeof nonce);
    return result;
}

static int decrypt_and_hash(ot_noise_xk_state *state,
                            const uint8_t *cipher, size_t cipher_size,
                            uint8_t *plain, size_t plain_capacity,
                            size_t expected_plain_size)
{
    uint8_t nonce[crypto_aead_chacha20poly1305_ietf_NPUBBYTES];
    unsigned long long written = 0U;
    int result = OT_NOISE_XK_ERROR;

    if (state == NULL || !state->has_cipher_key || state->nonce == UINT64_MAX ||
        cipher == NULL || (plain == NULL && expected_plain_size != 0U) ||
        cipher_size != expected_plain_size + OT_NOISE_XK_TAG_BYTES ||
        plain_capacity < expected_plain_size) {
        return OT_NOISE_XK_ERROR;
    }
    encode_nonce(nonce, state->nonce);
    if (crypto_aead_chacha20poly1305_ietf_decrypt(
            plain, &written, NULL, cipher, (unsigned long long) cipher_size,
            state->handshake_hash, sizeof state->handshake_hash,
            nonce, state->cipher_key) == 0 &&
        written == (unsigned long long) expected_plain_size &&
        mix_hash(state, cipher, cipher_size) == 0) {
        state->nonce++;
        result = OT_NOISE_XK_OK;
    }
    sodium_memzero(nonce, sizeof nonce);
    return result;
}

static int perform_dh(ot_noise_xk_state *state,
                      const uint8_t secret[OT_NOISE_XK_KEY_BYTES],
                      const uint8_t public_key[OT_NOISE_XK_KEY_BYTES])
{
    uint8_t shared[OT_NOISE_XK_KEY_BYTES];
    int result = OT_NOISE_XK_ERROR;
    if (crypto_scalarmult_curve25519(shared, secret, public_key) == 0) {
        result = mix_key(state, shared);
    }
    sodium_memzero(shared, sizeof shared);
    return result;
}

static int initialize(ot_noise_xk_state *state, ot_noise_xk_role role,
                      const ot_noise_xk_keypair *local_static,
                      const ot_noise_xk_keypair *local_ephemeral,
                      const uint8_t *responder_static_public,
                      const uint8_t *prologue, size_t prologue_size)
{
    if (state == NULL) {
        return OT_NOISE_XK_ERROR;
    }
    memset(state, 0, sizeof *state);
    if (local_static == NULL || local_ephemeral == NULL ||
        responder_static_public == NULL || (prologue == NULL && prologue_size != 0U)) {
        state->stage = OT_NOISE_XK_STAGE_FAILED;
        return OT_NOISE_XK_ERROR;
    }
    state->role = role;
    memcpy(state->local_static_secret, local_static->secret, OT_NOISE_XK_KEY_BYTES);
    memcpy(state->local_static_public, local_static->public_key, OT_NOISE_XK_KEY_BYTES);
    memcpy(state->local_ephemeral_secret, local_ephemeral->secret, OT_NOISE_XK_KEY_BYTES);
    memcpy(state->local_ephemeral_public, local_ephemeral->public_key, OT_NOISE_XK_KEY_BYTES);
    memcpy(state->remote_static_public, responder_static_public, OT_NOISE_XK_KEY_BYTES);
    if (initialize_symmetric(state) != 0 ||
        mix_hash(state, prologue, prologue_size) != 0 ||
        mix_hash(state, responder_static_public, OT_NOISE_XK_KEY_BYTES) != 0) {
        fail_state(state);
        return OT_NOISE_XK_ERROR;
    }
    state->stage = role == OT_NOISE_XK_INITIATOR
        ? OT_NOISE_XK_STAGE_WRITE_MESSAGE_1
        : OT_NOISE_XK_STAGE_READ_MESSAGE_1;
    return OT_NOISE_XK_OK;
}

int ot_noise_xk_init_initiator(ot_noise_xk_state *state,
                               const ot_noise_xk_keypair *initiator_static,
                               const ot_noise_xk_keypair *initiator_ephemeral,
                               const uint8_t responder_static_public[OT_NOISE_XK_KEY_BYTES],
                               const uint8_t *prologue, size_t prologue_size)
{
    return initialize(state, OT_NOISE_XK_INITIATOR, initiator_static,
                      initiator_ephemeral, responder_static_public,
                      prologue, prologue_size);
}

int ot_noise_xk_init_responder(ot_noise_xk_state *state,
                               const ot_noise_xk_keypair *responder_static,
                               const ot_noise_xk_keypair *responder_ephemeral,
                               const uint8_t *prologue, size_t prologue_size)
{
    if (responder_static == NULL) {
        if (state != NULL) {
            memset(state, 0, sizeof *state);
            state->stage = OT_NOISE_XK_STAGE_FAILED;
        }
        return OT_NOISE_XK_ERROR;
    }
    return initialize(state, OT_NOISE_XK_RESPONDER, responder_static,
                      responder_ephemeral, responder_static->public_key,
                      prologue, prologue_size);
}

static int write_message_1(ot_noise_xk_state *state, uint8_t *message,
                           size_t capacity, size_t *size)
{
    size_t payload_size = 0U;
    if (capacity < OT_NOISE_XK_MESSAGE_1_BYTES) return OT_NOISE_XK_ERROR;
    memcpy(message, state->local_ephemeral_public, OT_NOISE_XK_KEY_BYTES);
    if (mix_hash(state, message, OT_NOISE_XK_KEY_BYTES) != 0 ||
        perform_dh(state, state->local_ephemeral_secret,
                   state->remote_static_public) != 0 ||
        encrypt_and_hash(state, NULL, 0U, message + OT_NOISE_XK_KEY_BYTES,
                         capacity - OT_NOISE_XK_KEY_BYTES, &payload_size) != 0 ||
        payload_size != OT_NOISE_XK_TAG_BYTES) return OT_NOISE_XK_ERROR;
    *size = OT_NOISE_XK_MESSAGE_1_BYTES;
    state->stage = OT_NOISE_XK_STAGE_READ_MESSAGE_2;
    return OT_NOISE_XK_OK;
}

static int read_message_1(ot_noise_xk_state *state, const uint8_t *message,
                          size_t size)
{
    uint8_t empty[1];
    if (size != OT_NOISE_XK_MESSAGE_1_BYTES) return OT_NOISE_XK_ERROR;
    memcpy(state->remote_ephemeral_public, message, OT_NOISE_XK_KEY_BYTES);
    if (mix_hash(state, message, OT_NOISE_XK_KEY_BYTES) != 0 ||
        perform_dh(state, state->local_static_secret,
                   state->remote_ephemeral_public) != 0 ||
        decrypt_and_hash(state, message + OT_NOISE_XK_KEY_BYTES,
                         OT_NOISE_XK_TAG_BYTES, empty, 0U, 0U) != 0)
        return OT_NOISE_XK_ERROR;
    state->stage = OT_NOISE_XK_STAGE_WRITE_MESSAGE_2;
    return OT_NOISE_XK_OK;
}

static int write_message_2(ot_noise_xk_state *state, uint8_t *message,
                           size_t capacity, size_t *size)
{
    size_t payload_size = 0U;
    if (capacity < OT_NOISE_XK_MESSAGE_2_BYTES) return OT_NOISE_XK_ERROR;
    memcpy(message, state->local_ephemeral_public, OT_NOISE_XK_KEY_BYTES);
    if (mix_hash(state, message, OT_NOISE_XK_KEY_BYTES) != 0 ||
        perform_dh(state, state->local_ephemeral_secret,
                   state->remote_ephemeral_public) != 0 ||
        encrypt_and_hash(state, NULL, 0U, message + OT_NOISE_XK_KEY_BYTES,
                         capacity - OT_NOISE_XK_KEY_BYTES, &payload_size) != 0 ||
        payload_size != OT_NOISE_XK_TAG_BYTES) return OT_NOISE_XK_ERROR;
    *size = OT_NOISE_XK_MESSAGE_2_BYTES;
    state->stage = OT_NOISE_XK_STAGE_READ_MESSAGE_3;
    return OT_NOISE_XK_OK;
}

static int read_message_2(ot_noise_xk_state *state, const uint8_t *message,
                          size_t size)
{
    uint8_t empty[1];
    if (size != OT_NOISE_XK_MESSAGE_2_BYTES) return OT_NOISE_XK_ERROR;
    memcpy(state->remote_ephemeral_public, message, OT_NOISE_XK_KEY_BYTES);
    if (mix_hash(state, message, OT_NOISE_XK_KEY_BYTES) != 0 ||
        perform_dh(state, state->local_ephemeral_secret,
                   state->remote_ephemeral_public) != 0 ||
        decrypt_and_hash(state, message + OT_NOISE_XK_KEY_BYTES,
                         OT_NOISE_XK_TAG_BYTES, empty, 0U, 0U) != 0)
        return OT_NOISE_XK_ERROR;
    state->stage = OT_NOISE_XK_STAGE_WRITE_MESSAGE_3;
    return OT_NOISE_XK_OK;
}

static int write_message_3(ot_noise_xk_state *state, uint8_t *message,
                           size_t capacity, size_t *size)
{
    size_t static_size = 0U, payload_size = 0U;
    if (capacity < OT_NOISE_XK_MESSAGE_3_BYTES) return OT_NOISE_XK_ERROR;
    if (encrypt_and_hash(state, state->local_static_public, OT_NOISE_XK_KEY_BYTES,
                         message, capacity, &static_size) != 0 ||
        static_size != OT_NOISE_XK_KEY_BYTES + OT_NOISE_XK_TAG_BYTES ||
        perform_dh(state, state->local_static_secret,
                   state->remote_ephemeral_public) != 0 ||
        encrypt_and_hash(state, NULL, 0U, message + static_size,
                         capacity - static_size, &payload_size) != 0 ||
        payload_size != OT_NOISE_XK_TAG_BYTES) return OT_NOISE_XK_ERROR;
    *size = OT_NOISE_XK_MESSAGE_3_BYTES;
    state->stage = OT_NOISE_XK_STAGE_SPLIT;
    return OT_NOISE_XK_OK;
}

static int read_message_3(ot_noise_xk_state *state, const uint8_t *message,
                          size_t size)
{
    uint8_t empty[1];
    if (size != OT_NOISE_XK_MESSAGE_3_BYTES) return OT_NOISE_XK_ERROR;
    if (decrypt_and_hash(state, message,
                         OT_NOISE_XK_KEY_BYTES + OT_NOISE_XK_TAG_BYTES,
                         state->remote_static_public, OT_NOISE_XK_KEY_BYTES,
                         OT_NOISE_XK_KEY_BYTES) != 0 ||
        perform_dh(state, state->local_ephemeral_secret,
                   state->remote_static_public) != 0 ||
        decrypt_and_hash(state,
                         message + OT_NOISE_XK_KEY_BYTES + OT_NOISE_XK_TAG_BYTES,
                         OT_NOISE_XK_TAG_BYTES, empty, 0U, 0U) != 0)
        return OT_NOISE_XK_ERROR;
    state->stage = OT_NOISE_XK_STAGE_SPLIT;
    return OT_NOISE_XK_OK;
}

int ot_noise_xk_write_message(ot_noise_xk_state *state, uint8_t *message,
                              size_t capacity, size_t *size)
{
    int result = OT_NOISE_XK_ERROR;
    if (state == NULL) return OT_NOISE_XK_ERROR;
    if (message == NULL || size == NULL) {
        fail_state(state);
        return OT_NOISE_XK_ERROR;
    }
    *size = 0U;
    if (state->stage == OT_NOISE_XK_STAGE_WRITE_MESSAGE_1)
        result = write_message_1(state, message, capacity, size);
    else if (state->stage == OT_NOISE_XK_STAGE_WRITE_MESSAGE_2)
        result = write_message_2(state, message, capacity, size);
    else if (state->stage == OT_NOISE_XK_STAGE_WRITE_MESSAGE_3)
        result = write_message_3(state, message, capacity, size);
    if (result != OT_NOISE_XK_OK) fail_state(state);
    return result;
}

int ot_noise_xk_read_message(ot_noise_xk_state *state, const uint8_t *message,
                             size_t size)
{
    int result = OT_NOISE_XK_ERROR;
    if (state == NULL) return OT_NOISE_XK_ERROR;
    if (message == NULL) {
        fail_state(state);
        return OT_NOISE_XK_ERROR;
    }
    if (state->stage == OT_NOISE_XK_STAGE_READ_MESSAGE_1)
        result = read_message_1(state, message, size);
    else if (state->stage == OT_NOISE_XK_STAGE_READ_MESSAGE_2)
        result = read_message_2(state, message, size);
    else if (state->stage == OT_NOISE_XK_STAGE_READ_MESSAGE_3)
        result = read_message_3(state, message, size);
    if (result != OT_NOISE_XK_OK) fail_state(state);
    return result;
}

int ot_noise_xk_split(ot_noise_xk_state *state,
                      uint8_t transmit_key[OT_NOISE_XK_KEY_BYTES],
                      uint8_t receive_key[OT_NOISE_XK_KEY_BYTES])
{
    uint8_t pseudo_random_key[crypto_kdf_hkdf_sha256_KEYBYTES];
    uint8_t outputs[OT_NOISE_XK_KEY_BYTES * 2U];
    int result = OT_NOISE_XK_ERROR;
    if (state == NULL || transmit_key == NULL || receive_key == NULL ||
        state->stage != OT_NOISE_XK_STAGE_SPLIT) {
        if (state != NULL) fail_state(state);
        return OT_NOISE_XK_ERROR;
    }
    if (crypto_kdf_hkdf_sha256_extract(
            pseudo_random_key, state->chaining_key, sizeof state->chaining_key,
            NULL, 0U) == 0 &&
        crypto_kdf_hkdf_sha256_expand(outputs, sizeof outputs, "", 0U,
                                      pseudo_random_key) == 0) {
        if (state->role == OT_NOISE_XK_INITIATOR) {
            memcpy(transmit_key, outputs, OT_NOISE_XK_KEY_BYTES);
            memcpy(receive_key, outputs + OT_NOISE_XK_KEY_BYTES, OT_NOISE_XK_KEY_BYTES);
        } else {
            memcpy(receive_key, outputs, OT_NOISE_XK_KEY_BYTES);
            memcpy(transmit_key, outputs + OT_NOISE_XK_KEY_BYTES, OT_NOISE_XK_KEY_BYTES);
        }
        sodium_memzero(state->chaining_key, sizeof state->chaining_key);
        sodium_memzero(state->cipher_key, sizeof state->cipher_key);
        sodium_memzero(state->local_static_secret, sizeof state->local_static_secret);
        sodium_memzero(state->local_ephemeral_secret,
                       sizeof state->local_ephemeral_secret);
        state->has_cipher_key = 0U;
        state->nonce = 0U;
        state->stage = OT_NOISE_XK_STAGE_FINISHED;
        result = OT_NOISE_XK_OK;
    } else {
        fail_state(state);
    }
    sodium_memzero(pseudo_random_key, sizeof pseudo_random_key);
    sodium_memzero(outputs, sizeof outputs);
    return result;
}
