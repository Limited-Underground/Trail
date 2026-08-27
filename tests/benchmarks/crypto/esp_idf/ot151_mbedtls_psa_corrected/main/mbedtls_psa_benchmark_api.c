#include "ot149_candidate_api.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mbedtls/platform_util.h"
#include "psa/crypto.h"

#define OT149_MESSAGE_BYTES 128U
#define OT149_AD_BYTES 16U
#define OT149_X25519_BYTES 32U
#define OT149_SHA256_BYTES 32U
#define OT149_HKDF_BYTES 42U
#define OT149_AEAD_KEY_BYTES 32U
#define OT149_AEAD_NONCE_BYTES 12U
#define OT149_AEAD_TAG_BYTES 16U

static mbedtls_svc_key_id_t g_x25519_key = MBEDTLS_SVC_KEY_ID_INIT;
static mbedtls_svc_key_id_t g_hkdf_key = MBEDTLS_SVC_KEY_ID_INIT;
static mbedtls_svc_key_id_t g_aead_key = MBEDTLS_SVC_KEY_ID_INIT;
static uint8_t g_message[OT149_MESSAGE_BYTES];
static uint8_t g_ad[OT149_AD_BYTES];
static uint8_t g_hash[OT149_SHA256_BYTES];
static uint8_t g_hkdf_output[OT149_HKDF_BYTES];
static uint8_t g_ciphertext[OT149_MESSAGE_BYTES + OT149_AEAD_TAG_BYTES];
static size_t g_ciphertext_length;
static uint8_t g_plaintext[OT149_MESSAGE_BYTES];
static volatile uint8_t g_sink;

static const uint8_t k_x25519_private[OT149_X25519_BYTES] = {
    0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5, 0x7d,
    0x3c, 0x16, 0xc1, 0x72, 0x51, 0xb2, 0x66, 0x45,
    0xdf, 0x4c, 0x2f, 0x87, 0xeb, 0xc0, 0x99, 0x2a,
    0xb1, 0x77, 0xfb, 0xa5, 0x1d, 0xb9, 0x2c, 0x2a,
};

static const uint8_t k_x25519_peer_public[OT149_X25519_BYTES] = {
    0xde, 0x9e, 0xdb, 0x7d, 0x7b, 0x7d, 0xc1, 0xb4,
    0xd3, 0x5b, 0x61, 0xc2, 0xec, 0xe4, 0x35, 0x37,
    0x3f, 0x83, 0x43, 0xc8, 0x5b, 0x78, 0x67, 0x4d,
    0xad, 0xfc, 0x7e, 0x14, 0x6f, 0x88, 0x2b, 0x4f,
};

static const uint8_t k_x25519_shared[OT149_X25519_BYTES] = {
    0x4a, 0x5d, 0x9d, 0x5b, 0xa4, 0xce, 0x2d, 0xe1,
    0x72, 0x8e, 0x3b, 0xf4, 0x80, 0x35, 0x0f, 0x25,
    0xe0, 0x7e, 0x21, 0xc9, 0x47, 0xd1, 0x9e, 0x33,
    0x76, 0xf0, 0x9b, 0x3c, 0x1e, 0x16, 0x17, 0x42,
};

static const uint8_t k_sha256_abc[OT149_SHA256_BYTES] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
    0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
};

static const uint8_t k_hkdf_ikm[22] = {
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
};

static const uint8_t k_hkdf_salt[13] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
};

static const uint8_t k_hkdf_info[10] = {
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4,
    0xf5, 0xf6, 0xf7, 0xf8, 0xf9,
};

static const uint8_t k_hkdf_okm[OT149_HKDF_BYTES] = {
    0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a,
    0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36, 0x2f, 0x2a,
    0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c,
    0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf,
    0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18,
    0x58, 0x65,
};

static const uint8_t k_aead_key[OT149_AEAD_KEY_BYTES] = {
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
};

static const uint8_t k_aead_nonce[OT149_AEAD_NONCE_BYTES] = {
    0x07, 0x00, 0x00, 0x00, 0x40, 0x41,
    0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
};

static const uint8_t k_aead_ad[12] = {
    0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1,
    0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
};

static const uint8_t k_aead_plaintext[114] = {
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
    0x74, 0x2e,
};

static const uint8_t k_aead_ciphertext[130] = {
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
    0x06, 0x91,
};

static bool import_key(psa_key_type_t type,
                       size_t bits,
                       psa_key_usage_t usage,
                       psa_algorithm_t algorithm,
                       const uint8_t *data,
                       size_t data_size,
                       mbedtls_svc_key_id_t *key)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, type);
    psa_set_key_bits(&attributes, bits);
    psa_set_key_usage_flags(&attributes, usage);
    psa_set_key_algorithm(&attributes, algorithm);
    psa_status_t status = psa_import_key(&attributes, data, data_size, key);
    psa_reset_key_attributes(&attributes);
    return status == PSA_SUCCESS;
}

static bool import_fixed_keys(void)
{
    if (!import_key(
            PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY),
            255U,
            PSA_KEY_USAGE_DERIVE,
            PSA_ALG_ECDH,
            k_x25519_private,
            sizeof(k_x25519_private),
            &g_x25519_key)) {
        return false;
    }
    if (!import_key(PSA_KEY_TYPE_DERIVE,
                    sizeof(k_hkdf_ikm) * 8U,
                    PSA_KEY_USAGE_DERIVE,
                    PSA_ALG_HKDF(PSA_ALG_SHA_256),
                    k_hkdf_ikm,
                    sizeof(k_hkdf_ikm),
                    &g_hkdf_key)) {
        return false;
    }
    return import_key(PSA_KEY_TYPE_CHACHA20,
                      sizeof(k_aead_key) * 8U,
                      PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT,
                      PSA_ALG_CHACHA20_POLY1305,
                      k_aead_key,
                      sizeof(k_aead_key),
                      &g_aead_key);
}

static int derive_hkdf(uint8_t output[OT149_HKDF_BYTES])
{
    psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_status_t status = psa_key_derivation_setup(
        &operation, PSA_ALG_HKDF(PSA_ALG_SHA_256));
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_bytes(
            &operation,
            PSA_KEY_DERIVATION_INPUT_SALT,
            k_hkdf_salt,
            sizeof(k_hkdf_salt));
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_key(
            &operation, PSA_KEY_DERIVATION_INPUT_SECRET, g_hkdf_key);
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_bytes(
            &operation,
            PSA_KEY_DERIVATION_INPUT_INFO,
            k_hkdf_info,
            sizeof(k_hkdf_info));
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_output_bytes(
            &operation, output, OT149_HKDF_BYTES);
    }
    psa_status_t abort_status = psa_key_derivation_abort(&operation);
    return status == PSA_SUCCESS && abort_status == PSA_SUCCESS ? 0 : -1;
}

bool ot149_candidate_initialize(void)
{
    for (size_t i = 0; i < sizeof(g_message); ++i) {
        g_message[i] = (uint8_t) (i ^ 0x5aU);
    }
    for (size_t i = 0; i < sizeof(g_ad); ++i) {
        g_ad[i] = (uint8_t) (0xa0U + i);
    }
    return psa_crypto_init() == PSA_SUCCESS;
}

__attribute__((noinline)) int ot149_candidate_x25519(void)
{
    uint8_t shared[OT149_X25519_BYTES] = { 0 };
    size_t shared_length = 0U;
    psa_status_t status = psa_raw_key_agreement(
        PSA_ALG_ECDH,
        g_x25519_key,
        k_x25519_peer_public,
        sizeof(k_x25519_peer_public),
        shared,
        sizeof(shared),
        &shared_length);
    int result = status == PSA_SUCCESS && shared_length == sizeof(shared) ? 0 : -1;
    if (result == 0) {
        g_sink ^= shared[0];
    }
    mbedtls_platform_zeroize(shared, sizeof(shared));
    return result;
}

__attribute__((noinline)) int ot149_candidate_sha256(void)
{
    size_t hash_length = 0U;
    psa_status_t status = psa_hash_compute(
        PSA_ALG_SHA_256,
        g_message,
        sizeof(g_message),
        g_hash,
        sizeof(g_hash),
        &hash_length);
    if (status == PSA_SUCCESS && hash_length == sizeof(g_hash)) {
        g_sink ^= g_hash[0];
        return 0;
    }
    return -1;
}

__attribute__((noinline)) int ot149_candidate_hkdf_sha256(void)
{
    int result = derive_hkdf(g_hkdf_output);
    if (result == 0) {
        g_sink ^= g_hkdf_output[0];
        return 0;
    }
    return -1;
}

__attribute__((noinline)) int ot149_candidate_chacha20poly1305_encrypt(void)
{
    psa_status_t status = psa_aead_encrypt(
        g_aead_key,
        PSA_ALG_CHACHA20_POLY1305,
        k_aead_nonce,
        sizeof(k_aead_nonce),
        g_ad,
        sizeof(g_ad),
        g_message,
        sizeof(g_message),
        g_ciphertext,
        sizeof(g_ciphertext),
        &g_ciphertext_length);
    if (status == PSA_SUCCESS && g_ciphertext_length == sizeof(g_ciphertext)) {
        g_sink ^= g_ciphertext[0];
        return 0;
    }
    return -1;
}

__attribute__((noinline)) int ot149_candidate_chacha20poly1305_decrypt(void)
{
    size_t plaintext_length = 0U;
    psa_status_t status = psa_aead_decrypt(
        g_aead_key,
        PSA_ALG_CHACHA20_POLY1305,
        k_aead_nonce,
        sizeof(k_aead_nonce),
        g_ad,
        sizeof(g_ad),
        g_ciphertext,
        g_ciphertext_length,
        g_plaintext,
        sizeof(g_plaintext),
        &plaintext_length);
    if (status == PSA_SUCCESS && plaintext_length == sizeof(g_message)) {
        g_sink ^= g_plaintext[0];
        return 0;
    }
    return -1;
}

bool ot149_candidate_vectors_and_negative_cases(void)
{
    static const uint8_t abc[] = { 'a', 'b', 'c' };
    uint8_t shared[OT149_X25519_BYTES] = { 0 };
    uint8_t hash[OT149_SHA256_BYTES] = { 0 };
    uint8_t hkdf_output[OT149_HKDF_BYTES] = { 0 };
    uint8_t low_order[OT149_X25519_BYTES] = { 0 };
    uint8_t low_order_output[OT149_X25519_BYTES] = { 0 };
    uint8_t aead_output[sizeof(k_aead_ciphertext)] = { 0 };
    uint8_t plaintext[sizeof(k_aead_plaintext)] = { 0 };
    uint8_t tampered[sizeof(k_aead_ciphertext)] = { 0 };
    size_t output_length = 0U;
    bool passed = false;

    if (!import_fixed_keys()) {
        goto cleanup;
    }
    if (psa_raw_key_agreement(PSA_ALG_ECDH,
                              g_x25519_key,
                              k_x25519_peer_public,
                              sizeof(k_x25519_peer_public),
                              shared,
                              sizeof(shared),
                              &output_length) != PSA_SUCCESS ||
        output_length != sizeof(shared) ||
        memcmp(shared, k_x25519_shared, sizeof(shared)) != 0) {
        goto cleanup;
    }
    output_length = sizeof(low_order_output);
    psa_status_t low_order_status = psa_raw_key_agreement(
        PSA_ALG_ECDH,
        g_x25519_key,
        low_order,
        sizeof(low_order),
        low_order_output,
        sizeof(low_order_output),
        &output_length);
    if (low_order_status != PSA_ERROR_INVALID_ARGUMENT) {
        goto cleanup;
    }

    output_length = 0U;
    if (psa_hash_compute(PSA_ALG_SHA_256,
                         abc,
                         sizeof(abc),
                         hash,
                         sizeof(hash),
                         &output_length) != PSA_SUCCESS ||
        output_length != sizeof(hash) ||
        memcmp(hash, k_sha256_abc, sizeof(hash)) != 0 ||
        derive_hkdf(hkdf_output) != 0 ||
        memcmp(hkdf_output, k_hkdf_okm, sizeof(hkdf_output)) != 0) {
        goto cleanup;
    }

    output_length = 0U;
    if (psa_aead_encrypt(g_aead_key,
                         PSA_ALG_CHACHA20_POLY1305,
                         k_aead_nonce,
                         sizeof(k_aead_nonce),
                         k_aead_ad,
                         sizeof(k_aead_ad),
                         k_aead_plaintext,
                         sizeof(k_aead_plaintext),
                         aead_output,
                         sizeof(aead_output),
                         &output_length) != PSA_SUCCESS ||
        output_length != sizeof(k_aead_ciphertext) ||
        memcmp(aead_output, k_aead_ciphertext, sizeof(aead_output)) != 0) {
        goto cleanup;
    }

    output_length = 0U;
    if (psa_aead_decrypt(g_aead_key,
                         PSA_ALG_CHACHA20_POLY1305,
                         k_aead_nonce,
                         sizeof(k_aead_nonce),
                         k_aead_ad,
                         sizeof(k_aead_ad),
                         aead_output,
                         sizeof(aead_output),
                         plaintext,
                         sizeof(plaintext),
                         &output_length) != PSA_SUCCESS ||
        output_length != sizeof(k_aead_plaintext) ||
        memcmp(plaintext, k_aead_plaintext, sizeof(plaintext)) != 0) {
        goto cleanup;
    }

    memcpy(tampered, aead_output, sizeof(tampered));
    tampered[sizeof(tampered) - 1U] ^= 0x01U;
    output_length = 0U;
    psa_status_t tampered_status = psa_aead_decrypt(g_aead_key,
                                                    PSA_ALG_CHACHA20_POLY1305,
                                                    k_aead_nonce,
                                                    sizeof(k_aead_nonce),
                                                    k_aead_ad,
                                                    sizeof(k_aead_ad),
                                                    tampered,
                                                    sizeof(tampered),
                                                    plaintext,
                                                    sizeof(plaintext),
                                                    &output_length);
    if (tampered_status != PSA_ERROR_INVALID_SIGNATURE || output_length != 0U ||
        ot149_candidate_chacha20poly1305_encrypt() != 0 ||
        ot149_candidate_chacha20poly1305_decrypt() != 0) {
        goto cleanup;
    }
    passed = true;

cleanup:
    mbedtls_platform_zeroize(shared, sizeof(shared));
    mbedtls_platform_zeroize(hash, sizeof(hash));
    mbedtls_platform_zeroize(hkdf_output, sizeof(hkdf_output));
    mbedtls_platform_zeroize(low_order, sizeof(low_order));
    mbedtls_platform_zeroize(low_order_output, sizeof(low_order_output));
    mbedtls_platform_zeroize(aead_output, sizeof(aead_output));
    mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
    mbedtls_platform_zeroize(tampered, sizeof(tampered));
    return passed;
}

bool ot149_candidate_cleanup(void)
{
    bool passed = true;
    if (!mbedtls_svc_key_id_is_null(g_x25519_key)) {
        passed = psa_destroy_key(g_x25519_key) == PSA_SUCCESS && passed;
        g_x25519_key = MBEDTLS_SVC_KEY_ID_INIT;
    }
    if (!mbedtls_svc_key_id_is_null(g_hkdf_key)) {
        passed = psa_destroy_key(g_hkdf_key) == PSA_SUCCESS && passed;
        g_hkdf_key = MBEDTLS_SVC_KEY_ID_INIT;
    }
    if (!mbedtls_svc_key_id_is_null(g_aead_key)) {
        passed = psa_destroy_key(g_aead_key) == PSA_SUCCESS && passed;
        g_aead_key = MBEDTLS_SVC_KEY_ID_INIT;
    }
    mbedtls_platform_zeroize(g_message, sizeof(g_message));
    mbedtls_platform_zeroize(g_ad, sizeof(g_ad));
    mbedtls_platform_zeroize(g_hash, sizeof(g_hash));
    mbedtls_platform_zeroize(g_hkdf_output, sizeof(g_hkdf_output));
    mbedtls_platform_zeroize(g_ciphertext, sizeof(g_ciphertext));
    mbedtls_platform_zeroize(g_plaintext, sizeof(g_plaintext));
    g_ciphertext_length = 0U;
    return passed;
}
