#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_candidate.hpp"
#include "opentrail/map_selector_checkpoint.hpp"
#include "opentrail/map_selector_store.hpp"

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
        ++read_calls;
        if (inject_on_read_call != 0 &&
            read_calls == inject_on_read_call) {
            slots[inject_slot] = inject_bytes;
            present[inject_slot] = true;
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
        ++write_calls;
        present[slot] = true;
        std::copy(data, data + size, slots[slot].begin());
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
    bool commit_then_fail{false};
    bool corrupt_after_commit{false};
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t inject_on_read_call{0};
    std::uint8_t inject_slot{1};
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> inject_bytes{};
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

MapPackageEvidence candidate() {
    return package(MapSlot::slot_b, 11);
}

MapActivationGuard active_guard() {
    MapActivationGuard guard{};
    EXPECT(guard.start(
               policy(), {MapSelectorState::valid, package()}) ==
           MapActivationError::none);
    return guard;
}

MapActivationGuard mapless_guard() {
    MapActivationGuard guard{};
    EXPECT(guard.start(
               policy(), {MapSelectorState::missing, {}}) ==
           MapActivationError::none);
    return guard;
}

MapActivationGuard staged_guard() {
    auto guard = active_guard();
    EXPECT(guard.stage(candidate()) == MapActivationError::none);
    return guard;
}

MapActivationGuard trial_guard() {
    auto guard = staged_guard();
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

void seed_current(
    FakeStorage& storage,
    const MapActivationGuard& guard,
    std::uint64_t generation = 1) {
    storage.seed(0, encode_guard(guard, generation));
}

MapSelectorCandidateContext context(
    std::uint64_t current_generation = 1,
    std::uint64_t floor = 1) {
    return {policy(), current_generation, floor};
}

void test_valid_replacement_is_durable_before_publish() {
    auto live = active_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    MapSelectorStore store{storage};
    MapSelectorCandidateCoordinator coordinator{store};

    const auto result = coordinator.activate(
        live, context(), candidate(), 100);
    EXPECT(result.committed());
    EXPECT(result.state == MapSelectorCandidateState::committed);
    EXPECT(result.reason == MapSelectorCandidateReason::none);
    EXPECT(result.prior_record_generation == 1);
    EXPECT(result.active_record_generation == 2);
    EXPECT(result.save.generation == 2);
    EXPECT(result.map_exposure_allowed);
    EXPECT(!result.live_guard_mapless);
    EXPECT(storage.write_calls == 1);
    const auto status = live.status();
    EXPECT(status.state == MapActivationState::trial);
    EXPECT(status.active_slot == MapSlot::slot_b);
    EXPECT(status.active_generation == 11);
    EXPECT(status.previous_slot == MapSlot::slot_a);
    EXPECT(status.previous_generation == 10);
    EXPECT(store.inspect().generation == 2);
}

void test_invalid_candidate_preserves_stable_map() {
    auto live = active_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    MapSelectorStore store{storage};
    MapSelectorCandidateCoordinator coordinator{store};
    auto invalid = candidate();
    invalid.integrity_verified = false;

    const auto result = coordinator.activate(
        live, context(), invalid, 100);
    EXPECT(result.state == MapSelectorCandidateState::rejected);
    EXPECT(result.reason ==
           MapSelectorCandidateReason::candidate_rejected);
    EXPECT(result.stage_error ==
           MapActivationError::verification_required);
    EXPECT(storage.write_calls == 0);
    EXPECT(live.status().state == MapActivationState::active);
    EXPECT(live.status().active_slot == MapSlot::slot_a);
    EXPECT(live.status().map_available);
}

void test_same_slot_or_generation_is_rejected() {
    auto same_slot_live = active_guard();
    FakeStorage same_slot_storage{};
    seed_current(same_slot_storage, same_slot_live);
    MapSelectorStore same_slot_store{same_slot_storage};
    MapSelectorCandidateCoordinator same_slot{same_slot_store};
    const auto wrong_slot = same_slot.activate(
        same_slot_live, context(), package(MapSlot::slot_a, 11), 100);
    EXPECT(wrong_slot.reason ==
           MapSelectorCandidateReason::candidate_rejected);
    EXPECT(wrong_slot.stage_error == MapActivationError::invalid_package);
    EXPECT(same_slot_storage.write_calls == 0);
    EXPECT(same_slot_live.status().state == MapActivationState::active);

    auto same_generation_live = active_guard();
    FakeStorage same_generation_storage{};
    seed_current(same_generation_storage, same_generation_live);
    MapSelectorStore same_generation_store{same_generation_storage};
    MapSelectorCandidateCoordinator same_generation{same_generation_store};
    const auto wrong_generation = same_generation.activate(
        same_generation_live,
        context(),
        package(MapSlot::slot_b, 10),
        100);
    EXPECT(wrong_generation.reason ==
           MapSelectorCandidateReason::candidate_rejected);
    EXPECT(same_generation_storage.write_calls == 0);
    EXPECT(same_generation_live.status().active_slot == MapSlot::slot_a);
}

void test_nonactive_baselines_are_rejected_without_storage_mutation() {
    auto mapless = mapless_guard();
    auto staged = staged_guard();
    auto trial = trial_guard();
    auto fallback = fallback_guard();
    MapActivationGuard stopped{};

    MapActivationGuard* guards[] = {
        &mapless, &staged, &trial, &fallback, &stopped};
    for (auto* guard : guards) {
        FakeStorage storage{};
        MapSelectorStore store{storage};
        MapSelectorCandidateCoordinator coordinator{store};
        const auto result = coordinator.activate(
            *guard, context(), candidate(), 100);
        EXPECT(result.state == MapSelectorCandidateState::rejected);
        if (guard == &stopped) {
            EXPECT(result.reason ==
                   MapSelectorCandidateReason::live_guard_not_running);
        } else {
            EXPECT(result.reason ==
                   MapSelectorCandidateReason::stable_baseline_required);
        }
        EXPECT(storage.write_calls == 0);
    }
}

void test_live_checkpoint_token_and_floor_mismatches_fail_mapless() {
    auto mismatch_live = active_guard();
    FakeStorage mismatch_storage{};
    seed_current(mismatch_storage, trial_guard());
    MapSelectorStore mismatch_store{mismatch_storage};
    MapSelectorCandidateCoordinator mismatch{mismatch_store};
    const auto different = mismatch.activate(
        mismatch_live, context(), candidate(), 100);
    EXPECT(different.reason ==
           MapSelectorCandidateReason::live_checkpoint_mismatch);
    EXPECT(different.reconciliation_required);
    EXPECT(mismatch_live.status().state == MapActivationState::mapless);

    auto token_live = active_guard();
    FakeStorage token_storage{};
    seed_current(token_storage, token_live);
    MapSelectorStore token_store{token_storage};
    MapSelectorCandidateCoordinator token{token_store};
    const auto wrong_token = token.activate(
        token_live, context(2, 1), candidate(), 100);
    EXPECT(wrong_token.reason ==
           MapSelectorCandidateReason::current_generation_mismatch);
    EXPECT(wrong_token.reconciliation_required);
    EXPECT(token_live.status().state == MapActivationState::mapless);

    auto floor_live = active_guard();
    FakeStorage floor_storage{};
    seed_current(floor_storage, floor_live);
    MapSelectorStore floor_store{floor_storage};
    MapSelectorCandidateCoordinator floor{floor_store};
    const auto stale = floor.activate(
        floor_live, context(1, 2), candidate(), 100);
    EXPECT(stale.reason == MapSelectorCandidateReason::rollback_detected);
    EXPECT(stale.reconciliation_required);
    EXPECT(floor_live.status().state == MapActivationState::mapless);

    auto zero_live = active_guard();
    FakeStorage zero_storage{};
    seed_current(zero_storage, zero_live);
    MapSelectorStore zero_store{zero_storage};
    MapSelectorCandidateCoordinator zero{zero_store};
    const auto missing_token = zero.activate(
        zero_live, context(0, 0), candidate(), 100);
    EXPECT(missing_token.reason ==
           MapSelectorCandidateReason::current_generation_mismatch);
    EXPECT(missing_token.reconciliation_required);
    EXPECT(zero_live.status().state == MapActivationState::mapless);
}

void test_unreadable_and_conflicted_storage_fail_mapless() {
    auto unavailable_live = active_guard();
    FakeStorage unavailable_storage{};
    seed_current(unavailable_storage, unavailable_live);
    unavailable_storage.fail_read_slot = 1;
    MapSelectorStore unavailable_store{unavailable_storage};
    MapSelectorCandidateCoordinator unavailable{unavailable_store};
    const auto failed = unavailable.activate(
        unavailable_live, context(), candidate(), 100);
    EXPECT(failed.reason ==
           MapSelectorCandidateReason::storage_unavailable);
    EXPECT(failed.state == MapSelectorCandidateState::service_required);
    EXPECT(unavailable_live.status().state == MapActivationState::mapless);

    auto conflict_live = active_guard();
    FakeStorage conflict_storage{};
    conflict_storage.seed(0, encode_guard(conflict_live, 1));
    conflict_storage.seed(1, encode_guard(trial_guard(), 1));
    MapSelectorStore conflict_store{conflict_storage};
    MapSelectorCandidateCoordinator conflict{conflict_store};
    const auto ambiguous = conflict.activate(
        conflict_live, context(), candidate(), 100);
    EXPECT(ambiguous.reason ==
           MapSelectorCandidateReason::generation_conflict);
    EXPECT(ambiguous.state ==
           MapSelectorCandidateState::reconciliation_required);
    EXPECT(ambiguous.reconciliation_required);
    EXPECT(conflict_live.status().reason ==
           MapActivationReason::selector_ambiguous);
}

void test_prepared_write_failure_never_publishes_candidate() {
    auto live = active_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    storage.fail_write_slot = 1;
    MapSelectorStore store{storage};
    MapSelectorCandidateCoordinator coordinator{store};

    const auto result = coordinator.activate(
        live, context(), candidate(), 100);
    EXPECT(result.state == MapSelectorCandidateState::service_required);
    EXPECT(result.reason ==
           MapSelectorCandidateReason::checkpoint_save_failed);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(result.live_guard_mapless);
    EXPECT(live.status().state == MapActivationState::mapless);
    EXPECT(live.status().active_slot == MapSlot::none);
}

void test_commit_uncertainty_and_bad_readback_require_reconciliation() {
    auto uncertain_live = active_guard();
    FakeStorage uncertain_storage{};
    seed_current(uncertain_storage, uncertain_live);
    uncertain_storage.fail_commit_slot = 1;
    uncertain_storage.commit_then_fail = true;
    MapSelectorStore uncertain_store{uncertain_storage};
    MapSelectorCandidateCoordinator uncertain{uncertain_store};
    const auto commit_failed = uncertain.activate(
        uncertain_live, context(), candidate(), 100);
    EXPECT(commit_failed.reason ==
           MapSelectorCandidateReason::checkpoint_commit_uncertain);
    EXPECT(commit_failed.reconciliation_required);
    EXPECT(commit_failed.state ==
           MapSelectorCandidateState::reconciliation_required);
    EXPECT(uncertain_live.status().state == MapActivationState::mapless);

    auto corrupt_live = active_guard();
    FakeStorage corrupt_storage{};
    seed_current(corrupt_storage, corrupt_live);
    corrupt_storage.corrupt_after_commit = true;
    MapSelectorStore corrupt_store{corrupt_storage};
    MapSelectorCandidateCoordinator corrupt{corrupt_store};
    const auto bad_readback = corrupt.activate(
        corrupt_live, context(), candidate(), 100);
    EXPECT(bad_readback.reason ==
           MapSelectorCandidateReason::checkpoint_verification_failed);
    EXPECT(bad_readback.reconciliation_required);
    EXPECT(bad_readback.state ==
           MapSelectorCandidateState::reconciliation_required);
    EXPECT(corrupt_live.status().state == MapActivationState::mapless);
}

void test_newer_checkpoint_between_verify_and_save_is_not_overwritten() {
    auto live = active_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    storage.inject_on_read_call = 3;
    storage.inject_slot = 1;
    storage.inject_bytes = encode_guard(live, 2);
    MapSelectorStore store{storage};
    MapSelectorCandidateCoordinator coordinator{store};

    const auto result = coordinator.activate(
        live, context(), candidate(), 100);
    EXPECT(result.reason ==
           MapSelectorCandidateReason::live_checkpoint_mismatch);
    EXPECT(result.save.error == MapSelectorStoreError::state_mismatch);
    EXPECT(result.reconciliation_required);
    EXPECT(result.state ==
           MapSelectorCandidateState::reconciliation_required);
    EXPECT(storage.write_calls == 0);
    EXPECT(store.inspect().generation == 2);
    EXPECT(live.status().state == MapActivationState::mapless);
    EXPECT(!result.map_exposure_allowed);
}

void test_invalid_policy_fails_mapless() {
    auto live = active_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    MapSelectorStore store{storage};
    MapSelectorCandidateCoordinator coordinator{store};
    auto invalid = context();
    invalid.policy.maximum_trial_boots = 2;

    const auto result = coordinator.activate(
        live, invalid, candidate(), 100);
    EXPECT(result.reason == MapSelectorCandidateReason::invalid_policy);
    EXPECT(result.state == MapSelectorCandidateState::service_required);
    EXPECT(result.live_guard_mapless);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(storage.write_calls == 0);
}

void test_generation_exhaustion_never_publishes_candidate() {
    auto live = active_guard();
    FakeStorage storage{};
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    seed_current(storage, live, maximum);
    MapSelectorStore store{storage};
    MapSelectorCandidateCoordinator coordinator{store};

    const auto result = coordinator.activate(
        live, context(maximum, maximum), candidate(), 100);
    EXPECT(result.reason ==
           MapSelectorCandidateReason::generation_exhausted);
    EXPECT(result.state == MapSelectorCandidateState::service_required);
    EXPECT(storage.write_calls == 0);
    EXPECT(live.status().state == MapActivationState::mapless);
    EXPECT(!result.map_exposure_allowed);
}

}  // namespace

int main() {
    test_valid_replacement_is_durable_before_publish();
    test_invalid_candidate_preserves_stable_map();
    test_same_slot_or_generation_is_rejected();
    test_nonactive_baselines_are_rejected_without_storage_mutation();
    test_live_checkpoint_token_and_floor_mismatches_fail_mapless();
    test_unreadable_and_conflicted_storage_fail_mapless();
    test_prepared_write_failure_never_publishes_candidate();
    test_commit_uncertainty_and_bad_readback_require_reconciliation();
    test_newer_checkpoint_between_verify_and_save_is_not_overwritten();
    test_invalid_policy_fails_mapless();
    test_generation_exhaustion_never_publishes_candidate();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector candidate assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 11 map selector candidate scenario groups\n";
    return EXIT_SUCCESS;
}
