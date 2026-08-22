#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

extern "C" {
#include "monocypher_benchmark_api.h"
#include <monocypher-ed25519.h>
#include <monocypher.h>
}

namespace {

int sign_calls, verify_calls, x25519_calls, init_ietf_calls;
int encrypt_calls, decrypt_calls, wipe_calls;

uint8_t fold(const uint8_t* data, size_t size, uint8_t value) {
    for (size_t index = 0; index < size; ++index) value = static_cast<uint8_t>((value * 33U) ^ data[index]);
    return value;
}

void reset_calls() {
    sign_calls = verify_calls = x25519_calls = init_ietf_calls = 0;
    encrypt_calls = decrypt_calls = wipe_calls = 0;
}

void test_exact_identity_and_direct_operations() {
    reset_calls();
    static_assert(OT_MONOCYPHER_API_VERSION == 0U);
    assert(std::strcmp(OT_MONOCYPHER_API_SCHEMA, "OTMAPI0") == 0);
    std::array<uint8_t, 64> secret{}, signature{};
    std::array<uint8_t, 32> public_key{}, local_secret{}, remote_public{}, shared{};
    const uint8_t message[] = {1, 2, 3, 4};
    std::fill(secret.begin(), secret.end(), 0x31);
    std::fill(public_key.begin(), public_key.end(), 0x31);
    std::fill(local_secret.begin(), local_secret.end(), 0x42);
    std::fill(remote_public.begin(), remote_public.end(), 0x19);
    assert(ot_monocypher_ed25519_sign(signature.data(), secret.data(), message, sizeof message) == 0);
    assert(ot_monocypher_ed25519_verify(signature.data(), public_key.data(), message, sizeof message) == 0);
    assert(ot_monocypher_x25519(shared.data(), local_secret.data(), remote_public.data()) == 0);
    assert(sign_calls == 1 && verify_calls == 1 && x25519_calls == 1);
    assert(std::any_of(shared.begin(), shared.end(), [](uint8_t value) { return value != 0; }));
}

void test_ietf_chachapoly_combined_round_trip() {
    reset_calls();
    std::array<uint8_t, 32> key{};
    std::array<uint8_t, 12> nonce{};
    std::array<uint8_t, 64> cipher{}, plain{};
    const uint8_t ad[] = {9, 8, 7};
    const uint8_t input[] = {1, 3, 3, 7, 9};
    size_t cipher_size = 0, plain_size = 0;
    std::fill(key.begin(), key.end(), 0xa5);
    std::fill(nonce.begin(), nonce.end(), 0x16);
    assert(ot_monocypher_chacha20poly1305_ietf_encrypt(
        cipher.data(), cipher.size(), &cipher_size, key.data(), nonce.data(),
        ad, sizeof ad, input, sizeof input) == 0);
    assert(cipher_size == sizeof input + OT_MONOCYPHER_AEAD_TAG_BYTES);
    assert(ot_monocypher_chacha20poly1305_ietf_decrypt(
        plain.data(), plain.size(), &plain_size, key.data(), nonce.data(),
        ad, sizeof ad, cipher.data(), cipher_size) == 0);
    assert(plain_size == sizeof input);
    assert(std::memcmp(plain.data(), input, sizeof input) == 0);
    assert(init_ietf_calls == 2 && encrypt_calls == 1 && decrypt_calls == 1);
    assert(wipe_calls == 2);
}

void test_corruption_rejected_and_plaintext_wiped() {
    std::array<uint8_t, 32> key{};
    std::array<uint8_t, 12> nonce{};
    std::array<uint8_t, 64> cipher{}, plain{};
    const uint8_t input[] = {1, 2, 3};
    size_t cipher_size = 0, plain_size = 99;
    assert(ot_monocypher_chacha20poly1305_ietf_encrypt(
        cipher.data(), cipher.size(), &cipher_size, key.data(), nonce.data(),
        nullptr, 0, input, sizeof input) == 0);
    cipher[cipher_size - 1U] ^= 0x80;
    std::fill(plain.begin(), plain.end(), 0xcc);
    assert(ot_monocypher_chacha20poly1305_ietf_decrypt(
        plain.data(), plain.size(), &plain_size, key.data(), nonce.data(),
        nullptr, 0, cipher.data(), cipher_size) != 0);
    assert(plain_size == 0);
    assert(std::all_of(plain.begin(), plain.begin() + sizeof input,
                       [](uint8_t value) { return value == 0; }));
}

void test_bounded_arguments_fail_without_candidate_calls() {
    reset_calls();
    std::array<uint8_t, 32> key{}, shared{};
    std::array<uint8_t, 12> nonce{};
    std::array<uint8_t, 16> output{};
    size_t size = 7;
    assert(ot_monocypher_ed25519_sign(nullptr, nullptr, nullptr, 0) != 0);
    assert(ot_monocypher_ed25519_verify(nullptr, nullptr, nullptr, 0) != 0);
    assert(ot_monocypher_x25519(shared.data(), nullptr, key.data()) != 0);
    assert(ot_monocypher_chacha20poly1305_ietf_encrypt(
        output.data(), output.size() - 1U, &size, key.data(), nonce.data(),
        nullptr, 0, nullptr, 0) != 0);
    assert(size == 0);
    assert(ot_monocypher_chacha20poly1305_ietf_decrypt(
        output.data(), output.size(), &size, key.data(), nonce.data(),
        nullptr, 0, output.data(), OT_MONOCYPHER_AEAD_TAG_BYTES - 1U) != 0);
    assert(size == 0);
    assert(sign_calls == 0 && verify_calls == 0 && x25519_calls == 0);
    assert(init_ietf_calls == 0 && encrypt_calls == 0 && decrypt_calls == 0);
}

}  // namespace

extern "C" void crypto_ed25519_sign(uint8_t signature[64], const uint8_t secret[64],
        const uint8_t* message, size_t message_size) {
    ++sign_calls;
    uint8_t value = fold(message, message_size, fold(secret, 64, 0x19));
    for (size_t index = 0; index < 64; ++index) signature[index] = static_cast<uint8_t>(value ^ index);
}

extern "C" int crypto_ed25519_check(const uint8_t signature[64], const uint8_t public_key[32],
        const uint8_t* message, size_t message_size) {
    ++verify_calls;
    std::array<uint8_t, 64> expanded{};
    std::copy(public_key, public_key + 32, expanded.begin());
    std::copy(public_key, public_key + 32, expanded.begin() + 32);
    std::array<uint8_t, 64> expected{};
    crypto_ed25519_sign(expected.data(), expanded.data(), message, message_size);
    --sign_calls;
    return std::memcmp(signature, expected.data(), expected.size()) == 0 ? 0 : -1;
}

extern "C" void crypto_x25519(uint8_t output[32], const uint8_t local[32], const uint8_t remote[32]) {
    ++x25519_calls;
    for (size_t index = 0; index < 32; ++index) output[index] = static_cast<uint8_t>(local[index] ^ remote[index] ^ 0x5a);
}

extern "C" void crypto_aead_init_ietf(crypto_aead_ctx* context, const uint8_t key[32], const uint8_t nonce[12]) {
    ++init_ietf_calls;
    std::memcpy(context->key, key, 32);
    std::memcpy(context->nonce, nonce, 12);
}

extern "C" void crypto_aead_write(crypto_aead_ctx* context, uint8_t* cipher, uint8_t mac[16],
        const uint8_t* ad, size_t ad_size, const uint8_t* plain, size_t size) {
    ++encrypt_calls;
    for (size_t index = 0; index < size; ++index) cipher[index] = static_cast<uint8_t>(plain[index] ^ context->key[index % 32]);
    uint8_t tag = fold(cipher, size, fold(ad, ad_size, fold(context->nonce, 12, fold(context->key, 32, 0x72))));
    for (size_t index = 0; index < 16; ++index) mac[index] = static_cast<uint8_t>(tag ^ index);
}

extern "C" int crypto_aead_read(crypto_aead_ctx* context, uint8_t* plain, const uint8_t mac[16],
        const uint8_t* ad, size_t ad_size, const uint8_t* cipher, size_t size) {
    ++decrypt_calls;
    uint8_t tag = fold(cipher, size, fold(ad, ad_size, fold(context->nonce, 12, fold(context->key, 32, 0x72))));
    for (size_t index = 0; index < 16; ++index) if (mac[index] != static_cast<uint8_t>(tag ^ index)) return -1;
    for (size_t index = 0; index < size; ++index) plain[index] = static_cast<uint8_t>(cipher[index] ^ context->key[index % 32]);
    return 0;
}

extern "C" void crypto_wipe(void* data, size_t size) {
    ++wipe_calls;
    volatile uint8_t* bytes = static_cast<volatile uint8_t*>(data);
    while (size-- != 0U) *bytes++ = 0U;
}

int main() {
    test_exact_identity_and_direct_operations();
    test_ietf_chachapoly_combined_round_trip();
    test_corruption_rejected_and_plaintext_wiped();
    test_bounded_arguments_fail_without_candidate_calls();
    std::cout << "PASS: 4 benchmark-only Monocypher API adapter groups\n";
    return 0;
}
