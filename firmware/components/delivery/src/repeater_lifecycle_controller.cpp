#include "opentrail/repeater_lifecycle_controller.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::delivery {
namespace {
using State = RepeaterLifecycleState;
using Fault = RepeaterLifecycleFault;

bool same_scope(const RepeaterLifecycleScope& a,
                const RepeaterLifecycleScope& b) noexcept {
    return a.instance_id == b.instance_id && a.generation == b.generation &&
           a.group_context_id == b.group_context_id && a.group_epoch == b.group_epoch;
}

bool same_role(const RepeaterLifecycleRole& a,
               const RepeaterLifecycleRole& b) noexcept {
    return a.group_context_id == b.group_context_id && a.group_epoch == b.group_epoch &&
           a.generation == b.generation && a.enabled == b.enabled;
}

bool same_metadata(const VerifiedForwardingMetadata& a,
                   const VerifiedForwardingMetadata& b) noexcept {
    return a.group_context_id == b.group_context_id && a.source_alias == b.source_alias &&
        a.destination_alias == b.destination_alias && a.group_epoch == b.group_epoch &&
        a.message_id == b.message_id && a.source_authenticated == b.source_authenticated &&
        a.source_authorized == b.source_authorized && a.forwarding_permitted == b.forwarding_permitted;
}

bool same_ticket(const RepeaterLifecycleDispatchTicket& a,
                 const RepeaterLifecycleDispatchTicket& b) noexcept {
    return same_scope(a.scope, b.scope) && a.role_generation == b.role_generation &&
        a.readiness_generation == b.readiness_generation && a.candidate_id == b.candidate_id &&
        same_metadata(a.provenance.metadata, b.provenance.metadata) &&
        a.provenance.enqueued_at_ms == b.provenance.enqueued_at_ms &&
        a.provenance.maximum_queue_age_ms == b.provenance.maximum_queue_age_ms &&
        a.authority_deadline_ms == b.authority_deadline_ms &&
        a.consent_deadline_ms == b.consent_deadline_ms && a.frame_bytes == b.frame_bytes;
}
}  // namespace

RepeaterLifecycleController::Bundle::Bundle(
    const RepeaterLifecycleConfiguration& c,
    DuplicateCheckpointStorage& storage, SingleRepeaterAdmissionGate* gate) noexcept
    : duplicates(c.duplicate_retention_ms),
      store(storage, c.group_context_id, c.group_epoch),
      forwarder(c.local_alias, c.group_context_id, c.group_epoch, c.policy, duplicates, gate),
      coordinator(c.duplicate_retention_ms, c.group_context_id, c.group_epoch,
                  duplicates, store, forwarder) {}

RepeaterLifecycleController::RepeaterLifecycleController(
    const RepeaterLifecycleConfiguration& c,
    RepeaterLifecycleProvider& provider, RepeaterLifecycleRoleStorage& roles,
    DuplicateCheckpointStorage& replay_storage) noexcept
    : configuration_(c), provider_(provider), roles_(roles),
      replay_storage_(replay_storage), generation_(c.initial_generation),
      next_candidate_id_(c.initial_candidate_id) {
    configured_ = c.instance_id != 0 && c.initial_generation != 0 &&
        c.local_alias != 0 && c.group_context_id != 0 && c.group_epoch != 0 &&
        c.duplicate_retention_ms != 0 && c.maximum_frame_bytes > 0 &&
        c.maximum_frame_bytes <= radio::kMaximumFrameBytes &&
        c.policy.configured_authorized_repeaters == 1 &&
        c.policy.local_repeater_authorized && c.policy.maximum_queue_depth > 0 &&
        c.policy.maximum_queue_depth <= kSingleRepeaterQueueCapacity &&
        c.policy.maximum_forwards_per_window > 0 && c.policy.rate_window_ms > 0 &&
        c.policy.maximum_queue_age_ms > 0 &&
        (c.admission_gate == nullptr || c.bridge_mode) &&
        (!c.bridge_mode || c.initial_candidate_id != 0);
    if (!configured_) {
        state_ = State::contained;
        fault_ = Fault::invalid_configuration;
    } else if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
        state_ = State::contained;
        fault_ = Fault::generation_exhausted;
    }
}

RepeaterLifecycleScope RepeaterLifecycleController::scope() const noexcept {
    return {configuration_.instance_id, generation_, configuration_.group_context_id,
            configuration_.group_epoch};
}

bool RepeaterLifecycleController::retire(State state, Fault fault) noexcept {
    // Reentrant operations are refused by the public-call guard. Publish the
    // invalid lifetime before destroying its reference-bearing owner bundle.
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const bool available = generation_ < maximum - 1;
    generation_ = available ? generation_ + 1 : maximum;
    state_ = available ? state : State::contained;
    fault_ = available ? fault : Fault::generation_exhausted;
    lease_deadline_ms_ = 0;
    bound_ = {};
    pending_.reset();
    bundle_.reset();
    scratch_.fill(0);
    return available;
}

RepeaterLifecycleFault RepeaterLifecycleController::observe_time(
    std::uint64_t now_ms) noexcept {
    if (time_observed_ && now_ms < last_now_ms_) {
        retire(State::contained, Fault::clock_regression);
        return fault_;
    }
    time_observed_ = true;
    last_now_ms_ = now_ms;
    return Fault::none;
}

bool RepeaterLifecycleController::valid_snapshot(
    const RepeaterLifecycleSnapshot& s, std::uint64_t now_ms,
    bool enabled) const noexcept {
    return same_scope(s.scope, scope()) && s.role.group_context_id == configuration_.group_context_id &&
        s.role.group_epoch == configuration_.group_epoch && s.role.generation != 0 &&
        s.role.enabled == enabled && s.deadline_ms > now_ms &&
        (!enabled || (s.readiness_generation != 0 && s.membership_current &&
                      s.protected_namespace_verified && s.airtime_ready));
}

bool RepeaterLifecycleController::save_role(
    const RepeaterLifecycleRole& role) noexcept {
    if (roles_.write(role) != RepeaterRoleStorageError::none) {
        return false;  // A possibly committed write is never an acknowledgement.
    }
    RepeaterLifecycleRole readback{};
    return roles_.read(readback) == RepeaterRoleStorageError::none &&
           same_role(readback, role);
}

RepeaterLifecycleFault RepeaterLifecycleController::validate_current(
    std::uint64_t now_ms) noexcept {
    if (!bundle_ || (state_ != State::active && state_ != State::awaiting_consent)) {
        return Fault::wrong_state;
    }
    if (now_ms >= bound_.deadline_ms ||
        (state_ == State::active && now_ms >= lease_deadline_ms_)) {
        retire(State::suspended, Fault::expired);
        return fault_;
    }
    // Revalidate the original binding, not a newly supplied/extended snapshot.
    if (!provider_.current(bound_, now_ms)) {
        retire(State::contained, Fault::authority_changed);
        return fault_;
    }
    return Fault::none;
}

RepeaterLifecycleFault RepeaterLifecycleController::recover(
    std::uint64_t request_handle, std::uint64_t now_ms) noexcept {
    if (busy_) { return Fault::reentrant; }
    CallGuard guard(busy_);
    if (state_ == State::shutdown) { return Fault::wrong_state; }
    if (!configured_) { return Fault::invalid_configuration; }
    if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
        return Fault::generation_exhausted;
    }
    if (state_ != State::disabled && state_ != State::suspended && state_ != State::contained) {
        return Fault::wrong_state;
    }
    if (observe_time(now_ms) != Fault::none) { return fault_; }

    RepeaterLifecycleRequest request{};
    // Must precede any role mutation, replay initialization or degraded repair.
    if (!provider_.consume_request(RepeaterLifecycleOperation::recover,
            request_handle, scope(), now_ms, request) ||
        !valid_snapshot(request.authority, now_ms, true)) {
        return Fault::invalid_request;
    }
    state_ = State::recovering;
    RepeaterLifecycleRole actual{};
    const auto read = roles_.read(actual);
    if (!provider_.request_current(RepeaterLifecycleOperation::recover,
            request_handle, request, now_ms)) {
        retire(State::contained, Fault::authority_changed);
        return fault_;
    }
    if (request.authority.role.generation == std::numeric_limits<std::uint64_t>::max() ||
        (read == RepeaterRoleStorageError::none &&
         actual.generation == std::numeric_limits<std::uint64_t>::max())) {
        retire(State::contained, Fault::generation_exhausted);
        return fault_;
    }
    bool first_use = false;
    if (read == RepeaterRoleStorageError::not_found) {
        if (!request.first_use || !request.enable_role || first_use_spent_) {
            retire(State::contained, Fault::role_storage);
            return fault_;
        }
        first_use_spent_ = true;
        first_use = true;
        if (!save_role(request.authority.role)) {
            retire(State::contained, Fault::role_storage);
            return fault_;
        }
    } else if (read != RepeaterRoleStorageError::none || request.first_use ||
               actual.generation == 0) {
        retire(State::contained, Fault::role_storage);
        return fault_;
    } else if (!same_role(actual, request.authority.role)) {
        const bool enable = request.enable_role && !actual.enabled &&
            actual.group_context_id == configuration_.group_context_id &&
            actual.group_epoch == configuration_.group_epoch &&
            actual.generation != std::numeric_limits<std::uint64_t>::max() &&
            request.authority.role.generation == actual.generation + 1;
        if (!enable || !save_role(request.authority.role)) {
            retire(State::contained, Fault::role_storage);
            return fault_;
        }
    }
    if (!provider_.request_current(RepeaterLifecycleOperation::recover,
            request_handle, request, now_ms) ||
        !provider_.current(request.authority, now_ms)) {
        retire(State::contained, Fault::authority_changed);
        return fault_;
    }
    bundle_.emplace(configuration_, replay_storage_,
                    configuration_.bridge_mode ? &bridge_gate_ : nullptr);
    const auto boot = bundle_->coordinator.boot(
        {configuration_.group_context_id, configuration_.group_epoch,
         request.authority.protected_namespace_verified, first_use}, now_ms);
    if (!boot.operational()) {
        retire(State::contained, Fault::replay_storage);
        return fault_;
    }
    if (!provider_.request_current(RepeaterLifecycleOperation::recover,
            request_handle, request, now_ms) ||
        !provider_.current(request.authority, now_ms)) {
        retire(State::contained, Fault::authority_changed);
        return fault_;
    }
    bound_ = request.authority;
    lease_deadline_ms_ = 0;
    state_ = State::awaiting_consent;
    fault_ = Fault::none;
    return Fault::none;
}

RepeaterLifecycleFault RepeaterLifecycleController::consent(
    std::uint64_t handle, std::uint64_t now_ms) noexcept {
    if (busy_) { return Fault::reentrant; }
    CallGuard guard(busy_);
    if (state_ != State::awaiting_consent) { return Fault::wrong_state; }
    if (observe_time(now_ms) != Fault::none) { return fault_; }
    const auto check = validate_current(now_ms);
    if (check != Fault::none) { return check; }
    RepeaterLifecycleConsent consent{};
    if (!provider_.consume_consent(handle, bound_, now_ms, consent) ||
        !same_scope(consent.scope, bound_.scope) ||
        consent.role_generation != bound_.role.generation ||
        consent.readiness_generation != bound_.readiness_generation ||
        consent.deadline_ms <= now_ms || consent.deadline_ms > bound_.deadline_ms) {
        return Fault::invalid_request;
    }
    if (!provider_.current(bound_, now_ms)) {
        retire(State::contained, Fault::authority_changed);
        return fault_;
    }
    lease_deadline_ms_ = consent.deadline_ms;
    state_ = State::active;
    fault_ = Fault::none;
    return Fault::none;
}

RepeaterLifecycleAdmission RepeaterLifecycleController::admit(
    std::uint64_t handle, radio::ByteView bytes, std::uint64_t now_ms) noexcept {
    if (busy_) { return {Fault::reentrant}; }
    CallGuard guard(busy_);
    if (state_ != State::active) { return {Fault::wrong_state}; }
    if (observe_time(now_ms) != Fault::none) { return {fault_}; }
    const auto check = validate_current(now_ms);
    if (check != Fault::none) { return {check}; }
    if (bytes.data == nullptr || bytes.size == 0 ||
        bytes.size > configuration_.maximum_frame_bytes) {
        return {Fault::frame_rejected};
    }
    std::copy_n(bytes.data, bytes.size, scratch_.begin());
    const radio::ByteView exact{scratch_.data(), bytes.size};
    VerifiedForwardingMetadata metadata{};
    if (!provider_.verify_frame(handle, bound_, exact, now_ms, metadata)) {
        scratch_.fill(0);
        return {Fault::frame_rejected};
    }
    // Provider callbacks may report a changed world. Never grant it implicitly.
    if (!provider_.current(bound_, now_ms)) {
        retire(State::contained, Fault::authority_changed);
        return {fault_};
    }
    const auto processed = bundle_->coordinator.process_and_persist(metadata, exact, now_ms);
    scratch_.fill(0);
    RepeaterLifecycleAdmission result{Fault::none, processed.forwarding,
        processed.replay_state_persisted, processed.generation};
    if (processed.error != RepeaterReplayError::none) {
        retire(State::contained, Fault::replay_storage);
        result.error = fault_;
    } else if (processed.forwarding.disposition == SingleRepeaterDisposition::admission_uncertain) {
        retire(State::contained, Fault::admission_uncertain);
        result.error = fault_;
    } else if (!provider_.current(bound_, now_ms)) {
        retire(State::contained, Fault::authority_changed);
        result.error = fault_;
    }
    return result;
}

RepeaterLifecycleCandidate RepeaterLifecycleController::take_candidate(
    std::uint64_t now_ms) noexcept {
    if (busy_) { return {Fault::reentrant}; }
    if (configuration_.bridge_mode) { return {Fault::bridge_required}; }
    CallGuard guard(busy_);
    if (state_ != State::active) { return {Fault::wrong_state}; }
    if (observe_time(now_ms) != Fault::none) { return {fault_}; }
    const auto check = validate_current(now_ms);
    if (check != Fault::none) { return {check}; }
    const auto candidate = bundle_->coordinator.next_transmit(now_ms);
    if (candidate.error != RepeaterReplayError::none) {
        retire(State::contained, Fault::replay_storage);
        return {fault_};
    }
    return {Fault::none, generation_, candidate.forwarding};
}

SingleRepeaterAdmission RepeaterLifecycleController::BridgeAdmissionGate::evaluate(
    const VerifiedForwardingMetadata& metadata, std::size_t frame_bytes,
    std::uint64_t now_ms) noexcept {
    // Called inside the already-serialized admission after new-key observation.
    // Pressure refusal must flow back through the actual replay coordinator.
    if (owner_.pending_) { return SingleRepeaterAdmission::denied; }
    auto* external = owner_.configuration_.admission_gate;
    return external == nullptr ? SingleRepeaterAdmission::allow
                              : external->evaluate(metadata, frame_bytes, now_ms);
}

bool RepeaterLifecycleController::age_valid(
    const ForwardingProvenance& provenance, std::uint64_t now_ms) noexcept {
    return provenance.maximum_queue_age_ms != 0 && now_ms >= provenance.enqueued_at_ms &&
        now_ms - provenance.enqueued_at_ms < provenance.maximum_queue_age_ms;
}

RepeaterLifecyclePrepared RepeaterLifecycleController::prepare_dispatch(
    std::uint64_t now_ms) noexcept {
    if (busy_) { return {Fault::reentrant}; }
    CallGuard guard(busy_);
    if (!configuration_.bridge_mode) { return {Fault::bridge_required}; }
    if (state_ != State::active) { return {Fault::wrong_state}; }
    if (observe_time(now_ms) != Fault::none) { return {fault_}; }
    const auto check = validate_current(now_ms);
    if (check != Fault::none) { return {check}; }
    if (pending_) { return {Fault::pending_busy}; }
    if (next_candidate_id_ == std::numeric_limits<std::uint64_t>::max()) {
        retire(State::contained, Fault::generation_exhausted);
        return {fault_};
    }
    const auto result = bundle_->coordinator.next_transmit(now_ms);
    if (result.error != RepeaterReplayError::none) {
        retire(State::contained, Fault::replay_storage);
        return {fault_};
    }
    if (!result.forwarding.has_frame) {
        return {Fault::none, false, {}, result.forwarding.expired_frames_dropped};
    }
    const RepeaterLifecycleDispatchTicket ticket{
        bound_.scope, bound_.role.generation, bound_.readiness_generation,
        next_candidate_id_++, result.forwarding.provenance, bound_.deadline_ms,
        lease_deadline_ms_, result.forwarding.frame.size};
    pending_.emplace(Pending{result.forwarding.frame, ticket, true});
    return {Fault::none, true, ticket, result.forwarding.expired_frames_dropped};
}

RepeaterLifecycleClaimResult RepeaterLifecycleController::claim_dispatch(
    const RepeaterLifecycleDispatchTicket& ticket, std::uint64_t now_ms,
    RepeaterLifecycleClaimCallback& callback) noexcept {
    if (busy_) { return {Fault::reentrant}; }
    CallGuard guard(busy_);
    if (!configuration_.bridge_mode) { return {Fault::bridge_required}; }
    if (state_ != State::active) { return {Fault::wrong_state}; }
    // A stale/corrupt ticket cannot alter a different lifetime's clock or slot.
    if (!pending_ || !pending_->ticket_live || !same_ticket(ticket, pending_->ticket)) {
        return {Fault::invalid_request};
    }
    if (observe_time(now_ms) != Fault::none) { return {fault_}; }
    const auto check = validate_current(now_ms);
    if (check != Fault::none) { return {check}; }
    if (!age_valid(pending_->ticket.provenance, now_ms)) {
        pending_.reset();
        return {Fault::expired};
    }
    const auto original_ticket = pending_->ticket;
    pending_->ticket_live = false;  // Irreversible before any external callback.
    callback.invoke(original_ticket, {pending_->frame.bytes.data(), pending_->frame.size});
    pending_.reset();
    // A callback may observe/produce changed authority after the claim. Preserve
    // the truthful invoked boundary: a later refusal cannot mean zero emission.
    const auto after = validate_current(now_ms);
    return {after, true};
}

RepeaterLifecyclePruned RepeaterLifecycleController::prune_expired(
    std::uint64_t now_ms) noexcept {
    if (busy_) { return {Fault::reentrant}; }
    CallGuard guard(busy_);
    if (!configuration_.bridge_mode) { return {Fault::bridge_required}; }
    if (!bundle_ || (state_ != State::active && state_ != State::awaiting_consent)) {
        return {Fault::wrong_state};
    }
    if (observe_time(now_ms) != Fault::none) { return {fault_}; }
    if (now_ms >= bound_.deadline_ms ||
        (state_ == State::active && now_ms >= lease_deadline_ms_)) {
        const auto queued = static_cast<std::uint8_t>(bundle_->forwarder.status().queue_depth);
        const bool had_pending = pending_.has_value();
        retire(State::suspended, Fault::expired);
        return {fault_, queued, had_pending};
    }
    // Deleting expired work grants no authority; do not poll radio/budget ports
    // or pop a live frame merely to provide bounded expiry service.
    const auto queued = bundle_->forwarder.prune_expired(now_ms);
    const bool pending_expired = pending_ && !age_valid(pending_->ticket.provenance, now_ms);
    if (pending_expired) { pending_.reset(); }
    return {Fault::none, queued, pending_expired};
}

RepeaterLifecycleFault RepeaterLifecycleController::notify(
    std::uint64_t generation, RepeaterLifecycleEvent event,
    std::uint64_t now_ms) noexcept {
    if (busy_) { return Fault::reentrant; }
    CallGuard guard(busy_);
    if (state_ == State::shutdown) { return Fault::wrong_state; }
    if (generation != generation_) { return Fault::invalid_request; }
    if (state_ != State::active && state_ != State::awaiting_consent) {
        return Fault::wrong_state;
    }
    if (observe_time(now_ms) != Fault::none) { return fault_; }
    switch (event) {
        case RepeaterLifecycleEvent::suspend:
            retire(State::suspended, Fault::suspended); break;
        case RepeaterLifecycleEvent::radio_unavailable:
            retire(State::suspended, Fault::radio_unavailable); break;
        case RepeaterLifecycleEvent::authority_changed:
            retire(State::contained, Fault::authority_changed); break;
        case RepeaterLifecycleEvent::uncertain_submission:
            retire(State::contained, Fault::uncertain_submission); break;
        case RepeaterLifecycleEvent::rearm_failed:
            retire(State::contained, Fault::rearm_failed); break;
        default: return Fault::invalid_request;
    }
    return fault_;
}

RepeaterLifecycleFault RepeaterLifecycleController::disable(
    std::uint64_t request_handle, std::uint64_t now_ms) noexcept {
    if (busy_) { return Fault::reentrant; }
    CallGuard guard(busy_);
    if (state_ == State::shutdown) { return Fault::wrong_state; }
    if (!configured_) { return Fault::invalid_configuration; }
    if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
        return Fault::generation_exhausted;
    }
    if (observe_time(now_ms) != Fault::none) { return fault_; }
    RepeaterLifecycleRequest request{};
    if (!provider_.consume_request(RepeaterLifecycleOperation::disable,
            request_handle, scope(), now_ms, request) ||
        request.first_use || request.enable_role ||
        !valid_snapshot(request.authority, now_ms, false)) {
        return Fault::invalid_request;
    }
    RepeaterLifecycleRole actual{};
    const auto read = roles_.read(actual);
    if (!provider_.request_current(RepeaterLifecycleOperation::disable,
            request_handle, request, now_ms)) {
        retire(State::contained, Fault::authority_changed);
        return fault_;
    }
    if (request.authority.role.generation == std::numeric_limits<std::uint64_t>::max() ||
        (read == RepeaterRoleStorageError::none &&
         actual.generation == std::numeric_limits<std::uint64_t>::max())) {
        retire(State::contained, Fault::generation_exhausted);
        return fault_;
    }
    if (read != RepeaterRoleStorageError::none ||
        actual.generation == 0 ||
        actual.group_context_id != configuration_.group_context_id ||
        actual.group_epoch != configuration_.group_epoch ||
        (!same_role(actual, request.authority.role) &&
         (actual.generation == std::numeric_limits<std::uint64_t>::max() ||
          request.authority.role.generation != actual.generation + 1))) {
        retire(State::contained, Fault::role_storage);
        return fault_;
    }
    if (!retire(State::disabled, Fault::none)) { return fault_; }
    if (!same_role(actual, request.authority.role) && !save_role(request.authority.role)) {
        state_ = State::contained;
        fault_ = Fault::role_storage;
        return fault_;
    }
    if (!provider_.request_current(RepeaterLifecycleOperation::disable,
            request_handle, request, now_ms) ||
        !provider_.current(request.authority, now_ms)) {
        state_ = State::contained;
        fault_ = Fault::authority_changed;
        return fault_;
    }
    return Fault::none;
}

RepeaterLifecycleFault RepeaterLifecycleController::shutdown() noexcept {
    if (busy_) { return Fault::reentrant; }
    CallGuard guard(busy_);
    if (state_ == State::shutdown) { return Fault::none; }
    retire(State::shutdown, Fault::none);
    // Closure remains possible at exhaustion and never reopens a generation.
    state_ = State::shutdown;
    fault_ = Fault::none;
    return Fault::none;
}

RepeaterLifecycleStatus RepeaterLifecycleController::status() const noexcept {
    const bool live = bundle_ &&
        (state_ == State::active || state_ == State::awaiting_consent);
    return {state_, fault_, generation_,
        live ? bundle_->coordinator.status().persisted_generation : 0,
        live ? bundle_->forwarder.status().queue_depth : 0,
        live && pending_.has_value(),
        live ? bundle_->forwarder.status().queue_depth + (pending_ ? 1U : 0U) : 0,
        live && pending_ && !pending_->ticket_live};
}

}  // namespace opentrail::delivery
