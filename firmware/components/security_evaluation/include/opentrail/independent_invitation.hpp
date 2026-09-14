#pragma once
// OT228 evaluation provisioning contract. Both peers authenticate the same
// signed bytes, while each validates only its own boot and monotonic window.
// Trusted pins/role/boot/time come from the local composition, not the packet.
// This gate provides no durable consumption, clock synchronization or trust-root
// provisioning; the consuming role authority must durably consume before use.
#include "opentrail/evaluation_invitation_authority.hpp"

namespace opentrail::security_evaluation {
inline constexpr std::size_t kIndependentInvitationPayloadBytes = 188;
inline constexpr std::uint64_t kIndependentInvitationMaximumWindowMs = 60000;

struct IndependentInvitation {
    std::array<std::uint8_t, kIndependentInvitationPayloadBytes> payload{};
    std::array<std::uint8_t, 64> signature{};
};
struct IndependentInvitationFields {
    std::uint64_t group{0};
    std::uint32_t epoch{0};
    InvitationKey signer{}, peer_a{}, peer_b{};
    InvitationToken nonce{}, boot_a{}, boot_b{};
    std::uint64_t issued_a_ms{0}, issued_b_ms{0};
    std::uint32_t window_a_ms{0}, window_b_ms{0};
};

namespace independent_invitation_detail {
inline constexpr std::array<std::uint8_t, 8> magic{'O','T','E','I','N','V',0,2};
inline bool window_valid(std::uint64_t issued, std::uint32_t window) {
    return window != 0 && window <= kIndependentInvitationMaximumWindowMs &&
        issued <= std::numeric_limits<std::uint64_t>::max() - window;
}
inline bool valid(const IndependentInvitationFields& fields) {
    using invitation_detail::nonzero;
    return fields.group != 0 && fields.epoch != 0 && nonzero(fields.signer) &&
        nonzero(fields.peer_a) && nonzero(fields.peer_b) && fields.peer_a != fields.peer_b &&
        nonzero(fields.nonce) && nonzero(fields.boot_a) && nonzero(fields.boot_b) &&
        window_valid(fields.issued_a_ms, fields.window_a_ms) &&
        window_valid(fields.issued_b_ms, fields.window_b_ms);
}
inline IndependentInvitationFields decode(const IndependentInvitation& invitation) {
    IndependentInvitationFields fields{};
    const auto* p = invitation.payload.data();
    fields.group = invitation_detail::get(p + 8, 8);
    fields.epoch = static_cast<std::uint32_t>(invitation_detail::get(p + 16, 4));
    std::memcpy(fields.signer.data(), p + 20, 32);
    std::memcpy(fields.peer_a.data(), p + 52, 32);
    std::memcpy(fields.peer_b.data(), p + 84, 32);
    std::memcpy(fields.nonce.data(), p + 116, 16);
    std::memcpy(fields.boot_a.data(), p + 132, 16);
    fields.issued_a_ms = invitation_detail::get(p + 148, 8);
    fields.window_a_ms = static_cast<std::uint32_t>(invitation_detail::get(p + 156, 4));
    std::memcpy(fields.boot_b.data(), p + 160, 16);
    fields.issued_b_ms = invitation_detail::get(p + 176, 8);
    fields.window_b_ms = static_cast<std::uint32_t>(invitation_detail::get(p + 184, 4));
    return fields;
}
} // namespace independent_invitation_detail

// The trusted inviter signs the resulting exact payload separately. Invalid
// fields leave the caller's entire destination, including signature, unchanged.
inline bool encode_independent_invitation(const IndependentInvitationFields& fields,
                                           IndependentInvitation& output) {
    if (!independent_invitation_detail::valid(fields)) return false;
    IndependentInvitation result{};
    auto* p = result.payload.data();
    std::memcpy(p, independent_invitation_detail::magic.data(), 8);
    invitation_detail::put(p + 8, fields.group, 8);
    invitation_detail::put(p + 16, fields.epoch, 4);
    std::memcpy(p + 20, fields.signer.data(), 32);
    std::memcpy(p + 52, fields.peer_a.data(), 32);
    std::memcpy(p + 84, fields.peer_b.data(), 32);
    std::memcpy(p + 116, fields.nonce.data(), 16);
    std::memcpy(p + 132, fields.boot_a.data(), 16);
    invitation_detail::put(p + 148, fields.issued_a_ms, 8);
    invitation_detail::put(p + 156, fields.window_a_ms, 4);
    std::memcpy(p + 160, fields.boot_b.data(), 16);
    invitation_detail::put(p + 176, fields.issued_b_ms, 8);
    invitation_detail::put(p + 184, fields.window_b_ms, 4);
    output = result;
    return true;
}

// One sequential owner. Failure or repeated open consumes this gate permanently.
class IndependentInvitationGate final {
public:
    IndependentInvitationGate() = default;
    IndependentInvitationGate(const IndependentInvitationGate&) = delete;
    IndependentInvitationGate& operator=(const IndependentInvitationGate&) = delete;

    bool open(const IndependentInvitation& invitation, InvitationRole role,
              const InvitationKey& trusted_signer, const InvitationKey& expected_a,
              const InvitationKey& expected_b, const InvitationToken& expected_local_boot,
              std::uint64_t now) {
        if (phase_ != Phase::unused) return burn();
        phase_ = Phase::failed;
        // Signature verification and prologue hashing use one stable copy.
        const IndependentInvitation candidate = invitation;
        const auto fields = independent_invitation_detail::decode(candidate);
        if (!authority_detail::role_valid(role) ||
            std::memcmp(candidate.payload.data(), independent_invitation_detail::magic.data(), 8) != 0 ||
            !independent_invitation_detail::valid(fields) ||
            sodium_memcmp(fields.signer.data(), trusted_signer.data(), 32) != 0 ||
            sodium_memcmp(fields.peer_a.data(), expected_a.data(), 32) != 0 ||
            sodium_memcmp(fields.peer_b.data(), expected_b.data(), 32) != 0) return burn();
        const bool initiator = role == InvitationRole::initiator;
        const auto& local_boot = initiator ? fields.boot_a : fields.boot_b;
        const auto issued = initiator ? fields.issued_a_ms : fields.issued_b_ms;
        const auto window = initiator ? fields.window_a_ms : fields.window_b_ms;
        const auto deadline = issued + window; // Both windows already checked for overflow.
        if (sodium_memcmp(local_boot.data(), expected_local_boot.data(), local_boot.size()) != 0 ||
            now < issued || now >= deadline ||
            crypto_sign_verify_detached(candidate.signature.data(), candidate.payload.data(),
                candidate.payload.size(), trusted_signer.data()) != 0) return burn();
        std::array<std::uint8_t, kIndependentInvitationPayloadBytes + 64> signed_context{};
        std::memcpy(signed_context.data(), candidate.payload.data(), candidate.payload.size());
        std::memcpy(signed_context.data() + candidate.payload.size(), candidate.signature.data(), 64);
        InvitationKey common_prologue{};
        if (crypto_hash_sha256(common_prologue.data(), signed_context.data(), signed_context.size()) != 0)
            return burn();
        fields_ = fields;
        prologue_ = common_prologue;
        issued_ms_ = issued;
        deadline_ms_ = deadline;
        last_now_ = now;
        phase_ = Phase::opened;
        return true;
    }
    bool advance(std::uint64_t now) {
        if (phase_ == Phase::unused || phase_ == Phase::failed || now < last_now_ || now >= deadline_ms_)
            return burn();
        last_now_ = now;
        return true;
    }
    bool bind_transcript(const InvitationKey& actual_transcript, std::uint64_t now) {
        if (phase_ != Phase::opened || !advance(now) || !invitation_detail::nonzero(actual_transcript))
            return burn();
        transcript_ = actual_transcript;
        phase_ = Phase::confirmation_pending;
        return true;
    }
    bool confirm(const InvitationKey& supplied_transcript, std::uint64_t now) {
        if (phase_ != Phase::confirmation_pending || !advance(now) ||
            sodium_memcmp(transcript_.data(), supplied_transcript.data(), 32) != 0) return burn();
        phase_ = Phase::confirmed;
        return true;
    }
    void revoke() { (void)burn(); }
    bool confirmed() const { return phase_ == Phase::confirmed; }
    bool failed() const { return phase_ == Phase::failed; }
    const InvitationKey& prologue() const { return prologue_; }
    std::uint64_t group() const { return fields_.group; }
    std::uint32_t epoch() const { return fields_.epoch; }
    const InvitationKey& peer_a() const { return fields_.peer_a; }
    const InvitationKey& peer_b() const { return fields_.peer_b; }
    std::uint64_t issued_ms() const { return issued_ms_; }
    std::uint64_t deadline_ms() const { return deadline_ms_; }

private:
    enum class Phase { unused, opened, confirmation_pending, confirmed, failed };
    bool burn() {
        phase_ = Phase::failed;
        fields_ = {};
        prologue_ = {};
        transcript_ = {};
        issued_ms_ = 0;
        deadline_ms_ = 0;
        last_now_ = 0;
        return false;
    }
    Phase phase_{Phase::unused};
    IndependentInvitationFields fields_{};
    InvitationKey prologue_{}, transcript_{};
    std::uint64_t issued_ms_{0}, deadline_ms_{0}, last_now_{0};
};
} // namespace opentrail::security_evaluation
