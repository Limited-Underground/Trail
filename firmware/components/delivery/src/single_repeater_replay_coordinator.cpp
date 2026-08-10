#include "opentrail/single_repeater_replay_coordinator.hpp"

#include <limits>

namespace opentrail::delivery {
namespace {

bool checkpoint_matches_epoch(
    const DuplicateCheckpoint& checkpoint,
    std::uint32_t expected_epoch) {
    for (std::size_t index = 0; index < checkpoint.count; ++index) {
        if (checkpoint.entries[index].key.group_epoch != expected_epoch) {
            return false;
        }
    }
    return true;
}

}  // namespace

SingleRepeaterReplayCoordinator::SingleRepeaterReplayCoordinator(
    std::uint32_t duplicate_retention_ms,
    std::uint64_t expected_group_context_id,
    std::uint32_t expected_group_epoch,
    DuplicateWindow& live_window,
    DuplicateCheckpointStore& store,
    SingleRepeaterForwarder& forwarder)
    : duplicate_retention_ms_(duplicate_retention_ms),
      expected_group_context_id_(expected_group_context_id),
      expected_group_epoch_(expected_group_epoch),
      live_window_(live_window),
      store_(store),
      forwarder_(forwarder) {}

void SingleRepeaterReplayCoordinator::fail_persistence() {
    operational_ = false;
    if (persistence_failures_ != std::numeric_limits<std::uint32_t>::max()) {
        ++persistence_failures_;
    }
}

RepeaterReplayBootResult SingleRepeaterReplayCoordinator::boot(
    const ReplayStorageBindingEvidence& binding,
    std::uint64_t now_ms) {
    if (boot_attempted_) {
        return {RepeaterReplayError::already_booted};
    }
    boot_attempted_ = true;
    if (duplicate_retention_ms_ == 0 || expected_group_context_id_ == 0 ||
        expected_group_epoch_ == 0 ||
        store_.group_context_id() != expected_group_context_id_ ||
        store_.group_epoch() != expected_group_epoch_ ||
        binding.group_context_id != expected_group_context_id_ ||
        binding.group_epoch != expected_group_epoch_ ||
        !binding.protected_namespace_verified) {
        return {RepeaterReplayError::invalid_binding};
    }
    if (live_window_.status(now_ms).entries != 0) {
        return {RepeaterReplayError::live_window_not_empty};
    }

    DuplicateWindow candidate{duplicate_retention_ms_};
    const auto restored = store_.restore(candidate, now_ms);
    if (restored.error == DuplicateCheckpointStoreError::no_checkpoint) {
        if (!binding.empty_store_provisioning_authorized) {
            return {
                RepeaterReplayError::missing_checkpoint,
                RepeaterReplayBootOutcome::service_required,
                restored.error};
        }
        const auto initialized = store_.save(candidate, now_ms);
        if (!initialized.saved()) {
            fail_persistence();
            return {
                RepeaterReplayError::store_failure,
                RepeaterReplayBootOutcome::service_required,
                initialized.error};
        }
        persisted_generation_ = initialized.generation;
        operational_ = true;
        return {
            RepeaterReplayError::none,
            RepeaterReplayBootOutcome::first_boot_initialized,
            DuplicateCheckpointStoreError::none,
            initialized.generation};
    }
    if (restored.error == DuplicateCheckpointStoreError::legacy_unbound) {
        return {
            RepeaterReplayError::legacy_checkpoint,
            RepeaterReplayBootOutcome::service_required,
            restored.error,
            restored.generation};
    }
    if (restored.error == DuplicateCheckpointStoreError::binding_mismatch) {
        return {
            RepeaterReplayError::checkpoint_binding_mismatch,
            RepeaterReplayBootOutcome::service_required,
            restored.error,
            restored.generation};
    }
    if (restored.error != DuplicateCheckpointStoreError::none) {
        return {
            RepeaterReplayError::store_failure,
            RepeaterReplayBootOutcome::service_required,
            restored.error,
            restored.generation};
    }

    const auto checkpoint = candidate.checkpoint(now_ms);
    if (!checkpoint_matches_epoch(checkpoint, expected_group_epoch_)) {
        return {
            RepeaterReplayError::checkpoint_epoch_mismatch,
            RepeaterReplayBootOutcome::service_required,
            DuplicateCheckpointStoreError::checkpoint_rejected,
            restored.generation};
    }
    if (live_window_.restore(checkpoint, now_ms) != DuplicateError::none) {
        return {
            RepeaterReplayError::live_restore_failure,
            RepeaterReplayBootOutcome::service_required,
            DuplicateCheckpointStoreError::checkpoint_rejected,
            restored.generation};
    }

    persisted_generation_ = restored.generation;
    auto outcome = RepeaterReplayBootOutcome::restored;
    if (restored.recovery_required) {
        const auto repair = store_.save(candidate, now_ms);
        if (!repair.saved()) {
            fail_persistence();
            return {
                RepeaterReplayError::store_failure,
                RepeaterReplayBootOutcome::service_required,
                repair.error,
                restored.generation};
        }
        persisted_generation_ = repair.generation;
        outcome = RepeaterReplayBootOutcome::restored_and_repaired;
    }
    operational_ = true;
    return {
        RepeaterReplayError::none,
        outcome,
        DuplicateCheckpointStoreError::none,
        persisted_generation_};
}

RepeaterReplayProcessResult
SingleRepeaterReplayCoordinator::process_and_persist(
    const VerifiedForwardingMetadata& metadata,
    radio::ByteView exact_protected_frame,
    std::uint64_t now_ms) {
    if (!operational_) {
        return {};
    }
    RepeaterReplayProcessResult result{};
    result.error = RepeaterReplayError::none;
    result.generation = persisted_generation_;
    result.forwarding = forwarder_.process(
        metadata,
        exact_protected_frame,
        now_ms);
    if (!result.forwarding.replay_state_changed) {
        return result;
    }

    const auto saved = store_.save(live_window_, now_ms);
    result.store_error = saved.error;
    if (!saved.saved()) {
        fail_persistence();
        result.error = RepeaterReplayError::persistence_failure;
        return result;
    }
    persisted_generation_ = saved.generation;
    result.generation = saved.generation;
    result.replay_state_persisted = true;
    return result;
}

RepeaterReplayTransmitResult SingleRepeaterReplayCoordinator::next_transmit(
    std::uint64_t now_ms) {
    if (!operational_) {
        return {};
    }
    return {
        RepeaterReplayError::none,
        forwarder_.next_forward(now_ms)};
}

RepeaterReplayStatus SingleRepeaterReplayCoordinator::status() const {
    return {
        boot_attempted_,
        operational_,
        persisted_generation_,
        persistence_failures_};
}

}  // namespace opentrail::delivery
