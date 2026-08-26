#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "monocypher-ed25519.h"
#include "monocypher.h"
#include "monocypher_benchmark_api.h"
#include "ot129_control_protocol.h"

#define OT121_CANDIDATE_ID "monocypher"
#define OT121_LOCAL_OPERATIONS_REQUIRED 5U
#include "ot121_benchmark_frame.h"

#define OT123_MESSAGE_BYTES 128U
#define OT123_AD_BYTES 16U
#define OT123_COLD_SWEEP_BYTES (32U * 1024U)
#define OT123_USB_TX_BUFFER_BYTES (16U * 1024U)
#define OT123_USB_RX_BUFFER_BYTES 256U
#define OT123_BENCHMARK_TASK_STACK_BYTES (8U * 1024U)

_Static_assert(CONFIG_LOG_DYNAMIC_LEVEL_CONTROL == 1,
               "OT-123 runtime log suppression requires dynamic log control");
_Static_assert(OT123_USB_RX_BUFFER_BYTES > 64U,
               "OT-123 USB-JTAG RX ring buffer must exceed one endpoint packet");
_Static_assert(OT123_USB_TX_BUFFER_BYTES >= OT121_FRAME_CHUNK_BYTES,
               "OT-123 USB-JTAG TX ring buffer must hold one chunk");

typedef int (*ot123_operation_fn)(void);

typedef struct {
    const char *name;
    ot123_operation_fn invoke;
} ot123_operation;

typedef struct {
    uint64_t duration_us;
    bool passed;
} ot123_sample;

static void ot123_benchmark_task(void *context);

static void ot129_wait_for_start(void)
{
    ot129_control_state state;
    uint8_t input[64];
    ot129_control_init(&state);

    while (!state.started) {
        int received = usb_serial_jtag_read_bytes(
            input, sizeof(input), pdMS_TO_TICKS(250U));
        if (received > 0) {
            (void) ot129_control_feed(&state, input, (size_t) received);
        }
    }
    const char ready[] = OT129_CONTROL_READY;
    int written = usb_serial_jtag_write_bytes(
        ready, sizeof(ready) - 1U, pdMS_TO_TICKS(1000U));
    ESP_ERROR_CHECK(written == (int) (sizeof(ready) - 1U)
                        ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000U)));
}

static uint8_t g_message[OT123_MESSAGE_BYTES];
static uint8_t g_ad[OT123_AD_BYTES];
static uint8_t g_sign_seed[32];
static uint8_t g_sign_secret[OT_MONOCYPHER_ED25519_SECRET_BYTES];
static uint8_t g_sign_public[OT_MONOCYPHER_KEY_BYTES];
static uint8_t g_signature[OT_MONOCYPHER_ED25519_SIGNATURE_BYTES];
static uint8_t g_x_secret_a[OT_MONOCYPHER_KEY_BYTES];
static uint8_t g_x_secret_b[OT_MONOCYPHER_KEY_BYTES];
static uint8_t g_x_public_b[OT_MONOCYPHER_KEY_BYTES];
static uint8_t g_aead_key[OT_MONOCYPHER_KEY_BYTES];
static uint8_t g_aead_nonce[OT_MONOCYPHER_IETF_NONCE_BYTES];
static uint8_t g_ciphertext[OT123_MESSAGE_BYTES + OT_MONOCYPHER_AEAD_TAG_BYTES];
static size_t g_ciphertext_size;
static uint8_t g_plaintext[OT123_MESSAGE_BYTES];
static volatile uint8_t g_cache_sweep[OT123_COLD_SWEEP_BYTES];
static volatile uint8_t g_sink;
static ot123_sample g_samples[OT121_COLD_REPETITIONS];
static uint64_t g_sorted[OT121_COLD_REPETITIONS];

static const uint8_t k_x_public_a[OT_MONOCYPHER_KEY_BYTES] = {
    0x85, 0x20, 0xf0, 0x09, 0x89, 0x30, 0xa7, 0x54,
    0x74, 0x8b, 0x7d, 0xdc, 0xb4, 0x3e, 0xf7, 0x5a,
    0x0d, 0xbf, 0x3a, 0x0d, 0x26, 0x38, 0x1a, 0xf4,
    0xeb, 0xa4, 0xa9, 0x8e, 0xaa, 0x9b, 0x4e, 0x6a
};

static const uint8_t k_x_public_b[OT_MONOCYPHER_KEY_BYTES] = {
    0xde, 0x9e, 0xdb, 0x7d, 0x7b, 0x7d, 0xc1, 0xb4,
    0xd3, 0x5b, 0x61, 0xc2, 0xec, 0xe4, 0x35, 0x37,
    0x3f, 0x83, 0x43, 0xc8, 0x5b, 0x78, 0x67, 0x4d,
    0xad, 0xfc, 0x7e, 0x14, 0x6f, 0x88, 0x2b, 0x4f
};

static const uint8_t k_x_shared[OT_MONOCYPHER_KEY_BYTES] = {
    0x4a, 0x5d, 0x9d, 0x5b, 0xa4, 0xce, 0x2d, 0xe1,
    0x72, 0x8e, 0x3b, 0xf4, 0x80, 0x35, 0x0f, 0x25,
    0xe0, 0x7e, 0x21, 0xc9, 0x47, 0xd1, 0x9e, 0x33,
    0x76, 0xf0, 0x9b, 0x3c, 0x1e, 0x16, 0x17, 0x42
};

static bool any_nonzero(const uint8_t *value, size_t size)
{
    uint8_t combined = 0U;
    for (size_t i = 0; i < size; ++i) {
        combined |= value[i];
    }
    return combined != 0U;
}

static int op_ed25519_sign(void)
{
    int rc = ot_monocypher_ed25519_sign(g_signature, g_sign_secret,
                                         g_message, sizeof(g_message));
    if (rc == 0) {
        g_sink ^= g_signature[0];
    }
    return rc;
}

static int op_ed25519_verify(void)
{
    int rc = ot_monocypher_ed25519_verify(g_signature, g_sign_public,
                                           g_message, sizeof(g_message));
    if (rc == 0) {
        g_sink ^= g_signature[1];
    }
    return rc;
}

static int op_x25519(void)
{
    uint8_t shared[OT_MONOCYPHER_KEY_BYTES] = { 0 };
    int rc = ot_monocypher_x25519(shared, g_x_secret_a, g_x_public_b);
    if (rc == 0 && any_nonzero(shared, sizeof(shared))) {
        g_sink ^= shared[0];
    } else {
        rc = -1;
    }
    crypto_wipe(shared, sizeof(shared));
    return rc;
}

static int op_chacha20poly1305_encrypt(void)
{
    size_t output_size = 0U;
    int rc = ot_monocypher_chacha20poly1305_ietf_encrypt(
        g_ciphertext, sizeof(g_ciphertext), &output_size,
        g_aead_key, g_aead_nonce, g_ad, sizeof(g_ad),
        g_message, sizeof(g_message));
    if (rc == 0 && output_size == sizeof(g_ciphertext)) {
        g_ciphertext_size = output_size;
        g_sink ^= g_ciphertext[0];
        return 0;
    }
    g_ciphertext_size = 0U;
    return -1;
}

static int op_chacha20poly1305_decrypt(void)
{
    size_t output_size = 0U;
    int rc = ot_monocypher_chacha20poly1305_ietf_decrypt(
        g_plaintext, sizeof(g_plaintext), &output_size,
        g_aead_key, g_aead_nonce, g_ad, sizeof(g_ad),
        g_ciphertext, g_ciphertext_size);
    if (rc == 0 && output_size == sizeof(g_message) &&
        memcmp(g_plaintext, g_message, sizeof(g_message)) == 0) {
        g_sink ^= g_plaintext[0];
        return 0;
    }
    return -1;
}

static const ot123_operation k_operations[] = {
    { "ed25519_sign", op_ed25519_sign },
    { "ed25519_verify", op_ed25519_verify },
    { "x25519", op_x25519 },
    { "chacha20poly1305_encrypt", op_chacha20poly1305_encrypt },
    { "chacha20poly1305_decrypt", op_chacha20poly1305_decrypt }
};

_Static_assert(sizeof(k_operations) / sizeof(k_operations[0]) ==
                   OT121_LOCAL_OPERATIONS_REQUIRED,
               "OT-123 operation table must match the frame contract");

static void initialize_inputs(void)
{
    static const uint8_t x_secret_a[OT_MONOCYPHER_KEY_BYTES] = {
        0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5, 0x7d,
        0x3c, 0x16, 0xc1, 0x72, 0x51, 0xb2, 0x66, 0x45,
        0xdf, 0x4c, 0x2f, 0x87, 0xeb, 0xc0, 0x99, 0x2a,
        0xb1, 0x77, 0xfb, 0xa5, 0x1d, 0xb9, 0x2c, 0x2a
    };
    static const uint8_t x_secret_b[OT_MONOCYPHER_KEY_BYTES] = {
        0x5d, 0xab, 0x08, 0x7e, 0x62, 0x4a, 0x8a, 0x4b,
        0x79, 0xe1, 0x7f, 0x8b, 0x83, 0x80, 0x0e, 0xe6,
        0x6f, 0x3b, 0xb1, 0x29, 0x26, 0x18, 0xb6, 0xfd,
        0x1c, 0x2f, 0x8b, 0x27, 0xff, 0x88, 0xe0, 0xeb
    };

    for (size_t i = 0; i < sizeof(g_message); ++i) {
        g_message[i] = (uint8_t) (i ^ 0x5aU);
    }
    for (size_t i = 0; i < sizeof(g_ad); ++i) {
        g_ad[i] = (uint8_t) (0xa0U + i);
    }
    for (size_t i = 0; i < sizeof(g_sign_seed); ++i) {
        g_sign_seed[i] = (uint8_t) i;
    }
    for (size_t i = 0; i < sizeof(g_aead_key); ++i) {
        g_aead_key[i] = (uint8_t) (0x20U + i);
    }
    for (size_t i = 0; i < sizeof(g_aead_nonce); ++i) {
        g_aead_nonce[i] = (uint8_t) (0x70U + i);
    }
    memcpy(g_x_secret_a, x_secret_a, sizeof(g_x_secret_a));
    memcpy(g_x_secret_b, x_secret_b, sizeof(g_x_secret_b));
    memcpy(g_x_public_b, k_x_public_b, sizeof(g_x_public_b));
    crypto_ed25519_key_pair(g_sign_secret, g_sign_public, g_sign_seed);
}

static void clear_sensitive_state(void)
{
    crypto_wipe(g_message, sizeof(g_message));
    crypto_wipe(g_ad, sizeof(g_ad));
    crypto_wipe(g_sign_seed, sizeof(g_sign_seed));
    crypto_wipe(g_sign_secret, sizeof(g_sign_secret));
    crypto_wipe(g_sign_public, sizeof(g_sign_public));
    crypto_wipe(g_signature, sizeof(g_signature));
    crypto_wipe(g_x_secret_a, sizeof(g_x_secret_a));
    crypto_wipe(g_x_secret_b, sizeof(g_x_secret_b));
    crypto_wipe(g_x_public_b, sizeof(g_x_public_b));
    crypto_wipe(g_aead_key, sizeof(g_aead_key));
    crypto_wipe(g_aead_nonce, sizeof(g_aead_nonce));
    crypto_wipe(g_ciphertext, sizeof(g_ciphertext));
    crypto_wipe(g_plaintext, sizeof(g_plaintext));
    g_ciphertext_size = 0U;
}

static bool primitive_vectors_and_negative_cases(void)
{
    uint8_t public_a[OT_MONOCYPHER_KEY_BYTES] = { 0 };
    uint8_t public_b[OT_MONOCYPHER_KEY_BYTES] = { 0 };
    uint8_t shared_ab[OT_MONOCYPHER_KEY_BYTES] = { 0 };
    uint8_t shared_ba[OT_MONOCYPHER_KEY_BYTES] = { 0 };
    uint8_t scratch[OT123_MESSAGE_BYTES] = { 0 };
    size_t scratch_size = 0U;
    uint8_t ed_seed[32] = { 0 };
    uint8_t ed_secret[OT_MONOCYPHER_ED25519_SECRET_BYTES] = { 0 };
    uint8_t ed_public[OT_MONOCYPHER_KEY_BYTES] = { 0 };
    uint8_t ed_signature[OT_MONOCYPHER_ED25519_SIGNATURE_BYTES] = { 0 };
    uint8_t low_order_shared[OT_MONOCYPHER_KEY_BYTES] = { 0 };
    uint8_t aead_cipher[130] = { 0 };
    uint8_t aead_plain[114] = { 0 };
    size_t aead_cipher_size = 0U;
    size_t aead_plain_size = 0U;
    bool passed = false;

    /* RFC 8032 Section 7.1, TEST 1. */
    static const uint8_t k_ed_seed[32] = {
        0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60,
        0xba, 0x84, 0x4a, 0xf4, 0x92, 0xec, 0x2c, 0xc4,
        0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32, 0x69, 0x19,
        0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60
    };
    static const uint8_t k_ed_public[32] = {
        0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7,
        0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a,
        0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25,
        0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a
    };
    static const uint8_t k_ed_signature[64] = {
        0xe5, 0x56, 0x43, 0x00, 0xc3, 0x60, 0xac, 0x72,
        0x90, 0x86, 0xe2, 0xcc, 0x80, 0x6e, 0x82, 0x8a,
        0x84, 0x87, 0x7f, 0x1e, 0xb8, 0xe5, 0xd9, 0x74,
        0xd8, 0x73, 0xe0, 0x65, 0x22, 0x49, 0x01, 0x55,
        0x5f, 0xb8, 0x82, 0x15, 0x90, 0xa3, 0x3b, 0xac,
        0xc6, 0x1e, 0x39, 0x70, 0x1c, 0xf9, 0xb4, 0x6b,
        0xd2, 0x5b, 0xf5, 0xf0, 0x59, 0x5b, 0xbe, 0x24,
        0x65, 0x51, 0x41, 0x43, 0x8e, 0x7a, 0x10, 0x0b
    };
    /* Vendored Monocypher low-order X25519 vector; an all-zero result is rejected. */
    static const uint8_t k_low_order_scalar[32] = {
        0x78, 0x6a, 0x33, 0xa4, 0xf7, 0xaf, 0x29, 0x7a,
        0x20, 0xe7, 0x64, 0x29, 0x25, 0x93, 0x2b, 0xf5,
        0x09, 0xe7, 0x07, 0x0f, 0xa1, 0xbc, 0x36, 0x98,
        0x6a, 0xf1, 0xeb, 0x13, 0xf4, 0xf5, 0x0b, 0x55
    };
    static const uint8_t k_low_order_public[32] = { 0 };
    /* RFC 8439 Section 2.8.2; adapter wire order is ciphertext then tag. */
    static const uint8_t k_aead_key[32] = {
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f
    };
    static const uint8_t k_aead_nonce[12] = {
        0x07, 0x00, 0x00, 0x00, 0x40, 0x41, 0x42, 0x43,
        0x44, 0x45, 0x46, 0x47
    };
    static const uint8_t k_aead_ad[12] = {
        0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1, 0xc2, 0xc3,
        0xc4, 0xc5, 0xc6, 0xc7
    };
    static const uint8_t k_aead_plain[114] = {
        0x4c, 0x61, 0x64, 0x69, 0x65, 0x73, 0x20, 0x61,
        0x6e, 0x64, 0x20, 0x47, 0x65, 0x6e, 0x74, 0x6c,
        0x65, 0x6d, 0x65, 0x6e, 0x20, 0x6f, 0x66, 0x20,
        0x74, 0x68, 0x65, 0x20, 0x63, 0x6c, 0x61, 0x73,
        0x73, 0x20, 0x6f, 0x66, 0x20, 0x27, 0x39, 0x39,
        0x3a, 0x20, 0x49, 0x66, 0x20, 0x49, 0x20, 0x63,
        0x6f, 0x75, 0x6c, 0x64, 0x20, 0x6f, 0x66, 0x66,
        0x65, 0x72, 0x20, 0x79, 0x6f, 0x75, 0x20, 0x6f,
        0x6e, 0x6c, 0x79, 0x20, 0x6f, 0x6e, 0x65, 0x20,
        0x74, 0x69, 0x70, 0x20, 0x66, 0x6f, 0x72, 0x20,
        0x74, 0x68, 0x65, 0x20, 0x66, 0x75, 0x74, 0x75,
        0x72, 0x65, 0x2c, 0x20, 0x73, 0x75, 0x6e, 0x73,
        0x63, 0x72, 0x65, 0x65, 0x6e, 0x20, 0x77, 0x6f,
        0x75, 0x6c, 0x64, 0x20, 0x62, 0x65, 0x20, 0x69,
        0x74, 0x2e
    };
    static const uint8_t k_aead_cipher_and_tag[130] = {
        0xd3, 0x1a, 0x8d, 0x34, 0x64, 0x8e, 0x60, 0xdb,
        0x7b, 0x86, 0xaf, 0xbc, 0x53, 0xef, 0x7e, 0xc2,
        0xa4, 0xad, 0xed, 0x51, 0x29, 0x6e, 0x08, 0xfe,
        0xa9, 0xe2, 0xb5, 0xa7, 0x36, 0xee, 0x62, 0xd6,
        0x3d, 0xbe, 0xa4, 0x5e, 0x8c, 0xa9, 0x67, 0x12,
        0x82, 0xfa, 0xfb, 0x69, 0xda, 0x92, 0x72, 0x8b,
        0x1a, 0x71, 0xde, 0x0a, 0x9e, 0x06, 0x0b, 0x29,
        0x05, 0xd6, 0xa5, 0xb6, 0x7e, 0xcd, 0x3b, 0x36,
        0x92, 0xdd, 0xbd, 0x7f, 0x2d, 0x77, 0x8b, 0x8c,
        0x98, 0x03, 0xae, 0xe3, 0x28, 0x09, 0x1b, 0x58,
        0xfa, 0xb3, 0x24, 0xe4, 0xfa, 0xd6, 0x75, 0x94,
        0x55, 0x85, 0x80, 0x8b, 0x48, 0x31, 0xd7, 0xbc,
        0x3f, 0xf4, 0xde, 0xf0, 0x8e, 0x4b, 0x7a, 0x9d,
        0xe5, 0x76, 0xd2, 0x65, 0x86, 0xce, 0xc6, 0x4b,
        0x61, 0x16, 0x1a, 0xe1, 0x0b, 0x59, 0x4f, 0x09,
        0xe2, 0x6a, 0x7e, 0x90, 0x2e, 0xcb, 0xd0, 0x60,
        0x06, 0x91
    };

    memcpy(ed_seed, k_ed_seed, sizeof(ed_seed));
    crypto_ed25519_key_pair(ed_secret, ed_public, ed_seed);
    if (memcmp(ed_public, k_ed_public, sizeof(ed_public)) != 0 ||
        ot_monocypher_ed25519_sign(ed_signature, ed_secret, NULL, 0U) != 0 ||
        memcmp(ed_signature, k_ed_signature, sizeof(ed_signature)) != 0 ||
        ot_monocypher_ed25519_verify(k_ed_signature, k_ed_public, NULL, 0U) != 0) {
        goto cleanup;
    }

    if (ot_monocypher_x25519(low_order_shared, k_low_order_scalar,
                             k_low_order_public) != 0 ||
        any_nonzero(low_order_shared, sizeof(low_order_shared))) {
        goto cleanup;
    }

    if (ot_monocypher_chacha20poly1305_ietf_encrypt(
            aead_cipher, sizeof(aead_cipher), &aead_cipher_size,
            k_aead_key, k_aead_nonce, k_aead_ad, sizeof(k_aead_ad),
            k_aead_plain, sizeof(k_aead_plain)) != 0 ||
        aead_cipher_size != sizeof(k_aead_cipher_and_tag) ||
        memcmp(aead_cipher, k_aead_cipher_and_tag,
               sizeof(k_aead_cipher_and_tag)) != 0 ||
        ot_monocypher_chacha20poly1305_ietf_decrypt(
            aead_plain, sizeof(aead_plain), &aead_plain_size,
            k_aead_key, k_aead_nonce, k_aead_ad, sizeof(k_aead_ad),
            k_aead_cipher_and_tag, sizeof(k_aead_cipher_and_tag)) != 0 ||
        aead_plain_size != sizeof(k_aead_plain) ||
        memcmp(aead_plain, k_aead_plain, sizeof(k_aead_plain)) != 0) {
        goto cleanup;
    }

    if (op_ed25519_sign() != 0 || op_ed25519_verify() != 0) {
        goto cleanup;
    }
    g_signature[0] ^= 0x01U;
    if (ot_monocypher_ed25519_verify(g_signature, g_sign_public,
                                     g_message, sizeof(g_message)) == 0) {
        g_signature[0] ^= 0x01U;
        goto cleanup;
    }
    g_signature[0] ^= 0x01U;

    crypto_x25519_public_key(public_a, g_x_secret_a);
    crypto_x25519_public_key(public_b, g_x_secret_b);
    if (memcmp(public_a, k_x_public_a, sizeof(public_a)) != 0 ||
        memcmp(public_b, k_x_public_b, sizeof(public_b)) != 0 ||
        ot_monocypher_x25519(shared_ab, g_x_secret_a, public_b) != 0 ||
        ot_monocypher_x25519(shared_ba, g_x_secret_b, public_a) != 0 ||
        memcmp(shared_ab, shared_ba, sizeof(shared_ab)) != 0 ||
        memcmp(shared_ab, k_x_shared, sizeof(shared_ab)) != 0 ||
        !any_nonzero(shared_ab, sizeof(shared_ab))) {
        goto cleanup;
    }

    if (op_chacha20poly1305_encrypt() != 0 ||
        op_chacha20poly1305_decrypt() != 0) {
        goto cleanup;
    }
    g_ciphertext[g_ciphertext_size - 1U] ^= 0x80U;
    memset(scratch, 0xcc, sizeof(scratch));
    bool tamper_rejected = ot_monocypher_chacha20poly1305_ietf_decrypt(
        scratch, sizeof(scratch), &scratch_size,
        g_aead_key, g_aead_nonce, g_ad, sizeof(g_ad),
        g_ciphertext, g_ciphertext_size) != 0;
    g_ciphertext[g_ciphertext_size - 1U] ^= 0x80U;
    if (!tamper_rejected || scratch_size != 0U) {
        goto cleanup;
    }
    for (size_t i = 0; i < sizeof(g_message); ++i) {
        if (scratch[i] != 0U) {
            goto cleanup;
        }
    }
    passed = true;

cleanup:
    crypto_wipe(public_a, sizeof(public_a));
    crypto_wipe(public_b, sizeof(public_b));
    crypto_wipe(shared_ab, sizeof(shared_ab));
    crypto_wipe(shared_ba, sizeof(shared_ba));
    crypto_wipe(scratch, sizeof(scratch));
    crypto_wipe(ed_seed, sizeof(ed_seed));
    crypto_wipe(ed_secret, sizeof(ed_secret));
    crypto_wipe(ed_public, sizeof(ed_public));
    crypto_wipe(ed_signature, sizeof(ed_signature));
    crypto_wipe(low_order_shared, sizeof(low_order_shared));
    crypto_wipe(aead_cipher, sizeof(aead_cipher));
    crypto_wipe(aead_plain, sizeof(aead_plain));
    return passed;
}

static void cold_condition(void)
{
    uint8_t accumulator = g_sink;
    for (size_t i = 0; i < sizeof(g_cache_sweep); i += 32U) {
        accumulator ^= g_cache_sweep[i];
    }
    g_sink = accumulator;
}

static void sort_durations(uint64_t *durations, size_t count)
{
    for (size_t i = 1; i < count; ++i) {
        uint64_t value = durations[i];
        size_t j = i;
        while (j > 0U && durations[j - 1U] > value) {
            durations[j] = durations[j - 1U];
            --j;
        }
        durations[j] = value;
    }
}

static bool run_phase(const ot123_operation *operation,
                      const char *phase,
                      unsigned repetitions,
                      bool conditioned)
{
    bool passed = true;

    if (!conditioned && operation->invoke() != 0) {
        passed = false;
    }
    for (unsigned iteration = 0; iteration < repetitions; ++iteration) {
        if (conditioned) {
            cold_condition();
        }
        __asm__ __volatile__("" ::: "memory");
        int64_t started = esp_timer_get_time();
        int rc = operation->invoke();
        int64_t finished = esp_timer_get_time();
        __asm__ __volatile__("" ::: "memory");
        g_samples[iteration].duration_us = (uint64_t) (finished - started);
        g_samples[iteration].passed = rc == 0;
        passed = passed && g_samples[iteration].passed;
    }
    for (unsigned iteration = 0; iteration < repetitions; ++iteration) {
        ot121_frame_sample(operation->name, phase, iteration,
                           g_samples[iteration].duration_us,
                           g_samples[iteration].passed);
        g_sorted[iteration] = g_samples[iteration].duration_us;
    }
    sort_durations(g_sorted, repetitions);
    uint64_t median = (g_sorted[(repetitions / 2U) - 1U] +
                       g_sorted[repetitions / 2U]) / 2U;
    uint64_t p95 = g_sorted[((95U * repetitions) + 99U) / 100U - 1U];
    ot121_frame_summary(operation->name, phase, g_sorted[0], median, p95,
                        g_sorted[repetitions - 1U], passed);
    crypto_wipe(g_samples, sizeof(g_samples));
    crypto_wipe(g_sorted, sizeof(g_sorted));
    return passed;
}

static int ot123_discard_log_vprintf(const char *format, va_list arguments)
{
    (void) format;
    (void) arguments;
    return 0;
}

static void install_buffered_usb_serial_jtag_protocol(void)
{
    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = OT123_USB_TX_BUFFER_BYTES,
        .rx_buffer_size = OT123_USB_RX_BUFFER_BYTES,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
}

void app_main(void)
{
    (void) esp_log_set_vprintf(ot123_discard_log_vprintf);
    esp_log_level_set("*", ESP_LOG_NONE);
    install_buffered_usb_serial_jtag_protocol();
    BaseType_t created = xTaskCreatePinnedToCore(
        ot123_benchmark_task, "ot123_bench",
        OT123_BENCHMARK_TASK_STACK_BYTES, NULL,
        tskIDLE_PRIORITY + 1, NULL, xPortGetCoreID());
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

static void ot123_benchmark_task(void *context)
{
    (void) context;
    bool passed = false;
    unsigned completed = 0U;
    size_t heap_start_free_bytes = 0U;
    size_t heap_min_free_bytes = 0U;
    size_t peak_dynamic_ram_bytes = 0U;
    size_t stack_high_water_free_bytes = 0U;
    size_t max_stack_used_bytes = 0U;
    bool heap_monitor_started = false;

    ot129_wait_for_start();
    ot121_frame_header();
    initialize_inputs();

    heap_start_free_bytes = heap_caps_get_free_size(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (heap_caps_monitor_local_minimum_free_size_start() != ESP_OK) {
        goto cleanup;
    }
    heap_monitor_started = true;

    bool vectors_passed = primitive_vectors_and_negative_cases();
    ot121_frame_gate("primitive_vectors_and_negative_cases", vectors_passed);
    if (!vectors_passed) {
        goto cleanup;
    }

    passed = true;
    for (size_t i = 0; i < sizeof(k_operations) / sizeof(k_operations[0]); ++i) {
        bool conditioned_passed = run_phase(
            &k_operations[i], "cold", OT121_COLD_REPETITIONS, true);
        bool warm_passed = run_phase(
            &k_operations[i], "warm", OT121_WARM_REPETITIONS, false);
        if (conditioned_passed && warm_passed) {
            ++completed;
        } else {
            passed = false;
        }
    }

cleanup:
    if (heap_monitor_started) {
        heap_min_free_bytes = heap_caps_get_minimum_free_size(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ESP_ERROR_CHECK(heap_caps_monitor_local_minimum_free_size_stop());
        peak_dynamic_ram_bytes = heap_start_free_bytes >= heap_min_free_bytes
                                     ? heap_start_free_bytes - heap_min_free_bytes
                                     : 0U;
        stack_high_water_free_bytes = (size_t) uxTaskGetStackHighWaterMark2(NULL);
        max_stack_used_bytes =
            stack_high_water_free_bytes <= OT123_BENCHMARK_TASK_STACK_BYTES
                ? OT123_BENCHMARK_TASK_STACK_BYTES - stack_high_water_free_bytes
                : OT123_BENCHMARK_TASK_STACK_BYTES;
    }
    clear_sensitive_state();
    if (heap_monitor_started) {
        ot121_frame_runtime_resources(
            heap_start_free_bytes, heap_min_free_bytes,
            peak_dynamic_ram_bytes, OT123_BENCHMARK_TASK_STACK_BYTES,
            stack_high_water_free_bytes, max_stack_used_bytes, 0U);
    }
    ot121_frame_local_complete(completed, passed);
    vTaskDelete(NULL);
}