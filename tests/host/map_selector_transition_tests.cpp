#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_checkpoint.hpp"
#include "opentrail/map_selector_store.hpp"
#include "opentrail/map_selector_transition.hpp"

namespace {

using namespace opentrail::maps;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

class FakeStorage final : public MapSelectorStorage {
public:
    MapSelectorStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override {
        if (slot >= slots.size() || output == nullptr ||
            size != kMapSelectorCheckpointBytes) {
            return MapSelectorStorageError::invalid_argument;
        }
        if (fail_read_slot == static_cast<int>(slot)) {
            return MapSelectorStorageError::io_failure;
        }
        if (!present[slot]) {
            return MapSelectorStorageError::not_found;
        }
        std::copy(slots[slot].begin(), slots[slot].end(), output);
        return MapSelectorStorageError::none;
    }

    MapSelectorStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) override {
        if (slot >= slots.size() || data == nullptr ||
            size != kMapSelectorCheckpointBytes) {
            return MapSelectorStorageError::invalid_argument;
        }
        present[slot] = true;
        std::copy(data, data + size, slots[slot].begin());
        ++write_calls;
        if (fail_write_slot == static_cast<int>(slot)) {
            return MapSelectorStorageError::io_failure;
        }
        return MapSelectorStorageError::none;
    }

    MapSelectorStorageError commit_slot(
        std::uint8_t slot,
        std::size_t offset,
        std::uint8_t value) override {
        if (slot >= slots.size() || !present[slot] ||
            offset != kMapSelectorCommitOffset ||
            value != kMapSelectorCommitMarker) {
            return MapSelectorStorageError::invalid_argument;
        }
        if (fail_commit_slot == static_cast<int>(slot)) {
            if (commit_then_fail) {
                slots[slot][offset] = value;
            }
            return MapSelectorStorageError::io_failure;
        }
        slots[slot][offset] = value;
        if (corrupt_after_commit) {
            slots[slot][20] ^= 0x01U;
            corrupt_after_commit = false;
        }
        return MapSelectorStorageError::none;
    }

    MapSelectorStorageError erase_slot(std::uint8_t slot) override {
        if (slot >= slots.size()) {
            return MapSelectorStorageError::invalid_argument;
        }
        if (fail_erase_slot == static_cast<int>(slot)) {
            return MapSelectorStorageError::io_failure;
        }
        present[slot] = false;
        slots[slot].fill(0);
        return MapSelectorStorageError::none;
    }

    void seed(
        std::uint8_t slot,
        const std::array<std::uint8_t, kMapSelectorCheckpointBytes>& bytes) {
        slots[slot] = bytes;
        present[slot] = true;
    }

    std::array<std::array<std::uint8_t, kMapSelectorCheckpointBytes>, 2>
        slots{};
    std::array<bool, 2> present{};
    int fail_read_slot{-1};
    int fail_write_slot{-1};
    int fail_commit_slot{-1};
    int fail_erase_slot{-1};
    bool commit_then_fail{false};
    bool corrupt_after_commit{false};
    std::uint32_t write_calls{0};
};

MapActivationPolicy policy() {
    return {8U * 1024U * 1024U, 500, 3, 3};
}

MapPackageEvidence package(
    MapSlot slot = MapSlot::slot_a,
    std::uint64_t generation = 10) {
    return {
        slot,
        generation,
        1024U * 1024U,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true};
}

MapPackageEvidence candidate_package() {
    return package(MapSlot::slot_b, 11);
}

MapActivationGuard active_guard() {
    MapActivationGuard guard{};
    EXPECT(guard.start(
               policy(), {MapSelectorState::valid, package()}) ==
           MapActivationError::none);
    return guard;
}

MapActivationGuard trial_guard() {
    auto guard = active_guard();
    EXPECT(guard.stage(candidate_package()) == MapActivationError::none);
    EXPECT(guard.mark_selector_committed(
               MapSlot::slot_b, 11, 100) ==
           MapActivationError::none);
    return guard;
}

MapActivationGuard fallback_guard() {
    auto guard = trial_guard();
    EXPECT(guard.report_trial_read(false, 110) ==
           MapActivationError::trial_health_failed);
    return guard;
}

MapActivationGuard active_with_previous() {
    auto guard = trial_guard();
    EXPECT(guard.report_trial_read(true, 110) == MapActivationError::none);
    EXPECT(guard.report_trial_read(true, 120) == MapActivationError::none);
    EXPECT(guard.report_trial_read(true, 130) == MapActivationError::none);
    EXPECT(guard.status().state == MapActivationState::active);
    EXPECT(guard.status().previous_cleanup_permitted);
    return guard;
}

std::array<std::uint8_t, kMapSelectorCheckpointBytes> encode_guard(
    const MapActivationGuard& guard,
    std::uint64_t generation) {
    MapSelectorCheckpoint checkpoint{};
    EXPECT(guard.export_checkpoint(generation, checkpoint) ==
           MapActivationError::none);
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> bytes{};
    EXPECT(encode_map_selector_checkpoint(
               checkpoint, bytes.data(), bytes.size()).succeeded());
    return bytes;
}

MapSelectorTransitionContext context(
    std::uint64_t current_generation = 1,
    std::uint64_t floor = 1) {
    return {policy(), current_generation, floor};
}

void seed_current(
    FakeStorage& storage,
    const MapActivationGuard& guard,
    std::uint64_t generation = 1) {
    storage.seed(0, encode_guard(guard, generation));
}

void test_partial_trial_health_is_volatile() {
    auto live = trial_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    MapSelectorStore store{storage};
    MapSelectorTransitionCoordinator coordinator{store};

    const auto result = coordinator.report_trial_read(
        live, context(), true, 110);
    EXPECT(result.state ==
           MapSelectorTransitionState::applied_volatile);
    EXPECT(!result.persistence_required);
    EXPECT(result.active_generation == 1);
    EXPECT(result.map_exposure_allowed);
    EXPECT(live.status().healthy_trial_reads == 1);
    EXPECT(storage.write_calls == 0);
}

void test_trial_promotion_is_committed_before_release() {
    auto live = trial_guard();
    EXPECT(live.report_trial_read(true, 110) == MapActivationError::none);
    EXPECT(live.report_trial_read(true, 120) == MapActivationError::none);
    FakeStorage storage{};
    seed_current(storage, live);
    MapSelectorStore store{storage};
    MapSelectorTransitionCoordinator coordinator{store};

    const auto result = coordinator.report_trial_read(
        live, context(), true, 130);
    EXPECT(result.committed());
    EXPECT(result.persistence_required);
    EXPECT(result.active_generation == 2);
    EXPECT(result.map_exposure_allowed);
    EXPECT(live.status().state == MapActivationState::active);
    EXPECT(live.status().previous_cleanup_permitted);
    EXPECT(storage.write_calls == 1);
}

void test_trial_read_failure_is_committed_as_fallback() {
    auto live = trial_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    MapSelectorStore store{storage};
    MapSelectorTransitionCoordinator coordinator{store};

    const auto result = coordinator.report_trial_read(
        live, context(), false, 110);
    EXPECT(result.committed());
    EXPECT(result.guard_error == MapActivationError::trial_health_failed);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(live.status().state == MapActivationState::fallback_required);
    EXPECT(live.status().reason == MapActivationReason::trial_read_failed);
}

void test_deadline_and_clock_failure_are_durable() {
    auto deadline_live = trial_guard();
    FakeStorage deadline_storage{};
    seed_current(deadline_storage, deadline_live);
    MapSelectorStore deadline_store{deadline_storage};
    MapSelectorTransitionCoordinator deadline{deadline_store};
    const auto expired = deadline.tick(
        deadline_live, context(), 600);
    EXPECT(expired.committed());
    EXPECT(expired.guard_error ==
           MapActivationError::trial_deadline_reached);
    EXPECT(deadline_live.status().state ==
           MapActivationState::fallback_required);

    auto clock_live = trial_guard();
    FakeStorage clock_storage{};
    seed_current(clock_storage, clock_live);
    MapSelectorStore clock_store{clock_storage};
    MapSelectorTransitionCoordinator clock{clock_store};
    const auto regressed = clock.tick(clock_live, context(), 99);
    EXPECT(regressed.committed());
    EXPECT(regressed.guard_error == MapActivationError::clock_regression);
    EXPECT(clock_live.status().reason ==
           MapActivationReason::clock_regression);
}

void test_valid_fallback_completion_is_committed() {
    auto live = fallback_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    MapSelectorStore store{storage};
    MapSelectorTransitionCoordinator coordinator{store};

    const auto result = coordinator.complete_fallback(
        live, context(), package());
    EXPECT(result.committed());
    EXPECT(result.active_generation == 2);
    EXPECT(result.map_exposure_allowed);
    EXPECT(live.status().state == MapActivationState::active);
    EXPECT(live.status().active_slot == MapSlot::slot_a);
    EXPECT(live.status().previous_slot == MapSlot::none);
}

void test_invalid_fallback_clears_checkpoint_and_stays_mapless() {
    auto live = fallback_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    MapSelectorStore store{storage};
    MapSelectorTransitionCoordinator coordinator{store};
    auto wrong = package();
    wrong.generation = 99;

    const auto result = coordinator.complete_fallback(
        live, context(), wrong);
    EXPECT(result.state ==
           MapSelectorTransitionState::mapless_committed);
    EXPECT(result.guard_error == MapActivationError::fallback_unavailable);
    EXPECT(result.checkpoint_cleared);
    EXPECT(result.active_generation == 0);
    EXPECT(result.live_guard_mapless);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(live.status().reason == MapActivationReason::fallback_unavailable);
    EXPECT(store.inspect().error == MapSelectorStoreError::no_checkpoint);
}

void test_previous_cleanup_is_committed() {
    auto live = active_with_previous();
    FakeStorage storage{};
    seed_current(storage, live);
    MapSelectorStore store{storage};
    MapSelectorTransitionCoordinator coordinator{store};

    const auto result = coordinator.mark_previous_removed(
        live, context(), MapSlot::slot_a, 10);
    EXPECT(result.committed());
    EXPECT(result.active_generation == 2);
    EXPECT(live.status().previous_slot == MapSlot::none);
    EXPECT(!live.status().previous_cleanup_permitted);
}

void test_rejected_operation_does_not_write() {
    auto live = active_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    MapSelectorStore store{storage};
    MapSelectorTransitionCoordinator coordinator{store};

    const auto result = coordinator.mark_previous_removed(
        live, context(), MapSlot::slot_a, 10);
    EXPECT(result.state == MapSelectorTransitionState::rejected);
    EXPECT(result.reason == MapSelectorTransitionReason::guard_rejected);
    EXPECT(result.guard_error == MapActivationError::invalid_state);
    EXPECT(!result.persistence_required);
    EXPECT(result.map_exposure_allowed);
    EXPECT(live.status().state == MapActivationState::active);
    EXPECT(storage.write_calls == 0);
}

void test_live_state_generation_and_floor_mismatch_are_mapless() {
    FakeStorage mismatch_storage{};
    const auto persisted = active_guard();
    seed_current(mismatch_storage, persisted);
    auto different_live = trial_guard();
    MapSelectorStore mismatch_store{mismatch_storage};
    MapSelectorTransitionCoordinator mismatch{mismatch_store};
    const auto different = mismatch.tick(
        different_live, context(), 110);
    EXPECT(different.reason ==
           MapSelectorTransitionReason::live_checkpoint_mismatch);
    EXPECT(different.reconciliation_required);
    EXPECT(different.state ==
           MapSelectorTransitionState::reconciliation_required);
    EXPECT(different_live.status().state == MapActivationState::mapless);

    FakeStorage token_storage{};
    auto token_live = active_guard();
    seed_current(token_storage, token_live);
    MapSelectorStore token_store{token_storage};
    MapSelectorTransitionCoordinator token{token_store};
    const auto wrong_token = token.mark_previous_removed(
        token_live, context(2, 1), MapSlot::slot_a, 10);
    EXPECT(wrong_token.reason ==
           MapSelectorTransitionReason::current_generation_mismatch);
    EXPECT(wrong_token.state ==
           MapSelectorTransitionState::reconciliation_required);
    EXPECT(token_live.status().state == MapActivationState::mapless);

    FakeStorage floor_storage{};
    auto floor_live = active_guard();
    seed_current(floor_storage, floor_live);
    MapSelectorStore floor_store{floor_storage};
    MapSelectorTransitionCoordinator floor{floor_store};
    const auto stale = floor.mark_previous_removed(
        floor_live, context(1, 2), MapSlot::slot_a, 10);
    EXPECT(stale.reason == MapSelectorTransitionReason::rollback_detected);
    EXPECT(stale.reconciliation_required);
    EXPECT(stale.state ==
           MapSelectorTransitionState::reconciliation_required);
    EXPECT(floor_live.status().state == MapActivationState::mapless);

    FakeStorage zero_storage{};
    auto zero_live = active_guard();
    seed_current(zero_storage, zero_live);
    MapSelectorStore zero_store{zero_storage};
    MapSelectorTransitionCoordinator zero{zero_store};
    const auto zero_token = zero.mark_previous_removed(
        zero_live, context(0, 0), MapSlot::slot_a, 10);
    EXPECT(zero_token.reason ==
           MapSelectorTransitionReason::current_generation_mismatch);
    EXPECT(zero_token.reconciliation_required);
    EXPECT(zero_live.status().state == MapActivationState::mapless);
}

void test_unreadable_and_conflicted_storage_are_mapless() {
    auto unreadable_live = active_guard();
    FakeStorage unreadable_storage{};
    seed_current(unreadable_storage, unreadable_live);
    unreadable_storage.fail_read_slot = 1;
    MapSelectorStore unreadable_store{unreadable_storage};
    MapSelectorTransitionCoordinator unreadable{unreadable_store};
    const auto unavailable = unreadable.mark_previous_removed(
        unreadable_live, context(), MapSlot::slot_a, 10);
    EXPECT(unavailable.reason ==
           MapSelectorTransitionReason::storage_unavailable);
    EXPECT(unreadable_live.status().state == MapActivationState::mapless);

    auto conflict_live = active_guard();
    FakeStorage conflict_storage{};
    conflict_storage.seed(0, encode_guard(conflict_live, 1));
    conflict_storage.seed(1, encode_guard(fallback_guard(), 1));
    MapSelectorStore conflict_store{conflict_storage};
    MapSelectorTransitionCoordinator conflict{conflict_store};
    const auto ambiguous = conflict.mark_previous_removed(
        conflict_live, context(), MapSlot::slot_a, 10);
    EXPECT(ambiguous.reason ==
           MapSelectorTransitionReason::generation_conflict);
    EXPECT(ambiguous.reconciliation_required);
    EXPECT(conflict_live.status().reason ==
           MapActivationReason::selector_ambiguous);
}

void test_save_failures_never_publish_attempted_fallback() {
    auto write_live = trial_guard();
    FakeStorage write_storage{};
    seed_current(write_storage, write_live);
    write_storage.fail_write_slot = 1;
    MapSelectorStore write_store{write_storage};
    MapSelectorTransitionCoordinator write{write_store};
    const auto failed = write.report_trial_read(
        write_live, context(), false, 110);
    EXPECT(failed.state == MapSelectorTransitionState::service_required);
    EXPECT(failed.reason ==
           MapSelectorTransitionReason::checkpoint_save_failed);
    EXPECT(write_live.status().state == MapActivationState::mapless);

    auto commit_live = trial_guard();
    FakeStorage commit_storage{};
    seed_current(commit_storage, commit_live);
    commit_storage.fail_commit_slot = 1;
    commit_storage.commit_then_fail = true;
    MapSelectorStore commit_store{commit_storage};
    MapSelectorTransitionCoordinator commit{commit_store};
    const auto uncertain = commit.report_trial_read(
        commit_live, context(), false, 110);
    EXPECT(uncertain.state ==
           MapSelectorTransitionState::reconciliation_required);
    EXPECT(uncertain.reason ==
           MapSelectorTransitionReason::checkpoint_commit_uncertain);
    EXPECT(commit_live.status().state == MapActivationState::mapless);

    auto corrupt_live = trial_guard();
    FakeStorage corrupt_storage{};
    seed_current(corrupt_storage, corrupt_live);
    corrupt_storage.corrupt_after_commit = true;
    MapSelectorStore corrupt_store{corrupt_storage};
    MapSelectorTransitionCoordinator corrupt{corrupt_store};
    const auto bad = corrupt.report_trial_read(
        corrupt_live, context(), false, 110);
    EXPECT(bad.reason ==
           MapSelectorTransitionReason::checkpoint_verification_failed);
    EXPECT(bad.reconciliation_required);
    EXPECT(corrupt_live.status().state == MapActivationState::mapless);
}

void test_failed_checkpoint_clear_remains_mapless_and_uncertain() {
    auto live = fallback_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    storage.fail_erase_slot = 0;
    MapSelectorStore store{storage};
    MapSelectorTransitionCoordinator coordinator{store};
    auto wrong = package();
    wrong.generation = 99;

    const auto result = coordinator.complete_fallback(
        live, context(), wrong);
    EXPECT(result.state ==
           MapSelectorTransitionState::reconciliation_required);
    EXPECT(result.reason ==
           MapSelectorTransitionReason::checkpoint_clear_failed);
    EXPECT(result.reconciliation_required);
    EXPECT(!result.checkpoint_cleared);
    EXPECT(result.live_guard_mapless);
    EXPECT(live.status().state == MapActivationState::mapless);
}

void test_invalid_policy_and_stopped_live_are_rejected_safely() {
    auto invalid_live = active_guard();
    FakeStorage invalid_storage{};
    seed_current(invalid_storage, invalid_live);
    MapSelectorStore invalid_store{invalid_storage};
    MapSelectorTransitionCoordinator invalid{invalid_store};
    auto invalid_context = context();
    invalid_context.policy.maximum_trial_boots = 2;
    const auto policy_result = invalid.mark_previous_removed(
        invalid_live, invalid_context, MapSlot::slot_a, 10);
    EXPECT(policy_result.reason ==
           MapSelectorTransitionReason::invalid_policy);
    EXPECT(invalid_live.status().state == MapActivationState::mapless);

    MapActivationGuard stopped{};
    FakeStorage stopped_storage{};
    MapSelectorStore stopped_store{stopped_storage};
    MapSelectorTransitionCoordinator stopped_coordinator{stopped_store};
    const auto stopped_result = stopped_coordinator.tick(
        stopped, context(), 1);
    EXPECT(stopped_result.state == MapSelectorTransitionState::rejected);
    EXPECT(stopped_result.reason ==
           MapSelectorTransitionReason::live_guard_not_running);
    EXPECT(!stopped.status().running);
}

}  // namespace

int main() {
    test_partial_trial_health_is_volatile();
    test_trial_promotion_is_committed_before_release();
    test_trial_read_failure_is_committed_as_fallback();
    test_deadline_and_clock_failure_are_durable();
    test_valid_fallback_completion_is_committed();
    test_invalid_fallback_clears_checkpoint_and_stays_mapless();
    test_previous_cleanup_is_committed();
    test_rejected_operation_does_not_write();
    test_live_state_generation_and_floor_mismatch_are_mapless();
    test_unreadable_and_conflicted_storage_are_mapless();
    test_save_failures_never_publish_attempted_fallback();
    test_failed_checkpoint_clear_remains_mapless_and_uncertain();
    test_invalid_policy_and_stopped_live_are_rejected_safely();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector transition assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 13 map selector transition scenario groups\n";
    return EXIT_SUCCESS;
}
