#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ot121_benchmark_frame.h"
#include "sodium.h"

#define OT121_MESSAGE_BYTES 128U
#define OT121_AD_BYTES 16U
#define OT121_COLD_SWEEP_BYTES (32U * 1024U)
#define OT121_USB_TX_BUFFER_BYTES (16U * 1024U)
#define OT121_USB_RX_BUFFER_BYTES 256U
#define OT121_BENCHMARK_TASK_STACK_BYTES (8U * 1024U)

_Static_assert(CONFIG_LOG_DYNAMIC_LEVEL_CONTROL == 1,
               "OT-121 runtime log suppression requires dynamic log control");
_Static_assert(OT121_USB_RX_BUFFER_BYTES > 64U,
               "OT-121 USB-JTAG RX ring buffer must exceed one endpoint packet");
_Static_assert(OT121_USB_TX_BUFFER_BYTES >= OT121_FRAME_CHUNK_BYTES,
               "OT-121 USB-JTAG TX ring buffer must hold one chunk");

typedef int (*ot121_operation_fn)(void);

typedef struct {
    const char *name;
    ot121_operation_fn invoke;
} ot121_operation;

typedef struct {
    uint64_t duration_us;
    bool passed;
} ot121_sample;

static void ot121_benchmark_task(void *context);

static unsigned char g_message[OT121_MESSAGE_BYTES];
static unsigned char g_ad[OT121_AD_BYTES];
static unsigned char g_sign_seed[crypto_sign_SEEDBYTES];
static unsigned char g_sign_pk[crypto_sign_PUBLICKEYBYTES];
static unsigned char g_sign_sk[crypto_sign_SECRETKEYBYTES];
static unsigned char g_signature[crypto_sign_BYTES];
static unsigned char g_x_scalar_a[crypto_scalarmult_curve25519_SCALARBYTES];
static unsigned char g_x_scalar_b[crypto_scalarmult_curve25519_SCALARBYTES];
static unsigned char g_x_public_b[crypto_scalarmult_curve25519_BYTES];
static unsigned char g_hash[crypto_hash_sha256_BYTES];
static unsigned char g_hkdf_prk[crypto_kdf_hkdf_sha256_KEYBYTES];
static unsigned char g_hkdf_out[42];
static unsigned char g_aead_key[crypto_aead_chacha20poly1305_ietf_KEYBYTES];
static unsigned char g_aead_nonce[crypto_aead_chacha20poly1305_ietf_NPUBBYTES];
static unsigned char g_ciphertext[OT121_MESSAGE_BYTES + crypto_aead_chacha20poly1305_ietf_ABYTES];
static unsigned long long g_ciphertext_len;
static unsigned char g_plaintext[OT121_MESSAGE_BYTES];
static volatile unsigned char g_cache_sweep[OT121_COLD_SWEEP_BYTES];
static volatile unsigned char g_sink;
static ot121_sample g_samples[OT121_COLD_REPETITIONS];
static uint64_t g_sorted[OT121_COLD_REPETITIONS];

static const unsigned char k_sha256_abc[crypto_hash_sha256_BYTES] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
    0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
};

static const unsigned char k_hkdf_prk[crypto_kdf_hkdf_sha256_KEYBYTES] = {
    0x07, 0x77, 0x09, 0x36, 0x2c, 0x2e, 0x32, 0xdf,
    0x0d, 0xdc, 0x3f, 0x0d, 0xc4, 0x7b, 0xba, 0x63,
    0x90, 0xb6, 0xc7, 0x3b, 0xb5, 0x0f, 0x9c, 0x31,
    0x22, 0xec, 0x84, 0x4a, 0xd7, 0xc2, 0xb3, 0xe5
};

static const unsigned char k_hkdf_okm[42] = {
    0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a,
    0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36, 0x2f, 0x2a,
    0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c,
    0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf,
    0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18,
    0x58, 0x65
};

static const unsigned char k_hkdf_salt[13] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c
};

static const unsigned char k_hkdf_info[10] = {
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9
};

static int op_ed25519_sign(void)
{
    unsigned long long signature_len = 0;
    int rc = crypto_sign_detached(g_signature,
                                  &signature_len,
                                  g_message,
                                  sizeof(g_message),
                                  g_sign_sk);
    if (rc == 0 && signature_len == crypto_sign_BYTES) {
        g_sink ^= g_signature[0];
        return 0;
    }
    return -1;
}

static int op_ed25519_verify(void)
{
    int rc = crypto_sign_verify_detached(g_signature,
                                         g_message,
                                         sizeof(g_message),
                                         g_sign_pk);
    if (rc == 0) {
        g_sink ^= g_signature[1];
    }
    return rc;
}

static int op_x25519(void)
{
    unsigned char shared[crypto_scalarmult_curve25519_BYTES] = { 0 };
    int rc = crypto_scalarmult_curve25519(shared, g_x_scalar_a, g_x_public_b);
    if (rc == 0) {
        g_sink ^= shared[0];
    }
    sodium_memzero(shared, sizeof(shared));
    return rc;
}

static int op_sha256(void)
{
    int rc = crypto_hash_sha256(g_hash, g_message, sizeof(g_message));
    if (rc == 0) {
        g_sink ^= g_hash[0];
    }
    return rc;
}

static int op_hkdf_sha256(void)
{
    unsigned char ikm[22];
    memset(ikm, 0x0b, sizeof(ikm));
    int rc = crypto_kdf_hkdf_sha256_extract(g_hkdf_prk,
                                             k_hkdf_salt,
                                             sizeof(k_hkdf_salt),
                                             ikm,
                                             sizeof(ikm));
    if (rc == 0) {
        rc = crypto_kdf_hkdf_sha256_expand(g_hkdf_out,
                                            sizeof(g_hkdf_out),
                                            (const char *) k_hkdf_info,
                                            sizeof(k_hkdf_info),
                                            g_hkdf_prk);
    }
    if (rc == 0) {
        g_sink ^= g_hkdf_out[0];
    }
    sodium_memzero(ikm, sizeof(ikm));
    return rc;
}

static int op_chacha20poly1305_encrypt(void)
{
    int rc = crypto_aead_chacha20poly1305_ietf_encrypt(
        g_ciphertext,
        &g_ciphertext_len,
        g_message,
        sizeof(g_message),
        g_ad,
        sizeof(g_ad),
        NULL,
        g_aead_nonce,
        g_aead_key);
    if (rc == 0) {
        g_sink ^= g_ciphertext[0];
    }
    return rc;
}

static int op_chacha20poly1305_decrypt(void)
{
    unsigned long long plaintext_len = 0;
    int rc = crypto_aead_chacha20poly1305_ietf_decrypt(
        g_plaintext,
        &plaintext_len,
        NULL,
        g_ciphertext,
        g_ciphertext_len,
        g_ad,
        sizeof(g_ad),
        g_aead_nonce,
        g_aead_key);
    if (rc == 0 && plaintext_len == sizeof(g_message)) {
        g_sink ^= g_plaintext[0];
        return 0;
    }
    return -1;
}

static const ot121_operation k_operations[] = {
    { "ed25519_sign", op_ed25519_sign },
    { "ed25519_verify", op_ed25519_verify },
    { "x25519", op_x25519 },
    { "sha256", op_sha256 },
    { "hkdf_sha256", op_hkdf_sha256 },
    { "chacha20poly1305_encrypt", op_chacha20poly1305_encrypt },
    { "chacha20poly1305_decrypt", op_chacha20poly1305_decrypt }
};

static void initialize_inputs(void)
{
    for (size_t i = 0; i < sizeof(g_message); ++i) {
        g_message[i] = (unsigned char) (i ^ 0x5aU);
    }
    for (size_t i = 0; i < sizeof(g_ad); ++i) {
        g_ad[i] = (unsigned char) (0xa0U + i);
    }
    for (size_t i = 0; i < sizeof(g_sign_seed); ++i) {
        g_sign_seed[i] = (unsigned char) i;
    }
    for (size_t i = 0; i < sizeof(g_x_scalar_a); ++i) {
        g_x_scalar_a[i] = (unsigned char) (0x11U + i);
        g_x_scalar_b[i] = (unsigned char) (0x91U - i);
    }
    for (size_t i = 0; i < sizeof(g_aead_key); ++i) {
        g_aead_key[i] = (unsigned char) (0x20U + i);
    }
    for (size_t i = 0; i < sizeof(g_aead_nonce); ++i) {
        g_aead_nonce[i] = (unsigned char) (0x70U + i);
    }
}

static void clear_sensitive_state(void)
{
    sodium_memzero(g_message, sizeof(g_message));
    sodium_memzero(g_ad, sizeof(g_ad));
    sodium_memzero(g_sign_seed, sizeof(g_sign_seed));
    sodium_memzero(g_sign_pk, sizeof(g_sign_pk));
    sodium_memzero(g_sign_sk, sizeof(g_sign_sk));
    sodium_memzero(g_signature, sizeof(g_signature));
    sodium_memzero(g_x_scalar_a, sizeof(g_x_scalar_a));
    sodium_memzero(g_x_scalar_b, sizeof(g_x_scalar_b));
    sodium_memzero(g_x_public_b, sizeof(g_x_public_b));
    sodium_memzero(g_hash, sizeof(g_hash));
    sodium_memzero(g_hkdf_prk, sizeof(g_hkdf_prk));
    sodium_memzero(g_hkdf_out, sizeof(g_hkdf_out));
    sodium_memzero(g_aead_key, sizeof(g_aead_key));
    sodium_memzero(g_aead_nonce, sizeof(g_aead_nonce));
    sodium_memzero(g_ciphertext, sizeof(g_ciphertext));
    sodium_memzero(g_plaintext, sizeof(g_plaintext));
    g_ciphertext_len = 0;
}

static bool primitive_vectors_and_negative_cases(void)
{
    static const unsigned char abc[] = { 'a', 'b', 'c' };
    unsigned char hash[crypto_hash_sha256_BYTES] = { 0 };
    unsigned char public_a[crypto_scalarmult_curve25519_BYTES] = { 0 };
    unsigned char public_b[crypto_scalarmult_curve25519_BYTES] = { 0 };
    unsigned char shared_ab[crypto_scalarmult_curve25519_BYTES] = { 0 };
    unsigned char shared_ba[crypto_scalarmult_curve25519_BYTES] = { 0 };
    unsigned char zero_public[crypto_scalarmult_curve25519_BYTES] = { 0 };
    unsigned char scratch[OT121_MESSAGE_BYTES] = { 0 };
    unsigned long long scratch_len = 0;
    bool passed = false;

    if (crypto_hash_sha256(hash, abc, sizeof(abc)) != 0 ||
        sodium_memcmp(hash, k_sha256_abc, sizeof(hash)) != 0) {
        goto cleanup;
    }

    if (crypto_sign_seed_keypair(g_sign_pk, g_sign_sk, g_sign_seed) != 0 ||
        op_ed25519_sign() != 0 || op_ed25519_verify() != 0) {
        goto cleanup;
    }
    g_signature[0] ^= 0x01U;
    if (crypto_sign_verify_detached(g_signature,
                                    g_message,
                                    sizeof(g_message),
                                    g_sign_pk) == 0) {
        g_signature[0] ^= 0x01U;
        goto cleanup;
    }
    g_signature[0] ^= 0x01U;

    if (crypto_scalarmult_curve25519_base(public_a, g_x_scalar_a) != 0 ||
        crypto_scalarmult_curve25519_base(public_b, g_x_scalar_b) != 0 ||
        crypto_scalarmult_curve25519(shared_ab, g_x_scalar_a, public_b) != 0 ||
        crypto_scalarmult_curve25519(shared_ba, g_x_scalar_b, public_a) != 0 ||
        sodium_memcmp(shared_ab, shared_ba, sizeof(shared_ab)) != 0 ||
        crypto_scalarmult_curve25519(shared_ab, g_x_scalar_a, zero_public) == 0) {
        goto cleanup;
    }
    memcpy(g_x_public_b, public_b, sizeof(g_x_public_b));

    if (op_hkdf_sha256() != 0 ||
        sodium_memcmp(g_hkdf_prk, k_hkdf_prk, sizeof(k_hkdf_prk)) != 0 ||
        sodium_memcmp(g_hkdf_out, k_hkdf_okm, sizeof(k_hkdf_okm)) != 0) {
        goto cleanup;
    }

    if (op_chacha20poly1305_encrypt() != 0 ||
        op_chacha20poly1305_decrypt() != 0 ||
        sodium_memcmp(g_plaintext, g_message, sizeof(g_message)) != 0) {
        goto cleanup;
    }
    g_ciphertext[g_ciphertext_len - 1U] ^= 0x01U;
    bool tamper_rejected = crypto_aead_chacha20poly1305_ietf_decrypt(
        scratch,
        &scratch_len,
        NULL,
        g_ciphertext,
        g_ciphertext_len,
        g_ad,
        sizeof(g_ad),
        g_aead_nonce,
        g_aead_key) != 0;
    g_ciphertext[g_ciphertext_len - 1U] ^= 0x01U;
    if (!tamper_rejected) {
        goto cleanup;
    }
    passed = true;

cleanup:
    sodium_memzero(hash, sizeof(hash));
    sodium_memzero(public_a, sizeof(public_a));
    sodium_memzero(public_b, sizeof(public_b));
    sodium_memzero(shared_ab, sizeof(shared_ab));
    sodium_memzero(shared_ba, sizeof(shared_ba));
    sodium_memzero(zero_public, sizeof(zero_public));
    sodium_memzero(scratch, sizeof(scratch));
    return passed;
}

static void cold_condition(void)
{
    unsigned char accumulator = g_sink;
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
        while (j > 0 && durations[j - 1] > value) {
            durations[j] = durations[j - 1];
            --j;
        }
        durations[j] = value;
    }
}

static bool run_phase(const ot121_operation *operation,
                      const char *phase,
                      unsigned repetitions,
                      bool cold)
{
    bool passed = true;

    if (!cold && operation->invoke() != 0) {
        passed = false;
    }

    for (unsigned iteration = 0; iteration < repetitions; ++iteration) {
        if (cold) {
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
        ot121_frame_sample(operation->name,
                           phase,
                           iteration,
                           g_samples[iteration].duration_us,
                           g_samples[iteration].passed);
        g_sorted[iteration] = g_samples[iteration].duration_us;
    }

    sort_durations(g_sorted, repetitions);
    uint64_t median = (g_sorted[(repetitions / 2U) - 1U] +
                       g_sorted[repetitions / 2U]) / 2U;
    uint64_t p95 = g_sorted[((95U * repetitions) + 99U) / 100U - 1U];
    ot121_frame_summary(operation->name,
                        phase,
                        g_sorted[0],
                        median,
                        p95,
                        g_sorted[repetitions - 1U],
                        passed);
    sodium_memzero(g_samples, sizeof(g_samples));
    sodium_memzero(g_sorted, sizeof(g_sorted));
    return passed;
}

static int ot121_discard_log_vprintf(const char *format, va_list arguments)
{
    (void) format;
    (void) arguments;
    return 0;
}

static void install_buffered_usb_serial_jtag_protocol(void)
{
    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = OT121_USB_TX_BUFFER_BYTES,
        .rx_buffer_size = OT121_USB_RX_BUFFER_BYTES,
    };

    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
}

void app_main(void)
{
    (void) esp_log_set_vprintf(ot121_discard_log_vprintf);
    esp_log_level_set("*", ESP_LOG_NONE);
    install_buffered_usb_serial_jtag_protocol();

    BaseType_t created = xTaskCreatePinnedToCore(
        ot121_benchmark_task,
        "ot121_bench",
        OT121_BENCHMARK_TASK_STACK_BYTES,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL,
        xPortGetCoreID());
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

static void ot121_benchmark_task(void *context)
{
    (void) context;
    bool passed = false;
    unsigned completed = 0;

    vTaskDelay(pdMS_TO_TICKS(3000U));
    ot121_frame_header();
    initialize_inputs();

    bool initialized = sodium_init() >= 0;
    ot121_frame_gate("sodium_init", initialized);
    if (!initialized) {
        goto cleanup;
    }

    bool vectors_passed = primitive_vectors_and_negative_cases();
    ot121_frame_gate("primitive_vectors_and_negative_cases", vectors_passed);
    if (!vectors_passed) {
        goto cleanup;
    }

    passed = true;
    for (size_t i = 0; i < sizeof(k_operations) / sizeof(k_operations[0]); ++i) {
        bool cold_passed = run_phase(&k_operations[i],
                                     "cold",
                                     OT121_COLD_REPETITIONS,
                                     true);
        bool warm_passed = run_phase(&k_operations[i],
                                     "warm",
                                     OT121_WARM_REPETITIONS,
                                     false);
        if (cold_passed && warm_passed) {
            ++completed;
        } else {
            passed = false;
        }
    }

cleanup:
    clear_sensitive_state();
    ot121_frame_local_complete(completed, passed);
    vTaskDelete(NULL);
}
