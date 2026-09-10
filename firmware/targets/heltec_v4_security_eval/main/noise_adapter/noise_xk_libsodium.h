#ifndef OPENTRAIL_TESTS_BENCHMARKS_CRYPTO_LIBSODIUM_NOISE_XK_V0_H
#define OPENTRAIL_TESTS_BENCHMARKS_CRYPTO_LIBSODIUM_NOISE_XK_V0_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OT_NOISE_XK_SCHEMA "OTNXK0"
#define OT_NOISE_XK_VERSION 0U
#define OT_NOISE_XK_PROTOCOL_NAME "Noise_XK_25519_ChaChaPoly_SHA256"
#define OT_NOISE_XK_KEY_BYTES 32U
#define OT_NOISE_XK_HASH_BYTES 32U
#define OT_NOISE_XK_TAG_BYTES 16U
#define OT_NOISE_XK_MESSAGE_1_BYTES 48U
#define OT_NOISE_XK_MESSAGE_2_BYTES 48U
#define OT_NOISE_XK_MESSAGE_3_BYTES 64U
#define OT_NOISE_XK_TOTAL_HANDSHAKE_BYTES 160U

typedef enum ot_noise_xk_role {
    OT_NOISE_XK_INITIATOR = 1,
    OT_NOISE_XK_RESPONDER = 2
} ot_noise_xk_role;

typedef enum ot_noise_xk_stage {
    OT_NOISE_XK_STAGE_EMPTY = 0,
    OT_NOISE_XK_STAGE_WRITE_MESSAGE_1,
    OT_NOISE_XK_STAGE_READ_MESSAGE_1,
    OT_NOISE_XK_STAGE_WRITE_MESSAGE_2,
    OT_NOISE_XK_STAGE_READ_MESSAGE_2,
    OT_NOISE_XK_STAGE_WRITE_MESSAGE_3,
    OT_NOISE_XK_STAGE_READ_MESSAGE_3,
    OT_NOISE_XK_STAGE_SPLIT,
    OT_NOISE_XK_STAGE_FINISHED,
    OT_NOISE_XK_STAGE_FAILED
} ot_noise_xk_stage;

typedef struct ot_noise_xk_keypair {
    uint8_t secret[OT_NOISE_XK_KEY_BYTES];
    uint8_t public_key[OT_NOISE_XK_KEY_BYTES];
} ot_noise_xk_keypair;

typedef struct ot_noise_xk_state {
    ot_noise_xk_role role;
    ot_noise_xk_stage stage;
    uint8_t chaining_key[OT_NOISE_XK_HASH_BYTES];
    uint8_t handshake_hash[OT_NOISE_XK_HASH_BYTES];
    uint8_t cipher_key[OT_NOISE_XK_KEY_BYTES];
    uint8_t local_static_secret[OT_NOISE_XK_KEY_BYTES];
    uint8_t local_static_public[OT_NOISE_XK_KEY_BYTES];
    uint8_t remote_static_public[OT_NOISE_XK_KEY_BYTES];
    uint8_t local_ephemeral_secret[OT_NOISE_XK_KEY_BYTES];
    uint8_t local_ephemeral_public[OT_NOISE_XK_KEY_BYTES];
    uint8_t remote_ephemeral_public[OT_NOISE_XK_KEY_BYTES];
    uint64_t nonce;
    uint8_t has_cipher_key;
} ot_noise_xk_state;

/*
 * Benchmark-only structural composition. The caller supplies pre-generated,
 * deterministic keypairs so this adapter performs no entropy operation.
 * Production use, packet-v1 use, key storage, and benchmark execution are not
 * authorized by this source.
 */
int ot_noise_xk_init_initiator(
    ot_noise_xk_state *state,
    const ot_noise_xk_keypair *initiator_static,
    const ot_noise_xk_keypair *initiator_ephemeral,
    const uint8_t responder_static_public[OT_NOISE_XK_KEY_BYTES],
    const uint8_t *prologue,
    size_t prologue_size);

int ot_noise_xk_init_responder(
    ot_noise_xk_state *state,
    const ot_noise_xk_keypair *responder_static,
    const ot_noise_xk_keypair *responder_ephemeral,
    const uint8_t *prologue,
    size_t prologue_size);

int ot_noise_xk_write_message(
    ot_noise_xk_state *state,
    uint8_t *message,
    size_t message_capacity,
    size_t *message_size);

int ot_noise_xk_read_message(
    ot_noise_xk_state *state,
    const uint8_t *message,
    size_t message_size);

int ot_noise_xk_split(
    ot_noise_xk_state *state,
    uint8_t transmit_key[OT_NOISE_XK_KEY_BYTES],
    uint8_t receive_key[OT_NOISE_XK_KEY_BYTES]);

void ot_noise_xk_abort(ot_noise_xk_state *state);

#ifdef __cplusplus
}
#endif

#endif
