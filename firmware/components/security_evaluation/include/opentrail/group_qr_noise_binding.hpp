#pragma once

// D-170 (a): the QR group join's key agreement is the existing Noise XK suite
// (Noise_XK_25519_ChaChaPoly_SHA256, libsodium adapter), run directly between the
// joiner board (initiator) and the admin board (responder) and bound to the QR
// session. This produces qr::Authority::verified_handshake and the pair keys.
//
// - Statics: each board's Ed25519 identity converted to X25519 (libsodium). The
//   joiner learns the admin static from the signed invite, so XK needs no extra
//   frame; the admin checks the joiner's static against the signed Start.
// - Ephemerals: the X25519 keys already signed in the QR Start (joiner) and Reply
//   (admin). Noise must use exactly those, so a signed QR frame cannot be paired
//   with another handshake.
// - Prologue: a domain label and the QR scope digest.
// Messages: 48, 48 and 64 bytes. No payloads are carried.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <sodium/crypto_hash_sha256.h>
#include <sodium/crypto_scalarmult_curve25519.h>
#include <sodium/crypto_sign_ed25519.h>
#include <sodium/utils.h>

#include "noise_xk_libsodium.h"
#include "opentrail/group_qr_invite.hpp"

namespace opentrail::security_evaluation {

class GroupQrNoiseBinding {
public:
    using Key = std::array<std::uint8_t, OT_NOISE_XK_KEY_BYTES>;
    using Digest = identity::roster::Digest;
    using PublicKey = identity::roster::PublicKey;
    using Side = identity::roster::qr::Side;
    static constexpr std::size_t kMessageOneBytes = OT_NOISE_XK_MESSAGE_1_BYTES;
    static constexpr std::size_t kMessageTwoBytes = OT_NOISE_XK_MESSAGE_2_BYTES;
    static constexpr std::size_t kMessageThreeBytes = OT_NOISE_XK_MESSAGE_3_BYTES;

    // X25519 static derived from an Ed25519 identity public key.
    static bool x25519_public(const PublicKey& ed25519, Key& out) {
        Key candidate{};
        if (crypto_sign_ed25519_pk_to_curve25519(candidate.data(), ed25519.data()) != 0 || !nonzero(candidate))
            return false;
        out = candidate;
        return true;
    }

    // X25519 static keypair derived from the 64-byte libsodium Ed25519 secret key.
    static bool x25519_static(const std::array<std::uint8_t, 64>& ed25519_secret, ot_noise_xk_keypair& out) {
        ot_noise_xk_keypair candidate{};
        const bool ok = crypto_sign_ed25519_sk_to_curve25519(candidate.secret, ed25519_secret.data()) == 0 &&
                        crypto_scalarmult_curve25519_base(candidate.public_key, candidate.secret) == 0;
        if (ok) std::memcpy(&out, &candidate, sizeof out);
        sodium_memzero(&candidate, sizeof candidate);
        return ok;
    }

    GroupQrNoiseBinding() = default;
    GroupQrNoiseBinding(const GroupQrNoiseBinding&) = delete;
    GroupQrNoiseBinding& operator=(const GroupQrNoiseBinding&) = delete;
    ~GroupQrNoiseBinding() { abort(); }

    // local_static: this board's X25519 static (from x25519_static). peer: the other
    // board's Ed25519 identity (the invite's admin, or the Start's joiner).
    // local_ephemeral: the X25519 keypair whose public half this board signed in its
    // QR Start or Reply. One use per object.
    bool begin(Side side, const ot_noise_xk_keypair& local_static, const PublicKey& peer,
               const ot_noise_xk_keypair& local_ephemeral, const Digest& scope) {
        if (used_ || !nonzero(scope) || !x25519_public(peer, expected_remote_)) return fail();
        used_ = true;
        side_ = side;
        std::array<std::uint8_t, sizeof(kPrologueLabel) - 1 + 32> prologue{};
        std::memcpy(prologue.data(), kPrologueLabel, sizeof(kPrologueLabel) - 1);
        std::memcpy(prologue.data() + sizeof(kPrologueLabel) - 1, scope.data(), scope.size());
        const int rc = side == Side::joiner ?
            ot_noise_xk_init_initiator(&state_, &local_static, &local_ephemeral, expected_remote_.data(),
                                       prologue.data(), prologue.size()) :
            ot_noise_xk_init_responder(&state_, &local_static, &local_ephemeral, prologue.data(), prologue.size());
        return rc == 0 || fail();
    }

    // The next message this side sends: joiner 1 and 3, admin 2.
    bool write(std::uint8_t* out, std::size_t capacity, std::size_t& size) {
        size = 0;
        if (failed_ || !used_ || ot_noise_xk_write_message(&state_, out, capacity, &size) != 0) return fail();
        return finish_if_complete();
    }

    bool read(const std::uint8_t* in, std::size_t size) {
        if (failed_ || !used_ || ot_noise_xk_read_message(&state_, in, size) != 0) return fail();
        return finish_if_complete();
    }

    [[nodiscard]] bool complete() const { return complete_; }

    // The verified_handshake producer. True only once Noise has finished with the
    // expected remote static and with exactly the two ephemerals the QR Start and
    // Reply carry (initiator = joiner, responder = admin).
    bool verified(const Digest& admitted, const identity::roster::Ephemeral& initiator,
                  const identity::roster::Ephemeral& responder, Digest& out) const {
        if (!complete_ || failed_ || !nonzero(admitted)) return false;
        const auto& local = side_ == Side::joiner ? initiator : responder;
        const auto& remote = side_ == Side::joiner ? responder : initiator;
        if (sodium_memcmp(local.data(), local_ephemeral_.data(), 32) != 0 ||
            sodium_memcmp(remote.data(), remote_ephemeral_.data(), 32) != 0) return false;
        crypto_hash_sha256_state h{};
        Digest candidate{};
        const bool ok = crypto_hash_sha256_init(&h) == 0 &&
            crypto_hash_sha256_update(&h, reinterpret_cast<const unsigned char*>(kBindingLabel),
                                      sizeof(kBindingLabel) - 1) == 0 &&
            crypto_hash_sha256_update(&h, admitted.data(), admitted.size()) == 0 &&
            crypto_hash_sha256_update(&h, handshake_hash_.data(), handshake_hash_.size()) == 0 &&
            crypto_hash_sha256_final(&h, candidate.data()) == 0;
        sodium_memzero(&h, sizeof h);
        if (!ok || !nonzero(candidate)) return false;
        out = candidate;
        return true;
    }

    // Pair transport keys, once, after completion. The caller stores them only
    // after the QR receipts commit.
    bool split(Key& transmit, Key& receive) {
        if (!complete_ || failed_ || split_) return false;
        split_ = true;
        Key tx{}, rx{};
        const bool ok = ot_noise_xk_split(&state_, tx.data(), rx.data()) == 0;
        if (ok) { transmit = tx; receive = rx; }
        sodium_memzero(tx.data(), tx.size());
        sodium_memzero(rx.data(), rx.size());
        return ok || fail();
    }

    void abort() {
        ot_noise_xk_abort(&state_);
        sodium_memzero(handshake_hash_.data(), handshake_hash_.size());
    }

private:
    static constexpr char kPrologueLabel[] = "OpenTrail QR Group Join Noise XK v1";
    static constexpr char kBindingLabel[] = "OpenTrail QR Noise XK Binding v1";

    static bool nonzero(const std::array<std::uint8_t, 32>& value) {
        return sodium_is_zero(value.data(), value.size()) == 0;
    }

    bool finish_if_complete() {
        if (state_.stage != OT_NOISE_XK_STAGE_SPLIT) return true;
        // XK authenticates the initiator's static in message 3. Both sides check
        // it is exactly the expected board, then keep the transcript values.
        if (sodium_memcmp(state_.remote_static_public, expected_remote_.data(), 32) != 0) return fail();
        std::memcpy(handshake_hash_.data(), state_.handshake_hash, handshake_hash_.size());
        std::memcpy(local_ephemeral_.data(), state_.local_ephemeral_public, 32);
        std::memcpy(remote_ephemeral_.data(), state_.remote_ephemeral_public, 32);
        complete_ = true;
        return true;
    }

    bool fail() {
        failed_ = true;
        complete_ = false;
        abort();
        return false;
    }

    ot_noise_xk_state state_{};
    Key expected_remote_{};
    Digest handshake_hash_{};
    Key local_ephemeral_{}, remote_ephemeral_{};
    Side side_{Side::joiner};
    bool used_{false}, complete_{false}, failed_{false}, split_{false};
};

}  // namespace opentrail::security_evaluation
