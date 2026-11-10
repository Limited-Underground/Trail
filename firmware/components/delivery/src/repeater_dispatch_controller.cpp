#include "opentrail/repeater_dispatch_controller.hpp"

#include <limits>

namespace opentrail::delivery {
namespace {
using State = RepeaterDispatchState;
using Fault = RepeaterDispatchFault;
constexpr auto kMaximum = std::numeric_limits<std::uint64_t>::max();

bool same_source(const RepeaterDispatchSource& a, const RepeaterDispatchSource& b) noexcept {
    return a.group_context_id == b.group_context_id && a.group_epoch == b.group_epoch &&
           a.source_alias == b.source_alias;
}
bool same_ledger(const RepeaterDispatchLedger& a, const RepeaterDispatchLedger& b) noexcept {
    if (a.version != b.version || a.domain != b.domain || a.budget_epoch != b.budget_epoch ||
        a.sequence != b.sequence || a.global_remaining != b.global_remaining ||
        a.airtime_remaining_us != b.airtime_remaining_us || a.source_count != b.source_count) {
        return false;
    }
    for (std::size_t i = 0; i < kRepeaterDispatchSources; ++i) {
        if (!same_source(a.sources[i].source, b.sources[i].source) ||
            a.sources[i].remaining != b.sources[i].remaining) { return false; }
    }
    return true;
}
bool same_radio(const RepeaterDispatchRadioProof& a, const RepeaterDispatchRadioProof& b) noexcept {
    return a.lineage_id == b.lineage_id && a.driver_generation == b.driver_generation &&
        a.readiness_generation == b.readiness_generation &&
        a.quiescence_receipt == b.quiescence_receipt &&
        a.verified_quiescent == b.verified_quiescent && a.receive_ready == b.receive_ready;
}
bool same_ticket(const RepeaterLifecycleDispatchTicket& a,
                 const RepeaterLifecycleDispatchTicket& b) noexcept {
    const auto& x = a.provenance.metadata;
    const auto& y = b.provenance.metadata;
    return a.scope.instance_id == b.scope.instance_id && a.scope.generation == b.scope.generation &&
        a.scope.group_context_id == b.scope.group_context_id && a.scope.group_epoch == b.scope.group_epoch &&
        a.role_generation == b.role_generation && a.readiness_generation == b.readiness_generation &&
        a.candidate_id == b.candidate_id && a.frame_bytes == b.frame_bytes &&
        a.authority_deadline_ms == b.authority_deadline_ms && a.consent_deadline_ms == b.consent_deadline_ms &&
        a.provenance.enqueued_at_ms == b.provenance.enqueued_at_ms &&
        a.provenance.maximum_queue_age_ms == b.provenance.maximum_queue_age_ms &&
        x.group_context_id == y.group_context_id && x.group_epoch == y.group_epoch &&
        x.source_alias == y.source_alias && x.destination_alias == y.destination_alias &&
        x.message_id == y.message_id && x.source_authenticated == y.source_authenticated &&
        x.source_authorized == y.source_authorized && x.forwarding_permitted == y.forwarding_permitted;
}
bool same_attempt(const RepeaterDispatchAttempt& a, const RepeaterDispatchAttempt& b) noexcept {
    return a.attempt_id == b.attempt_id && same_ticket(a.ticket, b.ticket) &&
        a.budget_domain == b.budget_domain && a.budget_epoch == b.budget_epoch &&
        a.budget_receipt_sequence == b.budget_receipt_sequence && a.radio_lineage_id == b.radio_lineage_id &&
        a.driver_generation == b.driver_generation && a.readiness_generation == b.readiness_generation &&
        a.quiescence_receipt == b.quiescence_receipt;
}
}  // namespace

void increment_repeater_dispatch_counter(std::uint32_t& counter, bool& overflow) noexcept {
    if (counter == std::numeric_limits<std::uint32_t>::max()) { overflow = true; }
    else { ++counter; }
}

RepeaterLifecycleConfiguration RepeaterDispatchController::lifecycle_configuration(
    const RepeaterDispatchConfiguration& c, SingleRepeaterAdmissionGate* gate) noexcept {
    auto result = c.lifecycle;
    result.bridge_mode = true;
    result.admission_gate = gate;
    return result;
}

RepeaterDispatchController::RepeaterDispatchController(const RepeaterDispatchConfiguration& c,
    RepeaterLifecycleProvider& authority, RepeaterLifecycleRoleStorage& roles,
    DuplicateCheckpointStorage& replay, RepeaterDispatchLedgerPort& ledger,
    RepeaterDispatchRadioPort& radio) noexcept
    : configuration_(c), ledger_port_(ledger), radio_port_(radio),
      lifecycle_(lifecycle_configuration(c, &gate_), authority, roles, replay),
      next_attempt_id_(c.initial_attempt_id) {
    const auto cost = radio::calculate_lora_airtime(c.phy, radio::kMaximumFrameBytes);
    cost_us_ = cost.calculated() ? cost.airtime_us : 0;
    minimum_terminal_ms_ = cost_us_ / 1000 + (cost_us_ % 1000 != 0 ? 1U : 0U);
    const bool fixed_phy = c.phy.bandwidth_hz == 125000 && c.phy.preamble_symbols == 8 &&
        c.phy.spreading_factor == 7 && c.phy.coding_rate_denominator == 5 &&
        c.phy.explicit_header && c.phy.payload_crc && !c.phy.low_data_rate_optimization;
    configured_ = c.budget_domain != 0 && c.budget_epoch != 0 && c.budget_epoch != kMaximum &&
        c.radio_lineage_id != 0 && c.source_count > 0 && c.source_count <= kRepeaterDispatchSources &&
        c.per_source_credit_limit == 1 && c.global_credit_limit == 2 &&
        c.airtime_ceiling_us > 0 && fixed_phy && cost_us_ == 399616 &&
        c.lifecycle.admission_gate == nullptr &&
        c.lifecycle.policy.maximum_queue_depth > 0 && c.lifecycle.policy.maximum_queue_depth <= 8 &&
        c.lifecycle.maximum_frame_bytes > 0 && c.lifecycle.maximum_frame_bytes <= 255 &&
        c.logical_byte_cap >= c.lifecycle.policy.maximum_queue_depth * c.lifecycle.maximum_frame_bytes &&
        c.logical_byte_cap <= 8 * 255 && c.initial_attempt_id != 0 &&
        c.terminal_timeout_ms > minimum_terminal_ms_ && c.terminal_timeout_ms < kMaximum &&
        c.rearm_timeout_ms > 0 && c.rearm_timeout_ms < kMaximum &&
        lifecycle_.status().fault != RepeaterLifecycleFault::invalid_configuration;
    for (std::size_t i = 0; i < c.source_count && i < kRepeaterDispatchSources; ++i) {
        const auto& source = c.sources[i];
        configured_ = configured_ && source.group_context_id == c.lifecycle.group_context_id &&
            source.group_epoch == c.lifecycle.group_epoch && source.source_alias != 0 &&
            source.source_alias != c.lifecycle.local_alias;
        for (std::size_t j = 0; j < i; ++j) {
            configured_ = configured_ && !same_source(source, c.sources[j]);
        }
    }
    if (!configured_) { state_ = State::contained; fault_ = Fault::invalid_configuration; }
    else if (next_attempt_id_ == kMaximum) {
        state_ = State::contained; fault_ = Fault::identity_exhausted;
    }
}

void RepeaterDispatchController::count(std::uint32_t& counter) noexcept {
    increment_repeater_dispatch_counter(counter, diagnostics_.overflow);
}

void RepeaterDispatchController::invalidate_radio() noexcept {
    radio_reconciliation_required_ = true;
    radio_port_.invalidate_quiescence();
}

bool RepeaterDispatchController::valid_ledger(const RepeaterDispatchLedger& r) const noexcept {
    if (r.version != kRepeaterDispatchLedgerVersion || r.domain != configuration_.budget_domain ||
        r.budget_epoch != configuration_.budget_epoch || r.sequence == 0 || r.sequence == kMaximum ||
        r.global_remaining > configuration_.global_credit_limit ||
        r.airtime_remaining_us > configuration_.airtime_ceiling_us ||
        r.source_count != configuration_.source_count) { return false; }
    for (std::size_t i = 0; i < kRepeaterDispatchSources; ++i) {
        if (i < r.source_count) {
            if (!same_source(r.sources[i].source, configuration_.sources[i]) ||
                r.sources[i].remaining > configuration_.per_source_credit_limit) { return false; }
        } else if (!same_source(r.sources[i].source, {}) || r.sources[i].remaining != 0) { return false; }
    }
    return true;
}

bool RepeaterDispatchController::valid_radio(const RepeaterDispatchRadioProof& r) const noexcept {
    return r.lineage_id == configuration_.radio_lineage_id && r.lineage_id != 0 &&
        r.driver_generation != 0 && r.driver_generation != kMaximum &&
        r.readiness_generation != 0 && r.readiness_generation != kMaximum &&
        r.quiescence_receipt != 0 && r.quiescence_receipt != kMaximum &&
        r.verified_quiescent && r.receive_ready;
}

bool RepeaterDispatchController::load_ledger(bool recovery) noexcept {
    RepeaterDispatchLedger actual{};
    if (ledger_port_.read(actual) != RepeaterDispatchLedgerError::none) {
        fault_ = Fault::ledger_unavailable; ledger_known_ = false; return false;
    }
    if (!valid_ledger(actual)) {
        fault_ = Fault::ledger_mismatch; ledger_known_ = false; return false;
    }
    if (cached_once_) {
        bool safe = recovery ? (actual.sequence > ledger_.sequence || same_ledger(actual, ledger_))
                             : same_ledger(actual, ledger_);
        if (recovery) {
            safe = safe && actual.global_remaining <= ledger_.global_remaining &&
                actual.airtime_remaining_us <= ledger_.airtime_remaining_us;
            for (std::size_t i = 0; i < configuration_.source_count; ++i) {
                safe = safe && actual.sources[i].remaining <= ledger_.sources[i].remaining;
            }
        }
        if (!safe) { fault_ = Fault::ledger_mismatch; ledger_known_ = false; return false; }
    }
    ledger_ = actual;
    ledger_known_ = true;
    cached_once_ = true;
    return true;
}

bool RepeaterDispatchController::debit(std::size_t source_index, bool airtime) noexcept {
    if (!ledger_known_ || !load_ledger(false)) { return false; }
    if (ledger_.sequence >= kMaximum - 1) {
        fault_ = Fault::identity_exhausted; ledger_known_ = false; return false;
    }
    if (airtime ? ledger_.airtime_remaining_us < cost_us_
                : (source_index >= ledger_.source_count || ledger_.global_remaining == 0 ||
                   ledger_.sources[source_index].remaining == 0)) {
        fault_ = Fault::budget_exhausted; return false;
    }
    auto desired = ledger_;
    ++desired.sequence;
    if (airtime) { desired.airtime_remaining_us -= cost_us_; }
    else { --desired.global_remaining; --desired.sources[source_index].remaining; }
    const auto expected = ledger_;
    const auto committed = desired;
    if (ledger_port_.compare_exchange(expected, committed) != RepeaterDispatchLedgerError::none) {
        fault_ = Fault::ledger_uncertain; ledger_known_ = false; return false;
    }
    RepeaterDispatchLedger readback{};
    if (ledger_port_.read(readback) != RepeaterDispatchLedgerError::none ||
        !same_ledger(readback, committed) || !valid_ledger(readback)) {
        fault_ = Fault::ledger_uncertain; ledger_known_ = false; return false;
    }
    ledger_ = committed;
    count(diagnostics_.ledger_commits);
    return true;
}

void RepeaterDispatchController::stop(State state, Fault fault, std::uint64_t now) noexcept {
    if (possible_emission_ || attempt_live_ || fault == Fault::terminal_timeout || fault == Fault::rearm_timeout) {
        invalidate_radio();
    }
    const auto current = lifecycle_.status();
    if (current.state == RepeaterLifecycleState::active ||
        current.state == RepeaterLifecycleState::awaiting_consent) {
        const auto event = state == State::suspended ? RepeaterLifecycleEvent::suspend
                                                    : RepeaterLifecycleEvent::authority_changed;
        (void)lifecycle_.notify(current.generation, event, now);
    }
    state_ = state;
    fault_ = fault;
}

bool RepeaterDispatchController::observe_time(std::uint64_t now) noexcept {
    if (time_seen_ && now < last_now_ms_) {
        stop(State::contained, Fault::clock_regression, last_now_ms_); return false;
    }
    time_seen_ = true; last_now_ms_ = now; return true;
}

bool RepeaterDispatchController::prune(std::uint64_t now) noexcept {
    const auto result = lifecycle_.prune_expired(now);
    if (result.error != RepeaterLifecycleFault::none) {
        stop(State::contained, Fault::lifecycle, now); return false;
    }
    for (std::uint8_t i = 0; i < result.queued_dropped; ++i) { count(diagnostics_.expired); }
    if (result.pending_dropped) { count(diagnostics_.expired); }
    return true;
}

RepeaterDispatchFault RepeaterDispatchController::recover(std::uint64_t request, std::uint64_t now) noexcept {
    if (busy_) { return Fault::reentrant; }
    Guard guard(busy_);
    if (state_ == State::shutdown) { return Fault::wrong_state; }
    if (!configured_) { return Fault::invalid_configuration; }
    if (next_attempt_id_ == kMaximum) { return Fault::identity_exhausted; }
    if (state_ != State::disabled && state_ != State::contained && state_ != State::suspended) {
        return Fault::wrong_state;
    }
    if (!observe_time(now)) { return fault_; }
    RepeaterDispatchRadioProof proof{};
    if (!load_ledger(true)) { state_ = State::contained; return fault_; }
    if (!radio_port_.observe(proof) || !valid_radio(proof)) {
        state_ = State::contained; fault_ = Fault::radio_not_ready; return fault_;
    }
    if ((radio_reconciliation_required_ || possible_emission_ || attempt_live_) &&
        !(proof.driver_generation > claim_radio_.driver_generation ||
          (proof.driver_generation == claim_radio_.driver_generation &&
           proof.readiness_generation > claim_radio_.readiness_generation &&
           proof.quiescence_receipt > claim_radio_.quiescence_receipt))) {
        state_ = State::contained; fault_ = Fault::radio_not_ready; return fault_;
    }
    if (lifecycle_.recover(request, now) != RepeaterLifecycleFault::none) {
        state_ = State::contained; fault_ = Fault::lifecycle; return fault_;
    }
    RepeaterDispatchRadioProof verified{};
    if (!radio_port_.observe(verified) || !same_radio(verified, proof)) {
        stop(State::contained, Fault::radio_changed, now); return fault_;
    }
    claim_radio_ = verified;
    attempt_live_ = false; possible_emission_ = false; radio_reconciliation_required_ = false;
    state_ = State::awaiting_consent; fault_ = Fault::none;
    return Fault::none;
}

RepeaterDispatchFault RepeaterDispatchController::consent(std::uint64_t handle, std::uint64_t now) noexcept {
    if (busy_) { return Fault::reentrant; }
    Guard guard(busy_);
    if (state_ != State::awaiting_consent) { return Fault::wrong_state; }
    if (!observe_time(now)) { return fault_; }
    if (lifecycle_.consent(handle, now) != RepeaterLifecycleFault::none) {
        if (lifecycle_.status().state == RepeaterLifecycleState::contained ||
            lifecycle_.status().state == RepeaterLifecycleState::suspended) {
            state_ = State::contained;
        }
        fault_ = Fault::lifecycle; return fault_;
    }
    state_ = State::ready; fault_ = Fault::none; return Fault::none;
}

SingleRepeaterAdmission RepeaterDispatchController::Gate::evaluate(
    const VerifiedForwardingMetadata& metadata, std::size_t, std::uint64_t) noexcept {
    if (owner_.state_ != State::ready || owner_.attempt_live_) { return SingleRepeaterAdmission::denied; }
    std::size_t index = owner_.configuration_.source_count;
    for (std::size_t i = 0; i < owner_.configuration_.source_count; ++i) {
        const auto& source = owner_.configuration_.sources[i];
        if (source.group_context_id == metadata.group_context_id &&
            source.group_epoch == metadata.group_epoch && source.source_alias == metadata.source_alias) { index = i; }
    }
    if (index == owner_.configuration_.source_count) { return SingleRepeaterAdmission::denied; }
    if (owner_.debit(index, false)) { return SingleRepeaterAdmission::allow; }
    return owner_.fault_ == Fault::budget_exhausted ? SingleRepeaterAdmission::denied
                                                  : SingleRepeaterAdmission::uncertain;
}

RepeaterLifecycleAdmission RepeaterDispatchController::admit(
    std::uint64_t handle, radio::ByteView frame, std::uint64_t now) noexcept {
    if (busy_) { return {RepeaterLifecycleFault::reentrant}; }
    Guard guard(busy_);
    if (state_ == State::shutdown) { return {RepeaterLifecycleFault::wrong_state}; }
    if (!observe_time(now)) { return {RepeaterLifecycleFault::clock_regression}; }
    const auto result = lifecycle_.admit(handle, frame, now);
    if (result.forwarding.queued) { count(diagnostics_.admitted); }
    else { count(diagnostics_.denied); }
    if (lifecycle_.status().state == RepeaterLifecycleState::contained ||
        lifecycle_.status().state == RepeaterLifecycleState::suspended) {
        stop(State::contained, result.error == RepeaterLifecycleFault::admission_uncertain
            ? fault_ : Fault::lifecycle, now);
    }
    return result;
}

void RepeaterDispatchController::Claim::invoke(const RepeaterLifecycleDispatchTicket&,
                                              radio::ByteView bytes) noexcept {
    owner_.count(owner_.diagnostics_.claim_callbacks);
    owner_.count(owner_.diagnostics_.submit_requests);
    const auto attempt = owner_.attempt_;
    const auto expected_radio = owner_.claim_radio_;
    owner_.possible_emission_ = true;  // Submission is in progress until a definite result returns.
    owner_.submit_result_ = owner_.radio_port_.submit(attempt, expected_radio, bytes);
    const auto outcome = owner_.submit_result_.outcome;
    if (outcome == RepeaterDispatchSubmitOutcome::accepted_pending &&
        owner_.submit_result_.accepted_bytes == bytes.size) {
        owner_.count(owner_.diagnostics_.accepted_copies);
    }
    const bool definite_no_emission = owner_.submit_result_.accepted_bytes == 0 &&
        (outcome == RepeaterDispatchSubmitOutcome::tuple_refused ||
         outcome == RepeaterDispatchSubmitOutcome::busy || outcome == RepeaterDispatchSubmitOutcome::rejected);
    owner_.possible_emission_ = !definite_no_emission;
    if (outcome != RepeaterDispatchSubmitOutcome::tuple_refused ||
        owner_.submit_result_.accepted_bytes != 0) {
        owner_.invalidate_radio();
    }
}

RepeaterDispatchFault RepeaterDispatchController::service(std::uint64_t now) noexcept {
    if (busy_) { return Fault::reentrant; }
    Guard guard(busy_);
    if (state_ != State::ready && state_ != State::awaiting_terminal && state_ != State::awaiting_rearm) {
        return Fault::wrong_state;
    }
    if (!observe_time(now)) { return fault_; }
    if (!prune(now)) { return fault_; }
    if (state_ == State::awaiting_terminal || state_ == State::awaiting_rearm) {
        const auto interval = state_ == State::awaiting_terminal ? configuration_.terminal_timeout_ms
                                                               : configuration_.rearm_timeout_ms;
        if (now - phase_started_ms_ >= interval) {
            const auto fault = state_ == State::awaiting_terminal ? Fault::terminal_timeout : Fault::rearm_timeout;
            stop(State::contained, fault, now); return fault_;
        }
        return Fault::none;
    }
    RepeaterDispatchRadioProof original{};
    if (!radio_port_.observe(original) || !valid_radio(original)) {
        fault_ = Fault::radio_not_ready; return fault_;
    }
    if (next_attempt_id_ == kMaximum) {
        stop(State::contained, Fault::identity_exhausted, now); return fault_;
    }
    if (now > kMaximum - configuration_.terminal_timeout_ms ||
        now > kMaximum - configuration_.rearm_timeout_ms) {
        stop(State::contained, Fault::time_overflow, now); return fault_;
    }
    const auto candidate = lifecycle_.prepare_dispatch(now);
    if (candidate.error != RepeaterLifecycleFault::none) {
        stop(State::contained, Fault::lifecycle, now); return fault_;
    }
    if (!candidate.has_ticket) { return Fault::none; }
    if (!debit(0, true)) {
        stop(fault_ == Fault::budget_exhausted ? State::suspended : State::contained, fault_, now);
        return fault_;
    }
    attempt_ = {next_attempt_id_++, candidate.ticket, configuration_.budget_domain,
        configuration_.budget_epoch, ledger_.sequence, configuration_.radio_lineage_id,
        original.driver_generation, original.readiness_generation, original.quiescence_receipt};
    claim_radio_ = original;
    RepeaterDispatchRadioProof final{};
    if (!radio_port_.observe(final) || !valid_radio(final) || !same_radio(final, original)) {
        attempt_live_ = false;
        stop(State::contained, Fault::radio_changed, now); return fault_;
    }
    attempt_live_ = true;
    submit_result_ = {};
    const auto claimed = lifecycle_.claim_dispatch(candidate.ticket, now, claim_);
    if (!claimed.callback_invoked) {
        attempt_live_ = false;
        stop(State::suspended, Fault::lifecycle, now); return fault_;
    }
    if (claimed.error != RepeaterLifecycleFault::none) {
        stop(State::contained, Fault::lifecycle, now); return fault_;
    }
    const auto outcome = submit_result_.outcome;
    if (outcome == RepeaterDispatchSubmitOutcome::tuple_refused && submit_result_.accepted_bytes == 0) {
        attempt_live_ = false;
        stop(State::contained, Fault::radio_changed, now); return fault_;
    }
    if ((outcome == RepeaterDispatchSubmitOutcome::accepted_pending &&
         submit_result_.accepted_bytes != attempt_.ticket.frame_bytes) ||
        outcome == RepeaterDispatchSubmitOutcome::partial || outcome == RepeaterDispatchSubmitOutcome::uncertain) {
        count(diagnostics_.uncertain);
        stop(State::contained, Fault::uncertain_emission, now); return fault_;
    }
    if (outcome != RepeaterDispatchSubmitOutcome::accepted_pending && submit_result_.accepted_bytes != 0) {
        count(diagnostics_.uncertain);
        stop(State::contained, Fault::uncertain_emission, now); return fault_;
    }
    if (outcome != RepeaterDispatchSubmitOutcome::accepted_pending &&
        outcome != RepeaterDispatchSubmitOutcome::busy && outcome != RepeaterDispatchSubmitOutcome::rejected) {
        stop(State::contained, Fault::uncertain_emission, now); return fault_;
    }
    state_ = outcome == RepeaterDispatchSubmitOutcome::accepted_pending ? State::awaiting_terminal
                                                                     : State::awaiting_rearm;
    phase_started_ms_ = now; claim_started_ms_ = now; fault_ = Fault::none;
    return Fault::none;
}

RepeaterDispatchFault RepeaterDispatchController::terminal(std::uint64_t handle, std::uint64_t now) noexcept {
    if (busy_) { return Fault::reentrant; }
    Guard guard(busy_);
    if (state_ != State::awaiting_terminal) { return Fault::wrong_state; }
    RepeaterDispatchTerminal evidence{};
    const auto expected = attempt_;
    if (!radio_port_.consume_terminal(handle, expected, evidence) ||
        !same_attempt(evidence.attempt, attempt_)) { return Fault::invalid_event; }
    if (!observe_time(now) || !prune(now)) { return fault_; }
    if (now - phase_started_ms_ >= configuration_.terminal_timeout_ms) {
        stop(State::contained, Fault::terminal_timeout, now); return fault_;
    }
    if (evidence.outcome != RepeaterDispatchTerminalOutcome::completed ||
        now - claim_started_ms_ < minimum_terminal_ms_) {
        stop(State::contained, Fault::uncertain_emission, now); return fault_;
    }
    if (now > kMaximum - configuration_.rearm_timeout_ms) {
        stop(State::contained, Fault::time_overflow, now); return fault_;
    }
    count(diagnostics_.terminals);
    state_ = State::awaiting_rearm; phase_started_ms_ = now;
    return Fault::none;
}

RepeaterDispatchFault RepeaterDispatchController::rearm(std::uint64_t handle, std::uint64_t now) noexcept {
    if (busy_) { return Fault::reentrant; }
    Guard guard(busy_);
    if (state_ != State::awaiting_rearm) { return Fault::wrong_state; }
    RepeaterDispatchRearm evidence{};
    const auto expected = attempt_;
    if (!radio_port_.consume_rearm(handle, expected, evidence) ||
        !same_attempt(evidence.attempt, attempt_)) { return Fault::invalid_event; }
    if (!observe_time(now) || !prune(now)) { return fault_; }
    if (now - phase_started_ms_ >= configuration_.rearm_timeout_ms) {
        stop(State::contained, Fault::rearm_timeout, now); return fault_;
    }
    RepeaterDispatchRadioProof actual{};
    if (!valid_radio(evidence.proof) || evidence.proof.driver_generation != attempt_.driver_generation ||
        evidence.proof.readiness_generation <= attempt_.readiness_generation ||
        evidence.proof.quiescence_receipt <= attempt_.quiescence_receipt ||
        !radio_port_.observe(actual) || !same_radio(actual, evidence.proof)) {
        stop(State::contained, Fault::radio_changed, now); return fault_;
    }
    claim_radio_ = actual;
    count(diagnostics_.rearms); attempt_live_ = false; possible_emission_ = false;
    radio_reconciliation_required_ = false;
    state_ = State::ready; fault_ = Fault::none; return Fault::none;
}

RepeaterDispatchFault RepeaterDispatchController::notify(std::uint64_t generation,
    RepeaterLifecycleEvent event, std::uint64_t now) noexcept {
    if (busy_) { return Fault::reentrant; }
    Guard guard(busy_);
    if (state_ == State::shutdown) { return Fault::wrong_state; }
    if (generation != lifecycle_.status().generation) { return Fault::invalid_event; }
    if (!observe_time(now)) { return fault_; }
    const auto result = lifecycle_.notify(generation, event, now);
    if (result == RepeaterLifecycleFault::wrong_state || result == RepeaterLifecycleFault::invalid_request) {
        return Fault::wrong_state;
    }
    if (event == RepeaterLifecycleEvent::uncertain_submission) { possible_emission_ = true; }
    if (possible_emission_ || attempt_live_ || event == RepeaterLifecycleEvent::radio_unavailable ||
        event == RepeaterLifecycleEvent::rearm_failed) { invalidate_radio(); }
    state_ = lifecycle_.status().state == RepeaterLifecycleState::suspended ? State::suspended : State::contained;
    fault_ = Fault::suspended; return fault_;
}

RepeaterDispatchFault RepeaterDispatchController::disable(std::uint64_t request, std::uint64_t now) noexcept {
    if (busy_) { return Fault::reentrant; }
    Guard guard(busy_);
    if (state_ == State::shutdown) { return Fault::wrong_state; }
    if (!observe_time(now)) { return fault_; }
    const auto result = lifecycle_.disable(request, now);
    if (result != RepeaterLifecycleFault::none) {
        if (lifecycle_.status().state == RepeaterLifecycleState::contained) { state_ = State::contained; }
        fault_ = Fault::lifecycle; return fault_;
    }
    state_ = State::disabled; fault_ = Fault::none; return Fault::none;
}

RepeaterDispatchFault RepeaterDispatchController::shutdown() noexcept {
    if (busy_) { return Fault::reentrant; }
    Guard guard(busy_);
    if (state_ == State::shutdown) { return Fault::none; }
    if (possible_emission_ || attempt_live_) { invalidate_radio(); }
    (void)lifecycle_.shutdown(); state_ = State::shutdown; fault_ = Fault::none;
    return Fault::none;
}

RepeaterDispatchFault RepeaterDispatchController::clear_diagnostics() noexcept {
    if (busy_) { return Fault::reentrant; }
    Guard guard(busy_);
    if (state_ == State::shutdown) { return Fault::wrong_state; }
    diagnostics_ = {}; return Fault::none;
}

RepeaterDispatchStatus RepeaterDispatchController::status() const noexcept {
    const auto p2 = lifecycle_.status();
    // During the synchronous claim, A's pending bytes and this attempt are the
    // SAME logical candidate. Count it once even while both fixed buffers exist.
    const auto occupied = p2.occupied_frames + (attempt_live_ && !p2.pending ? 1U : 0U);
    return {state_, fault_, p2, occupied, occupied * configuration_.lifecycle.maximum_frame_bytes,
            possible_emission_, diagnostics_};
}
}  // namespace opentrail::delivery
