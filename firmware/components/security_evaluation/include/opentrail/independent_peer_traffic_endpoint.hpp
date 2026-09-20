#pragma once
// OT235 host integration candidate. Not a selected product wire or trust issuer.
// One serialized owner supplies authenticated signer/peer provisioning and five
// isolated stores. An activation receipt is durable evidence, never a resume key.
// All controls and fixed statuses expire with the original signed local window.
#include "opentrail/independent_handshake_endpoint.hpp"
#include "opentrail/peer_activation_store.hpp"

namespace opentrail::security_evaluation {
class IndependentPeerTrafficEndpoint final {
public:
    IndependentPeerTrafficEndpoint(security::SecureRandomSource& random,
        persistence::PersistentStorage& boot, persistence::PersistentStorage& role_store,
        persistence::PersistentStorage& tx, persistence::PersistentStorage& rx,
        persistence::PersistentStorage& trust, ConfirmationAuthority& clock,
        InvitationRole role, const InvitationKey& signer)
        : endpoint_(random, boot, role_store, tx, rx, clock, role, signer), trust_(trust),
          role_(role), isolated_(&trust != &boot && &trust != &role_store &&
                                &trust != &tx && &trust != &rx) {}
    ~IndependentPeerTrafficEndpoint() { (void)close(); }
    IndependentPeerTrafficEndpoint(const IndependentPeerTrafficEndpoint&) = delete;
    IndependentPeerTrafficEndpoint& operator=(const IndependentPeerTrafficEndpoint&) = delete;

    bool prepare_identity() { return operation([&] { return isolated_ && endpoint_.prepare_identity(); }, false); }
    const InvitationKey& public_identity() const { return endpoint_.public_identity(); }
    const InvitationToken& boot_context() const { return endpoint_.boot_context(); }
    bool begin(const IndependentInvitation& invitation, const InvitationKey& peer) {
        const auto candidate = invitation;
        const auto pin = peer;
        return operation([&] {
            if (!endpoint_.begin(candidate, pin)) return false;
            // Signed invitation contains both exact identities, group/epoch,
            // boot contexts, nonce and local windows. Bind its signature too.
            crypto_hash_sha256_state hash{};
            const bool ok = crypto_hash_sha256_init(&hash) == 0 &&
                crypto_hash_sha256_update(&hash, candidate.payload.data(), candidate.payload.size()) == 0 &&
                crypto_hash_sha256_update(&hash, candidate.signature.data(), candidate.signature.size()) == 0 &&
                crypto_hash_sha256_final(&hash, invitation_digest_.data()) == 0;
            sodium_memzero(&hash, sizeof hash);
            begun_ = ok;
            return ok;
        });
    }
    bool next_handshake(HandshakeFrame& out) {
        HandshakeFrame staged{};
        const bool ok = operation([&] { return endpoint_.next_send(staged); });
        if (ok) out = staged;
        sodium_memzero(&staged, sizeof staged);
        return ok;
    }
    bool receive_handshake(const HandshakeFrame& in) {
        const auto copy = in;
        return operation([&] { return endpoint_.receive(copy); });
    }
    const IndependentConfirmationOffer* offer() {
        const IndependentConfirmationOffer* value = nullptr;
        const bool ok = operation([&] { value = endpoint_.offer(); return value != nullptr; });
        return ok ? value : nullptr;
    }
    bool confirm(const IndependentConfirmationOffer& expected) {
        return operation([&] {
            if (!endpoint_.confirm(expected)) return false;
            // A local marker is not membership: the peer must prove its own
            // local decision through the direction-bound authenticated channel.
            constexpr unsigned char label[] = "OpenTrail OT235 activation receipt v1";
            const auto role = static_cast<unsigned char>(role_);
            crypto_hash_sha256_state hash{};
            const bool ok = crypto_hash_sha256_init(&hash) == 0 &&
                crypto_hash_sha256_update(&hash, label, sizeof(label)-1) == 0 &&
                crypto_hash_sha256_update(&hash, invitation_digest_.data(), invitation_digest_.size()) == 0 &&
                crypto_hash_sha256_update(&hash, expected.transcript().data(), expected.transcript().size()) == 0 &&
                crypto_hash_sha256_update(&hash, &role, 1) == 0 &&
                crypto_hash_sha256_final(&hash, binding_.data()) == 0;
            sodium_memzero(&hash, sizeof hash);
            local_confirmed_ = ok;
            return ok;
        });
    }
    // Exactly one confirmation and one durable-activation control per endpoint.
    // Exporting a record does not assert transport delivery or peer acceptance.
    bool next_control(EvaluationRecord& out) {
        EvaluationRecord staged{};
        const bool ok = operation([&] {
            if (!local_confirmed_ || !fresh()) return false;
            if (!confirmation_sent_) {
                if (!endpoint_.seal_record(control(1), staged)) return false;
                confirmation_sent_ = true;
            } else {
                if (!committed_ || activation_sent_ || !endpoint_.seal_record(control(2), staged)) return false;
                activation_sent_ = true;
            }
            return true;
        });
        if (ok) out = staged;
        sodium_memzero(&staged, sizeof staged);
        return ok;
    }
    bool receive_control(const EvaluationRecord& in) {
        const auto candidate = in;
        bool rejected = false;
        const bool ok = operation([&] {
            if (!local_confirmed_ || !fresh()) return false;
            std::array<unsigned char, 8> value{};
            if (!endpoint_.open_record(candidate, value)) { rejected = true; return true; }
            const auto expected_role = role_ == InvitationRole::initiator ? InvitationRole::responder : InvitationRole::initiator;
            const bool canonical = value[0]=='O' && value[1]=='T' && value[2]==1 &&
                value[4]==static_cast<unsigned char>(expected_role) && value[5]==0 && value[6]==0 && value[7]==0;
            const auto kind = value[3];
            sodium_memzero(value.data(), value.size());
            if (!canonical) return false;
            if (kind == 1) {
                if (committed_ || !trust_.commit(binding_)) return false;
                committed_ = true;
            } else if (kind == 2) {
                if (!committed_ || !confirmation_sent_ || peer_active_) return false;
                peer_active_ = true;
            } else return false;
            return true;
        });
        return ok && !rejected;
    }
    // Bounded host status identifiers only. No arbitrary payload, phone API,
    // delivery ACK, application wire selection or retained-key restart support.
    bool send_status(std::uint8_t status, EvaluationRecord& out) {
        EvaluationRecord staged{};
        const bool ok = operation([&] {
            if (status < 1 || status > 8 || !active() || !fresh()) return false;
            auto value = control(3); value[4] = status;
            return endpoint_.seal_record(value, staged);
        });
        if (ok) out = staged;
        sodium_memzero(&staged, sizeof staged);
        return ok;
    }
    bool receive_status(const EvaluationRecord& in, std::uint8_t& status) {
        const auto candidate = in;
        std::uint8_t staged = 0;
        bool rejected = false;
        const bool ok = operation([&] {
            if (!active() || !fresh()) return false;
            std::array<unsigned char, 8> value{};
            if (!endpoint_.open_record(candidate, value)) { rejected = true; return true; }
            const bool valid = value[0]=='O' && value[1]=='T' && value[2]==1 && value[3]==3 &&
                value[4]>=1 && value[4]<=8 && value[5]==0 && value[6]==0 && value[7]==0;
            staged = value[4];
            sodium_memzero(value.data(), value.size());
            return valid;
        });
        if (ok && !rejected) status = staged;
        return ok && !rejected;
    }
    bool ready() { return operation([&] { return fresh(); }) && active(); }
    bool poll() { return operation([&] { return begun_ && fresh(); }); }
    // Revocation, transport disconnect, reset preparation and normal shutdown
    // all close volatile keys and retire this exact durable activation receipt.
    // This does not erase user data or implement the factory-reset coordinator.
    bool close() {
        if (busy_) { reentered_ = true; return false; }
        if (closed_) return cleanup_ok_ && !reentered_;
        busy_ = true;
        cleanup();
        busy_ = false;
        return cleanup_ok_ && !reentered_;
    }
    EndpointState state() const { return endpoint_.state(); }
    bool secrets_cleared() const { return endpoint_.secrets_cleared(); }
    // Relays the durable handshake endpoint's own consuming accessor; this
    // layer has no separate invitation-window check of its own.
    bool consume_window_expired() { return endpoint_.consume_window_expired(); }
    EnrolledFailureDetail consume_failure_detail() { return endpoint_.consume_failure_detail(); }
private:
    std::array<unsigned char, 8> control(unsigned char kind) const {
        return {'O','T',1,kind,static_cast<unsigned char>(role_),0,0,0};
    }
    bool active() const { return local_confirmed_ && committed_ && activation_sent_ && peer_active_ && !closed_; }
    bool fresh() {
        return !reentered_ && (!committed_ || trust_.current()) && !reentered_ && endpoint_.poll() && !reentered_;
    }
    template<class Action> bool operation(Action action, bool observe = true) {
        if (busy_) { reentered_ = true; return false; }
        if (closed_) return false;
        busy_ = true;
        bool ok = action();
        if (ok && observe) ok = begun_ && fresh();
        if (!ok || reentered_) cleanup();
        busy_ = false;
        return ok && !reentered_ && !closed_;
    }
    void cleanup() {
        closed_ = true;
        const bool durable = committed_ ? trust_.retire() : !trust_.failed();
        const bool session = endpoint_.close();
        cleanup_ok_ = durable && session && endpoint_.secrets_cleared();
        sodium_memzero(binding_.data(), binding_.size());
    }
    IndependentHandshakeEndpoint endpoint_;
    PeerActivationStore trust_;
    const InvitationRole role_;
    const bool isolated_;
    InvitationKey invitation_digest_{}, binding_{};
    bool begun_{false}, local_confirmed_{false}, confirmation_sent_{false}, committed_{false};
    bool activation_sent_{false}, peer_active_{false}, busy_{false}, reentered_{false};
    bool closed_{false}, cleanup_ok_{false};
};
} // namespace opentrail::security_evaluation
