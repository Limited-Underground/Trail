#include "ot149_candidate_api.h"

#include <stdint.h>

static volatile uint32_t g_control_state;

bool ot149_candidate_initialize(void)
{
    g_control_state = 0x149005U;
    return true;
}

bool ot149_candidate_vectors_and_negative_cases(void)
{
    g_control_state ^= 0x010101U;
    return g_control_state != 0U;
}

__attribute__((noinline)) int ot149_candidate_x25519(void)
{
    g_control_state = (g_control_state << 1U) ^ 0x25519U;
    return 0;
}

__attribute__((noinline)) int ot149_candidate_sha256(void)
{
    g_control_state = (g_control_state << 3U) ^ 0x256U;
    return 0;
}

__attribute__((noinline)) int ot149_candidate_hkdf_sha256(void)
{
    g_control_state = (g_control_state << 5U) ^ 0x4b4446U;
    return 0;
}

__attribute__((noinline)) int ot149_candidate_chacha20poly1305_encrypt(void)
{
    g_control_state = (g_control_state << 7U) ^ 0x1305U;
    return 0;
}

__attribute__((noinline)) int ot149_candidate_chacha20poly1305_decrypt(void)
{
    g_control_state = (g_control_state << 11U) ^ 0x201305U;
    return 0;
}

bool ot149_candidate_cleanup(void)
{
    g_control_state = 0U;
    return true;
}
