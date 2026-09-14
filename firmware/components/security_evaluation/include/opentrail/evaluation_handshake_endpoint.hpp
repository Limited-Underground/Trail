#pragma once
// OT227 host-transport evaluation. One endpoint owns one local identity, boot
// authority and confirmation owner. The trusted composition supplies isolated
// storage, entropy, Ready authority and pins; received bytes supply none of them.
// As with the frozen session core, the composition initializes its crypto library
// before use. This component never initializes global entropy or dispatch state.
// This value envelope is not a selected product/radio wire format. There is no
// retransmission, membership, peer-confirmation or traffic API.
// One serialized caller owns all access; the reentry guard is not a thread lock.
// Distinct storage wrappers must also have distinct backing stores/namespaces.
#include "opentrail/evaluation_confirmation_owner.hpp"

namespace opentrail::security_evaluation {
struct HandshakeFrame {
    std::uint8_t version{0};
    std::uint8_t step{0};
    std::uint16_t payload_bytes{0};
    std::array<unsigned char, 128> payload{};
};

enum class EndpointState { empty, identity, handshake, review, local_confirmed, cancelled, refused };

class EvaluationHandshakeEndpoint final {
public:
    EvaluationHandshakeEndpoint(security::SecureRandomSource& random,
                                persistence::PersistentStorage& boot_storage,
                                persistence::PersistentStorage& role_storage,
                                persistence::PersistentStorage& tx_storage,
                                persistence::PersistentStorage& rx_storage,
                                ConfirmationAuthority& authority, InvitationRole role,
                                const InvitationKey& trusted_signer)
        : random_(random), role_storage_(role_storage), authority_(authority), role_(role),
          signer_(trusted_signer), boot_(boot_storage), session_(random, tx_storage, rx_storage),
          configuration_valid_(authority_detail::role_valid(role) && invitation_detail::nonzero(signer_) &&
              &boot_storage != &role_storage && &boot_storage != &tx_storage && &boot_storage != &rx_storage &&
              &role_storage != &tx_storage && &role_storage != &rx_storage && &tx_storage != &rx_storage) {}
    ~EvaluationHandshakeEndpoint() { (void)close(); }
    EvaluationHandshakeEndpoint(const EvaluationHandshakeEndpoint&) = delete;
    EvaluationHandshakeEndpoint& operator=(const EvaluationHandshakeEndpoint&) = delete;

    bool prepare_identity() {
        return operation([&] {
            if (state_ != EndpointState::empty || !configuration_valid_ || !observe(true) ||
                !observe() || !boot_.start()) return false;
            boot_started_ = true;
            if (!observe() || !session_.generate_identity() || !observe()) return false;
            state_ = EndpointState::identity;
            return true;
        });
    }
    // Public provisioning material only; valid after prepare_identity succeeds.
    // The frozen boot token is scoped to its ledger, not globally unique. A
    // common signed invitation requires equal contexts at both endpoints; a
    // different retained boot generation is refused, never coerced into a match.
    const InvitationKey& public_identity() const { return session_.public_identity(); }
    const InvitationToken& boot_context() const { return boot_.context(); }

    bool begin(const Invitation& invitation, const InvitationKey& trusted_peer_pin) {
        // Copy caller-owned inputs before any storage/authority/entropy callback.
        const Invitation candidate = invitation;
        const InvitationKey peer = trusted_peer_pin;
        return operation([&] {
            if (state_ != EndpointState::identity || !invitation_detail::nonzero(peer) ||
                peer == public_identity() || !observe()) return false;
            const auto& a = role_ == InvitationRole::initiator ? public_identity() : peer;
            const auto& b = role_ == InvitationRole::initiator ? peer : public_identity();
            role_authority_.emplace(role_storage_, boot_, role_, signer_, a, b);
            owner_.emplace(session_, *role_authority_, authority_);
            if (!owner_->begin(candidate)) return false;
            issued_ms_ = invitation_detail::get(candidate.payload.data() + 148, 8);
            deadline_ms_ = invitation_detail::get(candidate.payload.data() + 156, 8);
            invitation_begun_ = true;
            if (!observe(false, true)) return false;
            next_step_ = 1;
            state_ = EndpointState::handshake;
            return true;
        });
    }

    bool next_send(HandshakeFrame& output) {
        output = {};
        HandshakeFrame staged{};
        const bool accepted = operation([&] {
            if (state_ != EndpointState::handshake || !send_turn() || !observe(false, true)) return false;
            std::size_t bytes = 0;
            if (!owner_->write(staged.payload.data(), staged.payload.size(), bytes) ||
                bytes == 0 || bytes > staged.payload.size()) return false;
            staged.version = 1;
            staged.step = next_step_;
            staged.payload_bytes = static_cast<std::uint16_t>(bytes);
            return advance();
        });
        // Neither late completion nor reentrant invalidation can publish bytes.
        if (accepted) output = staged;
        else output = {};
        sodium_memzero(staged.payload.data(), staged.payload.size());
        return accepted;
    }

    bool receive(const HandshakeFrame& input) {
        const HandshakeFrame candidate = input;
        return operation([&] {
            if (state_ != EndpointState::handshake || send_turn() || candidate.version != 1 ||
                candidate.step != next_step_ || candidate.payload_bytes == 0 ||
                candidate.payload_bytes > candidate.payload.size()) return false;
            for (std::size_t i = candidate.payload_bytes; i < candidate.payload.size(); ++i)
                if (candidate.payload[i] != 0) return false;
            if (!observe(false, true) || !owner_->read(candidate.payload.data(), candidate.payload_bytes)) return false;
            return advance();
        });
    }

    const ConfirmationOffer* offer() {
        const ConfirmationOffer* result = nullptr;
        const bool accepted = operation([&] {
            if (state_ != EndpointState::review || !observe(false, true)) return false;
            result = owner_->offer();
            return result != nullptr && observe(false, true);
        });
        return accepted ? result : nullptr;
    }
    bool confirm(const ConfirmationOffer& expected) {
        return operation([&] {
            if (state_ != EndpointState::review || !observe(false, true) ||
                !owner_->confirm(expected) || !observe(false, true)) return false;
            state_ = EndpointState::local_confirmed;
            return true;
        });
    }
    bool cancel(const ConfirmationOffer& expected) {
        return operation([&] {
            if (state_ != EndpointState::review || !observe(false, true) ||
                !owner_->cancel(expected) || !observe()) return false;
            const bool cleaned = cleanup();
            if (cleaned) state_ = EndpointState::cancelled;
            return cleaned;
        });
    }
    // Required explicit cleanup, including durable RX retirement after local
    // confirmation. Destruction is a best-effort fallback, not accepted cleanup.
    bool close() {
        if (busy_) { revoked_ = true; return false; }
        if (cleanup_started_) return cleanup_ok_ && !revoked_;
        busy_ = true;
        const bool cleaned = cleanup();
        if (state_ != EndpointState::refused)
            state_ = cleaned && !revoked_ ? EndpointState::cancelled : EndpointState::refused;
        busy_ = false;
        return cleaned && !revoked_;
    }
    // Historical local outcome only. It is never a Ready/membership capability.
    EndpointState state() const { return state_; }
    bool secrets_cleared() const { return session_.secrets_cleared(); }

private:
    template<class Action> bool operation(Action action) {
        if (busy_) { revoked_ = true; return false; }
        if (cleanup_started_) return false;
        busy_ = true;
        const bool accepted = action();
        if (!accepted || revoked_) {
            state_ = EndpointState::refused;
            (void)cleanup();
        }
        busy_ = false;
        return accepted && !revoked_ && state_ != EndpointState::refused;
    }
    bool observe(bool initial = false, bool require_role = false) {
        if (revoked_ || random_.state() != security::EntropyState::ready || revoked_) return false;
        if (boot_started_ && (!boot_.current() || revoked_)) return false;
        if (require_role && (!role_authority_ || !role_authority_->current() || revoked_)) return false;
        // Sample time after durable readback, preserving the original Ready
        // context across preparation and every later endpoint operation.
        const auto sample = authority_.sample();
        if (revoked_ || random_.state() != security::EntropyState::ready || revoked_ ||
            sample.context.transport_generation == 0 || sample.context.session_nonce == 0 ||
            sample.now_ms == std::numeric_limits<std::uint64_t>::max() ||
            (clock_seen_ && sample.now_ms < last_now_)) return false;
        if (initial) context_ = sample.context;
        else if (sample.context != context_) return false;
        if (invitation_begun_ && (sample.now_ms < issued_ms_ || sample.now_ms >= deadline_ms_)) return false;
        last_now_ = sample.now_ms;
        clock_seen_ = true;
        return true;
    }
    bool send_turn() const {
        return role_ == InvitationRole::initiator ? next_step_ == 1 || next_step_ == 3 : next_step_ == 2;
    }
    bool advance() {
        if (next_step_ == 3) {
            if (!owner_->finish() || owner_->offer() == nullptr || !observe(false, true)) return false;
            state_ = EndpointState::review;
        } else if (!observe(false, true)) return false;
        ++next_step_;
        return true;
    }
    bool cleanup() {
        if (cleanup_started_) return cleanup_ok_;
        cleanup_started_ = true;
        if (owner_) cleanup_ok_ = owner_->close() && session_.secrets_cleared();
        else {
            // No bound role exists yet. The frozen wrapper's cancel refusal
            // still invokes its guaranteed pre-storage core wipe.
            (void)session_.cancel();
            cleanup_ok_ = session_.secrets_cleared();
        }
        return cleanup_ok_;
    }

    security::SecureRandomSource& random_;
    persistence::PersistentStorage& role_storage_;
    ConfirmationAuthority& authority_;
    const InvitationRole role_;
    const InvitationKey signer_;
    InvitationBootAuthority boot_;
    AuthorizedPolicySession session_;
    std::optional<RoleInvitationAuthority> role_authority_;
    std::optional<EvaluationConfirmationOwner> owner_;
    const bool configuration_valid_;
    ConfirmationContext context_{};
    EndpointState state_{EndpointState::empty};
    std::uint64_t last_now_{0}, issued_ms_{0}, deadline_ms_{0};
    std::uint8_t next_step_{0};
    bool clock_seen_{false}, boot_started_{false}, invitation_begun_{false}, busy_{false}, revoked_{false};
    bool cleanup_started_{false}, cleanup_ok_{false};
};
} // namespace opentrail::security_evaluation
