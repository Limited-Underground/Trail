#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

extern "C" {
#include "noise_xk_libsodium.h"
#include <sodium.h>
}

namespace {

struct Calls {
    int hashes{};
    int extracts{};
    int expands{};
    int dh{};
    int encrypts{};
    int decrypts{};
    int wipes{};
} calls;

uint8_t fold(const uint8_t* data, std::size_t size, uint8_t seed) {
    for (std::size_t i = 0; i < size; ++i) seed = static_cast<uint8_t>((seed * 33U) ^ data[i]);
    return seed;
}

ot_noise_xk_keypair keypair(uint8_t value) {
    ot_noise_xk_keypair result{};
    std::fill(std::begin(result.secret), std::end(result.secret), value);
    std::fill(std::begin(result.public_key), std::end(result.public_key), static_cast<uint8_t>(value ^ 0x5aU));
    return result;
}

void reset_calls() { calls = {}; }

bool all_zero(const uint8_t* value, std::size_t size) {
    return std::all_of(value, value + size, [](uint8_t byte) { return byte == 0U; });
}

void complete_handshake(ot_noise_xk_state& initiator, ot_noise_xk_state& responder,
                        std::array<uint8_t, OT_NOISE_XK_MESSAGE_3_BYTES>& message,
                        std::array<uint8_t, 32>& initiator_tx,
                        std::array<uint8_t, 32>& initiator_rx,
                        std::array<uint8_t, 32>& responder_tx,
                        std::array<uint8_t, 32>& responder_rx) {
    size_t size = 0;
    assert(ot_noise_xk_write_message(&initiator, message.data(), message.size(), &size) == 0);
    assert(size == OT_NOISE_XK_MESSAGE_1_BYTES);
    assert(ot_noise_xk_read_message(&responder, message.data(), size) == 0);
    assert(ot_noise_xk_write_message(&responder, message.data(), message.size(), &size) == 0);
    assert(size == OT_NOISE_XK_MESSAGE_2_BYTES);
    assert(ot_noise_xk_read_message(&initiator, message.data(), size) == 0);
    assert(ot_noise_xk_write_message(&initiator, message.data(), message.size(), &size) == 0);
    assert(size == OT_NOISE_XK_MESSAGE_3_BYTES);
    assert(ot_noise_xk_read_message(&responder, message.data(), size) == 0);
    assert(std::memcmp(initiator.handshake_hash, responder.handshake_hash, 32) == 0);
    assert(ot_noise_xk_split(&initiator, initiator_tx.data(), initiator_rx.data()) == 0);
    assert(ot_noise_xk_split(&responder, responder_tx.data(), responder_rx.data()) == 0);
}

void test_exact_xk_transcript_and_split_direction() {
    reset_calls();
    const auto initiator_static = keypair(0x11);
    const auto initiator_ephemeral = keypair(0x22);
    const auto responder_static = keypair(0x33);
    const auto responder_ephemeral = keypair(0x44);
    const uint8_t prologue[] = {'O','p','e','n','T','r','a','i','l'};
    ot_noise_xk_state initiator{}, responder{};
    assert(ot_noise_xk_init_initiator(&initiator, &initiator_static,
        &initiator_ephemeral, responder_static.public_key, prologue, sizeof prologue) == 0);
    assert(ot_noise_xk_init_responder(&responder, &responder_static,
        &responder_ephemeral, prologue, sizeof prologue) == 0);
    std::array<uint8_t, OT_NOISE_XK_MESSAGE_3_BYTES> message{};
    std::array<uint8_t, 32> itx{}, irx{}, rtx{}, rrx{};
    complete_handshake(initiator, responder, message, itx, irx, rtx, rrx);
    assert(itx == rrx && irx == rtx && itx != irx);
    assert(initiator.stage == OT_NOISE_XK_STAGE_FINISHED);
    assert(responder.stage == OT_NOISE_XK_STAGE_FINISHED);
    assert(all_zero(initiator.local_static_secret, 32));
    assert(all_zero(responder.local_ephemeral_secret, 32));
    assert(calls.dh == 6);
    assert(calls.encrypts == 4 && calls.decrypts == 4);
    assert(calls.extracts == 8 && calls.expands == 8);
    assert(calls.hashes > 0 && calls.wipes > 0);
    static_assert(OT_NOISE_XK_MESSAGE_1_BYTES + OT_NOISE_XK_MESSAGE_2_BYTES +
                  OT_NOISE_XK_MESSAGE_3_BYTES == OT_NOISE_XK_TOTAL_HANDSHAKE_BYTES);
}

void test_order_capacity_and_truncation_fail_closed() {
    const auto initiator_static = keypair(0x11);
    const auto initiator_ephemeral = keypair(0x22);
    const auto responder_static = keypair(0x33);
    const auto responder_ephemeral = keypair(0x44);
    ot_noise_xk_state initiator{}, responder{};
    std::array<uint8_t, OT_NOISE_XK_MESSAGE_3_BYTES> message{};
    size_t size = 99;
    assert(ot_noise_xk_init_initiator(&initiator, &initiator_static,
        &initiator_ephemeral, responder_static.public_key, nullptr, 0) == 0);
    assert(ot_noise_xk_write_message(&initiator, message.data(),
        OT_NOISE_XK_MESSAGE_1_BYTES - 1, &size) != 0);
    assert(size == 0 && initiator.stage == OT_NOISE_XK_STAGE_FAILED);
    assert(all_zero(initiator.local_static_secret, 32));

    assert(ot_noise_xk_init_responder(&responder, &responder_static,
        &responder_ephemeral, nullptr, 0) == 0);
    assert(ot_noise_xk_write_message(&responder, message.data(), message.size(), &size) != 0);
    assert(responder.stage == OT_NOISE_XK_STAGE_FAILED);

    assert(ot_noise_xk_init_responder(&responder, &responder_static,
        &responder_ephemeral, nullptr, 0) == 0);
    assert(ot_noise_xk_read_message(&responder, message.data(),
        OT_NOISE_XK_MESSAGE_1_BYTES - 1) != 0);
    assert(responder.stage == OT_NOISE_XK_STAGE_FAILED);
}

void test_ciphertext_corruption_and_wrong_pinned_static_fail() {
    const auto initiator_static = keypair(0x11);
    const auto initiator_ephemeral = keypair(0x22);
    const auto responder_static = keypair(0x33);
    const auto responder_ephemeral = keypair(0x44);
    std::array<uint8_t, 32> wrong_static{};
    std::fill(wrong_static.begin(), wrong_static.end(), 0x99);
    ot_noise_xk_state initiator{}, responder{};
    std::array<uint8_t, OT_NOISE_XK_MESSAGE_3_BYTES> message{};
    size_t size = 0;
    assert(ot_noise_xk_init_initiator(&initiator, &initiator_static,
        &initiator_ephemeral, responder_static.public_key, nullptr, 0) == 0);
    assert(ot_noise_xk_init_responder(&responder, &responder_static,
        &responder_ephemeral, nullptr, 0) == 0);
    assert(ot_noise_xk_write_message(&initiator, message.data(), message.size(), &size) == 0);
    message[size - 1U] ^= 0x80;
    assert(ot_noise_xk_read_message(&responder, message.data(), size) != 0);
    assert(responder.stage == OT_NOISE_XK_STAGE_FAILED);

    assert(ot_noise_xk_init_initiator(&initiator, &initiator_static,
        &initiator_ephemeral, wrong_static.data(), nullptr, 0) == 0);
    assert(ot_noise_xk_init_responder(&responder, &responder_static,
        &responder_ephemeral, nullptr, 0) == 0);
    assert(ot_noise_xk_write_message(&initiator, message.data(), message.size(), &size) == 0);
    assert(ot_noise_xk_read_message(&responder, message.data(), size) != 0);
}

void test_abort_and_split_before_completion_wipe() {
    const auto local_static = keypair(0x11);
    const auto local_ephemeral = keypair(0x22);
    const auto remote_static = keypair(0x33);
    ot_noise_xk_state state{};
    std::array<uint8_t, 32> tx{}, rx{};
    assert(ot_noise_xk_init_initiator(&state, &local_static, &local_ephemeral,
        remote_static.public_key, nullptr, 0) == 0);
    assert(ot_noise_xk_split(&state, tx.data(), rx.data()) != 0);
    assert(state.stage == OT_NOISE_XK_STAGE_FAILED);
    assert(all_zero(state.local_static_secret, 32));
    ot_noise_xk_abort(&state);
    assert(state.stage == OT_NOISE_XK_STAGE_FAILED);
    assert(all_zero(state.chaining_key, 32));
    assert(all_zero(state.handshake_hash, 32));
    assert(all_zero(state.local_static_secret, 32));
    assert(all_zero(state.local_ephemeral_secret, 32));

    assert(ot_noise_xk_init_responder(&state, nullptr, &local_ephemeral,
        nullptr, 0) != 0);
    assert(state.stage == OT_NOISE_XK_STAGE_FAILED);

    assert(ot_noise_xk_init_initiator(&state, &local_static, &local_ephemeral,
        remote_static.public_key, nullptr, 0) == 0);
    assert(ot_noise_xk_read_message(&state, nullptr, 0) != 0);
    assert(state.stage == OT_NOISE_XK_STAGE_FAILED);
    assert(all_zero(state.local_static_secret, 32));
}

}  // namespace

extern "C" int crypto_hash_sha256_init(crypto_hash_sha256_state* state) {
    std::memset(state, 0, sizeof *state); return 0;
}
extern "C" int crypto_hash_sha256_update(crypto_hash_sha256_state* state,
        const uint8_t* data, unsigned long long size) {
    for (unsigned long long i = 0; i < size; ++i) {
        const auto index = static_cast<std::size_t>(state->count++ % 32U);
        state->value[index] = static_cast<uint8_t>((state->value[index] * 33U) ^ data[i] ^ index);
    }
    return 0;
}
extern "C" int crypto_hash_sha256_final(crypto_hash_sha256_state* state, uint8_t* out) {
    ++calls.hashes; std::memcpy(out, state->value, 32); return 0;
}
extern "C" int crypto_hash_sha256(uint8_t* out, const uint8_t* data,
        unsigned long long size) {
    crypto_hash_sha256_state state{};
    crypto_hash_sha256_init(&state); crypto_hash_sha256_update(&state, data, size);
    return crypto_hash_sha256_final(&state, out);
}
extern "C" int crypto_kdf_hkdf_sha256_extract(uint8_t* out, const uint8_t* salt,
        size_t salt_size, const uint8_t* ikm, size_t ikm_size) {
    ++calls.extracts;
    const uint8_t value = fold(ikm, ikm_size, fold(salt, salt_size, 0x36));
    for (size_t i = 0; i < 32; ++i) out[i] = static_cast<uint8_t>(value ^ i);
    return 0;
}
extern "C" int crypto_kdf_hkdf_sha256_expand(uint8_t* out, size_t out_size,
        const char* context, size_t context_size, const uint8_t* key) {
    ++calls.expands;
    uint8_t value = fold(reinterpret_cast<const uint8_t*>(context), context_size, fold(key, 32, 0x5c));
    for (size_t i = 0; i < out_size; ++i) out[i] = static_cast<uint8_t>(value + i + (i / 32));
    return 0;
}
extern "C" int crypto_scalarmult_curve25519(uint8_t* out, const uint8_t* secret,
        const uint8_t* public_key) {
    ++calls.dh;
    for (size_t i = 0; i < 32; ++i) out[i] = static_cast<uint8_t>(secret[i] ^ public_key[i] ^ 0xa5);
    return 0;
}
extern "C" int crypto_aead_chacha20poly1305_ietf_encrypt(uint8_t* out,
        unsigned long long* out_size, const uint8_t* plain, unsigned long long plain_size,
        const uint8_t* ad, unsigned long long ad_size, const uint8_t*, const uint8_t* nonce,
        const uint8_t* key) {
    ++calls.encrypts;
    for (unsigned long long i = 0; i < plain_size; ++i) out[i] = static_cast<uint8_t>(plain[i] ^ key[i % 32]);
    uint8_t tag = fold(ad, static_cast<size_t>(ad_size), fold(nonce, 12, fold(key, 32, 0x91)));
    tag = fold(out, static_cast<size_t>(plain_size), tag);
    for (size_t i = 0; i < 16; ++i) out[plain_size + i] = static_cast<uint8_t>(tag ^ i);
    *out_size = plain_size + 16; return 0;
}
extern "C" int crypto_aead_chacha20poly1305_ietf_decrypt(uint8_t* out,
        unsigned long long* out_size, uint8_t*, const uint8_t* cipher,
        unsigned long long cipher_size, const uint8_t* ad, unsigned long long ad_size,
        const uint8_t* nonce, const uint8_t* key) {
    ++calls.decrypts;
    if (cipher_size < 16) return -1;
    const auto plain_size = cipher_size - 16;
    uint8_t tag = fold(ad, static_cast<size_t>(ad_size), fold(nonce, 12, fold(key, 32, 0x91)));
    tag = fold(cipher, static_cast<size_t>(plain_size), tag);
    for (size_t i = 0; i < 16; ++i) if (cipher[plain_size + i] != static_cast<uint8_t>(tag ^ i)) return -1;
    for (unsigned long long i = 0; i < plain_size; ++i) out[i] = static_cast<uint8_t>(cipher[i] ^ key[i % 32]);
    *out_size = plain_size; return 0;
}
extern "C" void sodium_memzero(void* data, size_t size) {
    ++calls.wipes; volatile uint8_t* bytes = static_cast<volatile uint8_t*>(data);
    while (size-- != 0U) *bytes++ = 0U;
}

int main() {
    test_exact_xk_transcript_and_split_direction();
    test_order_capacity_and_truncation_fail_closed();
    test_ciphertext_corruption_and_wrong_pinned_static_fail();
    test_abort_and_split_before_completion_wipe();
    std::cout << "PASS: 4 benchmark-only libsodium Noise XK composition groups\n";
    return 0;
}
