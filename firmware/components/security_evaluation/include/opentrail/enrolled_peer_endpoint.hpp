#pragma once
// OT238 evaluation-only owner. A locally provisioned signer/group is an explicit
// evaluation trust anchor, NOT authenticated product bootstrap. Durable records
// bind public signed enrollment evidence, never keys or permission to resume.
// Optional OT239 evidence storage owns all public invitation bytes and rejects
// caller-supplied retained data. Provision the signer/group locally before use.
// Initialize the crypto library before use. One serialized owner, six (seven with evidence) genuinely
// separate backing stores, one fresh attempt; reentry guards are not thread locks.
// Terminal clock/entropy observations must be read-only: no storage/authority
// mutations from sample()/state(). Other concurrent backing writers are forbidden.
#include "opentrail/independent_peer_traffic_endpoint.hpp"
#include "opentrail/peer_membership_store.hpp"
#include "opentrail/enrollment_evidence_store.hpp"
#include "opentrail/evaluation_storage_bank.hpp"

namespace opentrail::security_evaluation {
class EnrolledPeerEndpoint final {
public:
    EnrolledPeerEndpoint(security::SecureRandomSource& random,
        persistence::PersistentStorage& boot, persistence::PersistentStorage& roles,
        persistence::PersistentStorage& tx, persistence::PersistentStorage& rx,
        persistence::PersistentStorage& activation, persistence::PersistentStorage& membership,
        ConfirmationAuthority& clock, InvitationRole role, const InvitationKey& signer,
        std::uint64_t group, EnrollmentEvidenceStore* evidence = nullptr)
        : endpoint_(random, boot, roles, tx, rx, activation, clock, role, signer),
          membership_(membership), evidence_(evidence), random_(random), clock_(clock), role_(role), signer_(signer), group_(group),
          isolated_(&membership != &boot && &membership != &roles && &membership != &tx &&
                    &membership != &rx && &membership != &activation &&
                    (!evidence || (&evidence->storage() != &boot && &evidence->storage() != &roles &&
                     &evidence->storage() != &tx && &evidence->storage() != &rx &&
                     &evidence->storage() != &activation && &evidence->storage() != &membership))) {}
    // Preferred OT239 composition: fixed named views from one trusted backend.
    // The root/group/role still come from authenticated local provisioning.
    EnrolledPeerEndpoint(security::SecureRandomSource& random, EvaluationStorageBank& bank,
        EnrollmentEvidenceStore& evidence, ConfirmationAuthority& clock, InvitationRole role,
        const InvitationKey& signer, std::uint64_t group)
        : EnrolledPeerEndpoint(random, *bank.get(EvaluationNamespace::boot),
              *bank.get(EvaluationNamespace::role), *bank.get(EvaluationNamespace::transmit),
              *bank.get(EvaluationNamespace::receive), *bank.get(EvaluationNamespace::activation),
              *bank.get(EvaluationNamespace::membership), clock, role, signer, group, &evidence) {
        isolated_ = isolated_ && &evidence.storage() == bank.get(EvaluationNamespace::enrollment);
    }
    ~EnrolledPeerEndpoint() { (void)cancel(); }
    EnrolledPeerEndpoint(const EnrolledPeerEndpoint&) = delete;
    EnrolledPeerEndpoint& operator=(const EnrolledPeerEndpoint&) = delete;

    // Retained invitation is untrusted public data: its signature AND exact
    // durable digest must match. Missing public evidence refuses reconstruction.
    bool initialize(const IndependentInvitation* retained = nullptr) {
        auto copy = retained ? *retained : IndependentInvitation{};
        bool has_retained = retained != nullptr;
        return operation([&] {
            if (initialized_ || !isolated_ || !group_ || !authority_detail::role_valid(role_) ||
                !invitation_detail::nonzero(signer_)) return false;
            if (evidence_) {
                if (retained || !evidence_->initialize()) return false;
                evidence_initialized_ = true;
                if (!evidence_->empty()) {
                    if (!evidence_->read(copy)) return false;
                    has_retained = true;
                    evidence_expected_ = copy;
                    evidence_present_ = true;
                }
            }
            if (!membership_.initialize()) return false;
            if (membership_.empty()) {
                if (has_retained) return false;
            } else {
                if (!has_retained || !membership_.read(expected_) ||
                    expected_.state != MembershipState::active || !valid(copy) ||
                    digest(copy) != expected_.binding) return false;
                previous_ = copy;
            }
            initialized_ = true;
            return true;
        }, false);
    }
    bool prepare_identity() {
        return operation([&] {
            if (!initialized_ || identity_prepared_ || !observe(true) || !endpoint_.prepare_identity()) return false;
            identity_prepared_ = true;
            return true;
        });
    }
    const InvitationKey& public_identity() const { return endpoint_.public_identity(); }
    const InvitationToken& boot_context() const { return endpoint_.boot_context(); }
    bool begin(const IndependentInvitation& invitation) {
        const auto copy = invitation;
        return operation([&] {
            if (!initialized_ || begun_ || !valid(copy)) return false;
            const auto fields = independent_invitation_detail::decode(copy);
            if (expected_.generation != 0) {
                const auto prior = independent_invitation_detail::decode(previous_);
                // Both roles must generate new identities; a signed epoch alone
                // cannot bless accidental reuse of the prior key pair.
                if (fields.epoch <= prior.epoch || fields.peer_a == prior.peer_a ||
                    fields.peer_a == prior.peer_b || fields.peer_b == prior.peer_a ||
                    fields.peer_b == prior.peer_b || fields.nonce == prior.nonce) return false;
            }
            const auto& peer = role_ == InvitationRole::initiator ? fields.peer_b : fields.peer_a;
            if (!endpoint_.begin(copy, peer)) return false;
            // Actual local identity/boot/time validation precedes persistence.
            // No handshake output is exposed until the public evidence is durable.
            if (evidence_) {
                if (!evidence_->replace(copy)) return false;
                evidence_expected_ = copy;
                evidence_present_ = true;
            }
            pending_ = copy;
            begun_ = true;
            return true;
        });
    }
    bool next_handshake(HandshakeFrame& output) {
        HandshakeFrame staged{};
        const bool ok = operation([&] { return begun_ && endpoint_.next_handshake(staged); });
        if (ok) output = staged;
        sodium_memzero(&staged, sizeof staged);
        return ok;
    }
    bool receive_handshake(const HandshakeFrame& input) {
        const auto copy = input;
        return operation([&] { return begun_ && endpoint_.receive_handshake(copy); });
    }
    const IndependentConfirmationOffer* offer() {
        const IndependentConfirmationOffer* result = nullptr;
        const bool ok = operation([&] { result = endpoint_.offer(); return result != nullptr; });
        return ok ? result : nullptr;
    }
    bool confirm(const IndependentConfirmationOffer& expected) {
        return operation([&] { return endpoint_.confirm(expected); });
    }
    bool next_control(EvaluationRecord& output) {
        EvaluationRecord staged{};
        const bool ok = operation([&] { return endpoint_.next_control(staged) && activate(); });
        if (ok) output = staged;
        sodium_memzero(&staged, sizeof staged);
        return ok;
    }
    bool receive_control(const EvaluationRecord& input) {
        const auto copy = input;
        return operation([&] { return endpoint_.receive_control(copy) && activate(); });
    }
    bool send_status(std::uint8_t status, EvaluationRecord& output) {
        EvaluationRecord staged{};
        const bool ok = operation([&] { return active_ && endpoint_.send_status(status, staged); });
        if (ok) output = staged;
        sodium_memzero(&staged, sizeof staged);
        return ok;
    }
    bool receive_status(const EvaluationRecord& input, std::uint8_t& output) {
        const auto copy = input;
        std::uint8_t staged = 0;
        bool rejected = false;
        const bool ok = operation([&] {
            if (!active_) return false;
            if (!endpoint_.receive_status(copy, staged)) {
                rejected = true;
                return endpoint_.ready(); // bounded tag/replay refusal stays usable
            }
            return true;
        });
        if (ok && !rejected) output = staged;
        return ok && !rejected;
    }
    bool ready() {
        if (!active_) return false;
        return operation([&] { return endpoint_.ready(); });
    }
    bool poll() { return operation([&] { return begun_ && endpoint_.poll(); }); }

    // Cancel closes volatile keys but retains public membership for a later
    // explicitly signed fresh rekey. Revocation/reset are terminal tombstones.
    bool cancel() { return stop(0); }
    bool revoke() { return stop(1); }
    bool prepare_reset() { return stop(2); }
    EndpointState state() const { return endpoint_.state(); }
    bool secrets_cleared() const { return endpoint_.secrets_cleared(); }
    // True only if the most recent observe() call at this layer, or at the
    // durable handshake endpoint one layer deeper (the same invitation
    // window, mirrored there and reached via poll()), itself rejected on the
    // invitation-window predicate -- each at that call's own sampled time,
    // never re-derived later. Consuming (read-then-clear) at both layers so
    // a caller can attribute exactly one already-failed operation to this
    // specific boundary without risking a stale read from an earlier,
    // unrelated call.
    bool consume_window_expired() {
        const bool own = last_window_expired_; last_window_expired_ = false;
        const bool deep = endpoint_.consume_window_expired();
        return own || deep;
    }
    EnrolledFailureDetail consume_failure_detail() {
        const auto deep = endpoint_.consume_failure_detail();
        const auto value = failure_.reason == EnrolledFailureReason::none ? deep : failure_;
        failure_ = {};
        return value;
    }

private:
    bool valid(const IndependentInvitation& invitation) const {
        const auto fields = independent_invitation_detail::decode(invitation);
        return std::memcmp(invitation.payload.data(), independent_invitation_detail::magic.data(), 8) == 0 &&
            independent_invitation_detail::valid(fields) && fields.signer == signer_ && fields.group == group_ &&
            crypto_sign_verify_detached(invitation.signature.data(), invitation.payload.data(),
                invitation.payload.size(), signer_.data()) == 0;
    }
    InvitationKey digest(const IndependentInvitation& invitation) const {
        // Fixed public evidence + role, domain-separated from activation keys.
        constexpr unsigned char label[] = "OpenTrail OT238 membership evidence v1";
        std::array<unsigned char, sizeof(label) + 1 + kIndependentInvitationPayloadBytes + 64> bytes{};
        std::memcpy(bytes.data(), label, sizeof(label));
        bytes[sizeof(label)] = static_cast<unsigned char>(role_);
        std::memcpy(bytes.data() + sizeof(label) + 1, invitation.payload.data(), invitation.payload.size());
        std::memcpy(bytes.data() + sizeof(label) + 1 + invitation.payload.size(), invitation.signature.data(), 64);
        InvitationKey result{};
        if (crypto_hash_sha256(result.data(), bytes.data(), bytes.size()) != 0) result.fill(0);
        return result;
    }
    bool current() {
        if (!initialized_ || reentered_) return false;
        PeerMembership actual{};
        const bool membership_ok = expected_.generation == 0 ? membership_.empty() :
            (membership_.read(actual) && actual.state == MembershipState::active &&
             actual.generation == expected_.generation && actual.binding == expected_.binding);
        if (!membership_ok || reentered_) return reject(reentered_ ? EnrolledFailureReason::reentry : EnrolledFailureReason::membership_current);
        if (!evidence_) return true;
        if (!evidence_present_) {
            const bool empty = evidence_->empty();
            if (!empty || reentered_) return reject(reentered_ ? EnrolledFailureReason::reentry : EnrolledFailureReason::evidence_current);
            return true;
        }
        IndependentInvitation actual_evidence{};
        const bool same = evidence_->read(actual_evidence) && !reentered_ &&
            actual_evidence.payload == evidence_expected_.payload &&
            actual_evidence.signature == evidence_expected_.signature;
        if (!same) return reject(reentered_ ? EnrolledFailureReason::reentry : EnrolledFailureReason::evidence_current);
        return true;
    }

    bool activate() {
        if (active_ || !endpoint_.ready()) return !reentered_ && endpoint_.poll();
        const auto binding = digest(pending_);
        if (!invitation_detail::nonzero(binding) || !current()) return false;
        const auto prior_generation = expected_.generation;
        const bool committed = prior_generation == 0 ? membership_.enroll(binding) : membership_.rekey(binding);
        PeerMembership next{};
        if (!committed || reentered_ || !membership_.read(next) || reentered_ ||
            next.state != MembershipState::active || next.generation != prior_generation + 1 ||
            next.binding != binding) return false;
        expected_ = next;
        active_ = true;
        return true;
    }
    bool reject(EnrolledFailureReason reason, const ConfirmationSample* sample = nullptr) {
        if (failure_.reason == EnrolledFailureReason::none) {
            std::uint64_t issued = 0, deadline = 0;
            if (begun_) {
                const auto fields = independent_invitation_detail::decode(pending_);
                const bool a = role_ == InvitationRole::initiator;
                issued = a ? fields.issued_a_ms : fields.issued_b_ms;
                deadline = issued + (a ? fields.window_a_ms : fields.window_b_ms);
            }
            failure_ = {EnrolledFailureLayer::enrolled_peer, reason, sample != nullptr,
                begun_, false, sample ? sample->now_ms : 0, last_now_, issued, deadline, 0};
        }
        return false;
    }
    bool observe(bool initial = false) {
        last_window_expired_ = false;
        if (reentered_) return reject(EnrolledFailureReason::reentry);
        if (random_.state() != security::EntropyState::ready) return reject(EnrolledFailureReason::entropy);
        const auto sample = clock_.sample();
        if (reentered_) return reject(EnrolledFailureReason::reentry, &sample);
        if (random_.state() != security::EntropyState::ready) return reject(EnrolledFailureReason::entropy, &sample);
        if (reentered_) return reject(EnrolledFailureReason::reentry, &sample);
        if (sample.context.transport_generation == 0 || sample.context.session_nonce == 0)
            return reject(EnrolledFailureReason::invalid_context, &sample);
        if (sample.now_ms == std::numeric_limits<std::uint64_t>::max())
            return reject(EnrolledFailureReason::invalid_clock, &sample);
        if (clock_seen_ && sample.now_ms < last_now_)
            return reject(EnrolledFailureReason::clock_regression, &sample);
        if (initial) context_ = sample.context;
        else if (sample.context != context_) return reject(EnrolledFailureReason::context_changed, &sample);
        if (begun_) {
            const auto fields = independent_invitation_detail::decode(pending_);
            const bool a = role_ == InvitationRole::initiator;
            const auto issued = a ? fields.issued_a_ms : fields.issued_b_ms;
            const auto window = a ? fields.window_a_ms : fields.window_b_ms;
            if (sample.now_ms < issued || sample.now_ms >= issued + window) {
                last_window_expired_ = true;
                return reject(sample.now_ms < issued ? EnrolledFailureReason::window_before_issued :
                    EnrolledFailureReason::window_expired, &sample);
            }
        }
        clock_seen_ = true;
        last_now_ = sample.now_ms;
        return true;
    }
    template<class Action> bool operation(Action action, bool guard = true) {
        if (busy_) { reentered_ = true; return false; }
        if (closed_ || reentered_) return false;
        busy_ = true;
        failure_ = {};
        (void)endpoint_.consume_failure_detail();
        bool ok = (!guard || current()) && action();
        // Downstream observation can invoke storage/clock callbacks. Recheck
        // this owner's durable authority after those calls before publishing.
        if (ok) ok = current() && (!begun_ || endpoint_.poll()) && current();
        // Time/context/entropy can change during durable I/O. The final read-only
        // observations occur after that I/O; no callbacks follow before output.
        if (ok && identity_prepared_) ok = observe();
        if (!ok || reentered_) {
            const auto deep = endpoint_.consume_failure_detail();
            if (failure_.reason == EnrolledFailureReason::none) failure_ = deep;
            cleanup();
        }
        busy_ = false;
        return ok && !closed_ && !reentered_;
    }
    void cleanup() {
        closed_ = true;
        active_ = false;
        cleanup_ok_ = endpoint_.close() && endpoint_.secrets_cleared();
    }
    bool stop(unsigned kind) {
        if (busy_) { reentered_ = true; return false; }
        busy_ = true;
        if (!closed_) cleanup();
        bool durable = true;
        // Even when volatile cleanup fails, attempt the durable containment.
        if (kind) durable = initialized_ && (kind == 1 ? membership_.revoke() : membership_.prepare_reset());
        // Attempt both containment records even if one side fails. An incomplete
        // reset never reports success and never erases counters or keys for reuse.
        bool evidence_reset = true;
        if (kind == 2 && evidence_) evidence_reset = evidence_initialized_ && evidence_->prepare_reset();
        busy_ = false;
        return cleanup_ok_ && durable && evidence_reset && !reentered_;
    }
    IndependentPeerTrafficEndpoint endpoint_;
    PeerMembershipStore membership_;
    EnrollmentEvidenceStore* const evidence_;
    security::SecureRandomSource& random_;
    ConfirmationAuthority& clock_;
    const InvitationRole role_;
    const InvitationKey signer_;
    const std::uint64_t group_;
    bool isolated_;
    PeerMembership expected_{};
    IndependentInvitation previous_{}, pending_{}, evidence_expected_{};
    bool evidence_present_{false}, evidence_initialized_{false};
    ConfirmationContext context_{};
    std::uint64_t last_now_{0};
    bool clock_seen_{false}, identity_prepared_{false};
    bool initialized_{false}, begun_{false}, active_{false}, closed_{false};
    bool busy_{false}, reentered_{false}, cleanup_ok_{false};
    bool last_window_expired_{false};
    EnrolledFailureDetail failure_{};
};
} // namespace opentrail::security_evaluation
