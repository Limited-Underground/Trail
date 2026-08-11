#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_baseline.hpp"
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

MapActivationGuard clean_mapless_guard() {
    MapActivationGuard guard{};
    EXPECT(guard.start(
               policy(), {MapSelectorState::missing, {}}) ==
           MapActivationError::none);
    return guard;
}

MapActivationGuard active_guard(
    MapSlot slot = MapSlot::slot_a,
    std::uint64_t generation = 10) {
    MapActivationGuard guard{};
    EXPECT(guard.start(
               policy(),
               {MapSelectorState::valid, package(slot, generation)}) ==
           MapActivationError::none);
    return guard;
}

MapActivationGuard unreadable_mapless_guard() {
    MapActivationGuard guard{};
    EXPECT(guard.start(
               policy(), {MapSelectorState::unreadable, {}}) ==
           MapActivationError::none);
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

MapSelectorBaselineContext context(
    std::uint64_t trusted_minimum_generation = 0) {
    return {policy(), trusted_minimum_generation};
}

void test_empty_first_baseline_is_committed_before_exposure() {
    auto live = clean_mapless_guard();
    FakeStorage storage{};
    MapSelectorStore store{storage};
    MapSelectorBaselineCoordinator coordinator{store};

    const auto result = coordinator.establish(
        live, context(), package());
    EXPECT(result.committed());
    EXPECT(result.state == MapSelectorBaselineState::committed);
    EXPECT(result.reason == MapSelectorBaselineReason::none);
    EXPECT(result.save.generation == 1);
    EXPECT(result.active_record_generation == 1);
    EXPECT(result.map_exposure_allowed);
    EXPECT(!result.live_guard_mapless);
    EXPECT(storage.write_calls == 1);
    const auto status = live.status();
    EXPECT(status.state == MapActivationState::active);
    EXPECT(status.active_slot == MapSlot::slot_a);
    EXPECT(status.active_generation == 10);
    EXPECT(status.previous_slot == MapSlot::none);
    EXPECT(status.trial_boots == 0);
    EXPECT(store.inspect().generation == 1);
}

void test_committed_baseline_restores_as_stable_after_restart() {
    auto live = clean_mapless_guard();
    FakeStorage storage{};
    MapSelectorStore store{storage};
    MapSelectorBaselineCoordinator coordinator{store};
    EXPECT(coordinator.establish(live, context(), package()).committed());

    MapActivationGuard restored{};
    const auto loaded = store.restore_at_or_above(
        restored, policy(), package(), {}, 100, 1);
    EXPECT(loaded.restored);
    EXPECT(loaded.generation == 1);
    EXPECT(restored.status().state == MapActivationState::active);
    EXPECT(restored.status().active_slot == MapSlot::slot_a);
    EXPECT(restored.status().previous_slot == MapSlot::none);
    EXPECT(restored.status().map_available);
}

void test_invalid_candidate_preserves_clean_mapless_state() {
    auto live = clean_mapless_guard();
    FakeStorage storage{};
    MapSelectorStore store{storage};
    MapSelectorBaselineCoordinator coordinator{store};
    auto invalid = package();
    invalid.integrity_verified = false;

    const auto result = coordinator.establish(
        live, context(), invalid);
    EXPECT(result.state == MapSelectorBaselineState::rejected);
    EXPECT(result.reason == MapSelectorBaselineReason::candidate_rejected);
    EXPECT(result.candidate_error ==
           MapActivationError::verification_required);
    EXPECT(storage.read_calls == 0);
    EXPECT(storage.write_calls == 0);
    EXPECT(live.status().state == MapActivationState::mapless);
    EXPECT(live.status().reason == MapActivationReason::no_selector);
    EXPECT(result.live_guard_mapless);
}

void test_only_clean_no_selector_guard_can_establish_baseline() {
    auto active = active_guard();
    auto unreadable = unreadable_mapless_guard();
    MapActivationGuard stopped{};
    MapActivationGuard* guards[] = {&active, &unreadable, &stopped};

    for (auto* guard : guards) {
        FakeStorage storage{};
        MapSelectorStore store{storage};
        MapSelectorBaselineCoordinator coordinator{store};
        const auto result = coordinator.establish(
            *guard, context(), package());
        EXPECT(result.state == MapSelectorBaselineState::rejected);
        if (guard == &stopped) {
            EXPECT(result.reason ==
                   MapSelectorBaselineReason::live_guard_not_running);
        } else {
            EXPECT(result.reason ==
                   MapSelectorBaselineReason::
                       clean_mapless_guard_required);
        }
        EXPECT(storage.read_calls == 0);
        EXPECT(storage.write_calls == 0);
    }
    EXPECT(active.status().state == MapActivationState::active);
}

void test_policy_mismatch_and_trusted_history_block_first_use() {
    auto policy_live = clean_mapless_guard();
    FakeStorage policy_storage{};
    MapSelectorStore policy_store{policy_storage};
    MapSelectorBaselineCoordinator policy_coordinator{policy_store};
    auto wrong = context();
    wrong.policy.maximum_trial_boots = 2;
    const auto mismatch = policy_coordinator.establish(
        policy_live, wrong, package());
    EXPECT(mismatch.reason == MapSelectorBaselineReason::invalid_policy);
    EXPECT(mismatch.state == MapSelectorBaselineState::service_required);
    EXPECT(!policy_live.status().running);
    EXPECT(policy_storage.write_calls == 0);

    auto history_live = clean_mapless_guard();
    FakeStorage history_storage{};
    MapSelectorStore history_store{history_storage};
    MapSelectorBaselineCoordinator history{history_store};
    const auto blocked = history.establish(
        history_live, context(1), package());
    EXPECT(blocked.reason ==
           MapSelectorBaselineReason::trusted_history_present);
    EXPECT(blocked.reconciliation_required);
    EXPECT(blocked.state ==
           MapSelectorBaselineState::reconciliation_required);
    EXPECT(history_storage.read_calls == 0);
    EXPECT(history_storage.write_calls == 0);
    EXPECT(history_live.status().reason ==
           MapActivationReason::selector_ambiguous);
}

void test_existing_selector_is_never_overwritten_as_first_use() {
    auto live = clean_mapless_guard();
    FakeStorage storage{};
    storage.seed(0, encode_guard(active_guard(), 1));
    MapSelectorStore store{storage};
    MapSelectorBaselineCoordinator coordinator{store};

    const auto result = coordinator.establish(
        live, context(), package());
    EXPECT(result.reason == MapSelectorBaselineReason::selector_not_empty);
    EXPECT(result.reconciliation_required);
    EXPECT(result.state ==
           MapSelectorBaselineState::reconciliation_required);
    EXPECT(storage.write_calls == 0);
    EXPECT(store.inspect().generation == 1);
    EXPECT(live.status().reason == MapActivationReason::selector_ambiguous);
}

void test_dirty_unreadable_and_conflicted_media_fail_closed() {
    auto dirty_live = clean_mapless_guard();
    FakeStorage dirty_storage{};
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> invalid{};
    invalid.fill(0xAAU);
    dirty_storage.seed(0, invalid);
    MapSelectorStore dirty_store{dirty_storage};
    MapSelectorBaselineCoordinator dirty{dirty_store};
    const auto dirty_result = dirty.establish(
        dirty_live, context(), package());
    EXPECT(dirty_result.reason == MapSelectorBaselineReason::selector_invalid);
    EXPECT(dirty_result.state == MapSelectorBaselineState::service_required);
    EXPECT(dirty_storage.write_calls == 0);
    EXPECT(dirty_live.status().reason ==
           MapActivationReason::selector_unreadable);

    auto unavailable_live = clean_mapless_guard();
    FakeStorage unavailable_storage{};
    unavailable_storage.fail_read_slot = 1;
    MapSelectorStore unavailable_store{unavailable_storage};
    MapSelectorBaselineCoordinator unavailable{unavailable_store};
    const auto unavailable_result = unavailable.establish(
        unavailable_live, context(), package());
    EXPECT(unavailable_result.reason ==
           MapSelectorBaselineReason::storage_unavailable);
    EXPECT(unavailable_result.state ==
           MapSelectorBaselineState::service_required);
    EXPECT(unavailable_storage.write_calls == 0);

    auto conflict_live = clean_mapless_guard();
    FakeStorage conflict_storage{};
    conflict_storage.seed(0, encode_guard(active_guard(), 1));
    conflict_storage.seed(
        1, encode_guard(active_guard(MapSlot::slot_b, 11), 1));
    MapSelectorStore conflict_store{conflict_storage};
    MapSelectorBaselineCoordinator conflict{conflict_store};
    const auto conflict_result = conflict.establish(
        conflict_live, context(), package());
    EXPECT(conflict_result.reason ==
           MapSelectorBaselineReason::generation_conflict);
    EXPECT(conflict_result.reconciliation_required);
    EXPECT(conflict_storage.write_calls == 0);
    EXPECT(conflict_live.status().reason ==
           MapActivationReason::selector_ambiguous);
}

void test_prepared_write_failure_never_exposes_baseline() {
    auto live = clean_mapless_guard();
    FakeStorage storage{};
    storage.fail_write_slot = 0;
    MapSelectorStore store{storage};
    MapSelectorBaselineCoordinator coordinator{store};

    const auto result = coordinator.establish(
        live, context(), package());
    EXPECT(result.reason ==
           MapSelectorBaselineReason::checkpoint_save_failed);
    EXPECT(result.state == MapSelectorBaselineState::service_required);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(result.live_guard_mapless);
    EXPECT(storage.write_calls == 1);
    EXPECT(live.status().state == MapActivationState::mapless);
}

void test_uncertain_commit_and_bad_readback_require_reconciliation() {
    auto uncertain_live = clean_mapless_guard();
    FakeStorage uncertain_storage{};
    uncertain_storage.fail_commit_slot = 0;
    uncertain_storage.commit_then_fail = true;
    MapSelectorStore uncertain_store{uncertain_storage};
    MapSelectorBaselineCoordinator uncertain{uncertain_store};
    const auto commit_failed = uncertain.establish(
        uncertain_live, context(), package());
    EXPECT(commit_failed.reason ==
           MapSelectorBaselineReason::checkpoint_commit_uncertain);
    EXPECT(commit_failed.reconciliation_required);
    EXPECT(commit_failed.state ==
           MapSelectorBaselineState::reconciliation_required);
    EXPECT(uncertain_live.status().reason ==
           MapActivationReason::selector_ambiguous);

    auto corrupt_live = clean_mapless_guard();
    FakeStorage corrupt_storage{};
    corrupt_storage.corrupt_after_commit = true;
    MapSelectorStore corrupt_store{corrupt_storage};
    MapSelectorBaselineCoordinator corrupt{corrupt_store};
    const auto bad_readback = corrupt.establish(
        corrupt_live, context(), package());
    EXPECT(bad_readback.reason ==
           MapSelectorBaselineReason::checkpoint_verification_failed);
    EXPECT(bad_readback.reconciliation_required);
    EXPECT(bad_readback.state ==
           MapSelectorBaselineState::reconciliation_required);
    EXPECT(corrupt_live.status().state == MapActivationState::mapless);
}

void test_selector_appearing_between_inspect_and_save_is_not_overwritten() {
    auto live = clean_mapless_guard();
    FakeStorage storage{};
    storage.inject_on_read_call = 3;
    storage.inject_slot = 1;
    storage.inject_bytes = encode_guard(active_guard(), 1);
    MapSelectorStore store{storage};
    MapSelectorBaselineCoordinator coordinator{store};

    const auto result = coordinator.establish(
        live, context(), package());
    EXPECT(result.reason == MapSelectorBaselineReason::selector_changed);
    EXPECT(result.save.error == MapSelectorStoreError::state_mismatch);
    EXPECT(result.reconciliation_required);
    EXPECT(result.state ==
           MapSelectorBaselineState::reconciliation_required);
    EXPECT(storage.write_calls == 0);
    EXPECT(store.inspect().generation == 1);
    EXPECT(live.status().state == MapActivationState::mapless);
    EXPECT(live.status().reason == MapActivationReason::selector_ambiguous);
}

}  // namespace

int main() {
    test_empty_first_baseline_is_committed_before_exposure();
    test_committed_baseline_restores_as_stable_after_restart();
    test_invalid_candidate_preserves_clean_mapless_state();
    test_only_clean_no_selector_guard_can_establish_baseline();
    test_policy_mismatch_and_trusted_history_block_first_use();
    test_existing_selector_is_never_overwritten_as_first_use();
    test_dirty_unreadable_and_conflicted_media_fail_closed();
    test_prepared_write_failure_never_exposes_baseline();
    test_uncertain_commit_and_bad_readback_require_reconciliation();
    test_selector_appearing_between_inspect_and_save_is_not_overwritten();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector baseline assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 map selector baseline scenario groups\n";
    return EXIT_SUCCESS;
}
