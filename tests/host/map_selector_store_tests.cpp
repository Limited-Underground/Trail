#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/map_activation_guard.hpp"
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

class FakeMapSelectorStorage final : public MapSelectorStorage {
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
        ++write_calls;
        if (partial_write_bytes < size) {
            slots[slot].fill(0);
            std::copy(
                data, data + partial_write_bytes, slots[slot].begin());
            partial_write_bytes = size;
            return MapSelectorStorageError::io_failure;
        }
        std::copy(data, data + size, slots[slot].begin());
        if (corrupt_after_write) {
            slots[slot][20] ^= 0x01U;
            corrupt_after_write = false;
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
        ++commit_calls;
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
    int fail_commit_slot{-1};
    int fail_erase_slot{-1};
    std::size_t partial_write_bytes{kMapSelectorCheckpointBytes};
    bool corrupt_after_write{false};
    bool corrupt_after_commit{false};
    bool commit_then_fail{false};
    std::uint32_t write_calls{0};
    std::uint32_t commit_calls{0};
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

MapActivationGuard trial_guard() {
    auto guard = active_guard();
    EXPECT(guard.stage(candidate()) == MapActivationError::none);
    EXPECT(guard.mark_selector_committed(MapSlot::slot_b, 11, 100) ==
           MapActivationError::none);
    return guard;
}

std::array<std::uint8_t, kMapSelectorCheckpointBytes> encoded_checkpoint(
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

void test_empty_first_save_and_restore() {
    FakeMapSelectorStorage storage{};
    MapSelectorStore store{storage};
    auto guard = active_guard();
    MapActivationGuard empty{};
    const auto absent = store.restore(empty, policy(), package(), {}, 100);
    EXPECT(absent.error == MapSelectorStoreError::no_checkpoint);
    EXPECT(!absent.restored);

    const auto saved = store.save(guard);
    EXPECT(saved.saved());
    EXPECT(saved.generation == 1);
    EXPECT(saved.written_slot == MapSelectorSource::slot_a);
    EXPECT(!saved.commit_uncertain);
    EXPECT(storage.write_calls == 1);
    EXPECT(storage.commit_calls == 1);
    EXPECT(storage.slots[0][kMapSelectorCommitOffset] ==
           kMapSelectorCommitMarker);

    MapActivationGuard restored{};
    const auto loaded = store.restore(
        restored, policy(), package(), {}, 200);
    EXPECT(loaded.restored);
    EXPECT(loaded.generation == 1);
    EXPECT(loaded.source == MapSelectorSource::slot_a);
    EXPECT(loaded.recovery_required);
    EXPECT(restored.status().state == MapActivationState::active);
}

void test_rotation_selects_newest_unique_generation() {
    FakeMapSelectorStorage storage{};
    MapSelectorStore store{storage};
    const auto guard = active_guard();
    EXPECT(store.save(guard).generation == 1);
    const auto second = store.save(guard);
    EXPECT(second.saved());
    EXPECT(second.generation == 2);
    EXPECT(second.written_slot == MapSelectorSource::slot_b);

    MapActivationGuard restored{};
    const auto loaded = store.restore(
        restored, policy(), package(), {}, 200);
    EXPECT(loaded.restored);
    EXPECT(loaded.generation == 2);
    EXPECT(loaded.source == MapSelectorSource::slot_b);
    EXPECT(!loaded.recovery_required);

    const auto third = store.save(guard);
    EXPECT(third.generation == 3);
    EXPECT(third.written_slot == MapSelectorSource::slot_a);
}

void test_partial_prepared_write_preserves_previous() {
    FakeMapSelectorStorage storage{};
    MapSelectorStore store{storage};
    const auto guard = active_guard();
    EXPECT(store.save(guard).saved());
    storage.partial_write_bytes = 20;
    const auto interrupted = store.save(guard);
    EXPECT(interrupted.error == MapSelectorStoreError::storage_failure);
    EXPECT(!interrupted.commit_uncertain);
    EXPECT(interrupted.generation == 2);

    MapActivationGuard restored{};
    const auto loaded = store.restore(
        restored, policy(), package(), {}, 200);
    EXPECT(loaded.restored);
    EXPECT(loaded.generation == 1);
    EXPECT(loaded.slot_b == MapSelectorSlotState::uncommitted);
    EXPECT(loaded.recovery_required);
}

void test_commit_failure_is_uncertain_but_never_guessed() {
    FakeMapSelectorStorage storage{};
    MapSelectorStore store{storage};
    const auto guard = active_guard();
    EXPECT(store.save(guard).saved());
    storage.fail_commit_slot = 1;
    const auto failed = store.save(guard);
    EXPECT(failed.error == MapSelectorStoreError::storage_failure);
    EXPECT(failed.commit_uncertain);

    MapActivationGuard restored{};
    const auto old = store.restore(restored, policy(), package(), {}, 200);
    EXPECT(old.restored);
    EXPECT(old.generation == 1);
    EXPECT(old.slot_b == MapSelectorSlotState::uncommitted);

    FakeMapSelectorStorage committed_storage{};
    MapSelectorStore committed_store{committed_storage};
    EXPECT(committed_store.save(guard).saved());
    committed_storage.fail_commit_slot = 1;
    committed_storage.commit_then_fail = true;
    const auto uncertain = committed_store.save(guard);
    EXPECT(uncertain.commit_uncertain);
    MapActivationGuard selected_new{};
    const auto newer = committed_store.restore(
        selected_new, policy(), package(), {}, 200);
    EXPECT(newer.restored);
    EXPECT(newer.generation == 2);
    EXPECT(newer.source == MapSelectorSource::slot_b);
}

void test_corrupt_readback_preserves_previous() {
    FakeMapSelectorStorage storage{};
    MapSelectorStore store{storage};
    const auto guard = active_guard();
    EXPECT(store.save(guard).saved());
    storage.corrupt_after_commit = true;
    const auto failed = store.save(guard);
    EXPECT(failed.error == MapSelectorStoreError::verification_failure);
    EXPECT(failed.commit_uncertain);

    MapActivationGuard restored{};
    const auto loaded = store.restore(
        restored, policy(), package(), {}, 200);
    EXPECT(loaded.restored);
    EXPECT(loaded.generation == 1);
    EXPECT(loaded.slot_b == MapSelectorSlotState::invalid);
}

void test_degraded_peer_is_repaired_without_overwriting_good_slot() {
    FakeMapSelectorStorage storage{};
    MapSelectorStore store{storage};
    const auto guard = active_guard();
    EXPECT(store.save(guard).saved());
    auto prepared = encoded_checkpoint(guard, 2);
    prepared[kMapSelectorCommitOffset] = 0;
    storage.seed(1, prepared);

    const auto repaired = store.save(guard);
    EXPECT(repaired.saved());
    EXPECT(repaired.repaired_peer);
    EXPECT(repaired.generation == 2);
    EXPECT(repaired.written_slot == MapSelectorSource::slot_b);
    EXPECT(storage.slots[0] == encoded_checkpoint(guard, 1));

    MapActivationGuard restored{};
    const auto loaded = store.restore(
        restored, policy(), package(), {}, 200);
    EXPECT(loaded.restored);
    EXPECT(!loaded.recovery_required);
}

void test_unreadable_slot_fails_closed() {
    FakeMapSelectorStorage storage{};
    MapSelectorStore store{storage};
    const auto guard = active_guard();
    EXPECT(store.save(guard).saved());
    storage.fail_read_slot = 1;

    MapActivationGuard restored{};
    const auto loaded = store.restore(
        restored, policy(), package(), {}, 200);
    EXPECT(loaded.error == MapSelectorStoreError::storage_failure);
    EXPECT(loaded.recovery_required);
    EXPECT(restored.status().state == MapActivationState::stopped);
    EXPECT(store.save(guard).error == MapSelectorStoreError::storage_failure);
}

void test_equal_generation_conflict_fails_closed() {
    FakeMapSelectorStorage storage{};
    MapSelectorStore store{storage};
    const auto active = active_guard();
    const auto trial = trial_guard();
    storage.seed(0, encoded_checkpoint(active, 5));
    storage.seed(1, encoded_checkpoint(trial, 5));

    MapActivationGuard restored{};
    const auto loaded = store.restore(
        restored, policy(), package(), {}, 200);
    EXPECT(loaded.error == MapSelectorStoreError::generation_conflict);
    EXPECT(loaded.recovery_required);
    EXPECT(restored.status().state == MapActivationState::stopped);
    EXPECT(store.save(active).error ==
           MapSelectorStoreError::generation_conflict);
}

void test_policy_and_package_mismatch_fail_mapless() {
    FakeMapSelectorStorage storage{};
    MapSelectorStore store{storage};
    const auto guard = active_guard();
    EXPECT(store.save(guard).saved());

    auto changed = policy();
    changed.trial_deadline_ms += 1;
    MapActivationGuard policy_rejected{};
    const auto policy_result = store.restore(
        policy_rejected, changed, package(), {}, 200);
    EXPECT(policy_result.error == MapSelectorStoreError::checkpoint_rejected);
    EXPECT(policy_result.guard_error == MapActivationError::checkpoint_mismatch);
    EXPECT(policy_rejected.status().state == MapActivationState::mapless);

    auto wrong = package();
    wrong.generation = 99;
    MapActivationGuard package_rejected{};
    const auto package_result = store.restore(
        package_rejected, policy(), wrong, {}, 200);
    EXPECT(package_result.error == MapSelectorStoreError::checkpoint_rejected);
    EXPECT(package_result.guard_error ==
           MapActivationError::verification_required);
    EXPECT(package_rejected.status().state == MapActivationState::mapless);
}

void test_generation_floor_and_exhaustion() {
    FakeMapSelectorStorage storage{};
    MapSelectorStore store{storage};
    const auto guard = active_guard();
    storage.seed(0, encoded_checkpoint(guard, 5));
    MapActivationGuard below{};
    const auto rejected = store.restore_at_or_above(
        below, policy(), package(), {}, 200, 6);
    EXPECT(rejected.error == MapSelectorStoreError::generation_below_floor);
    EXPECT(below.status().state == MapActivationState::stopped);

    const auto advanced = store.save_next_after(guard, 7);
    EXPECT(advanced.saved());
    EXPECT(advanced.generation == 8);

    FakeMapSelectorStorage exhausted_storage{};
    MapSelectorStore exhausted_store{exhausted_storage};
    exhausted_storage.seed(
        0,
        encoded_checkpoint(
            guard, std::numeric_limits<std::uint64_t>::max()));
    EXPECT(exhausted_store.save(guard).error ==
           MapSelectorStoreError::generation_exhausted);
}

void test_current_guard_must_exactly_match_newest_checkpoint() {
    FakeMapSelectorStorage storage{};
    MapSelectorStore store{storage};
    const auto active = active_guard();
    storage.seed(0, encoded_checkpoint(active, 5));

    const auto exact = store.verify_current(active, 5);
    EXPECT(exact.error == MapSelectorStoreError::none);
    EXPECT(exact.exact_match);
    EXPECT(exact.generation == 5);
    EXPECT(exact.recovery_required);

    const auto different = trial_guard();
    const auto mismatch = store.verify_current(different, 5);
    EXPECT(mismatch.error == MapSelectorStoreError::state_mismatch);
    EXPECT(!mismatch.exact_match);
    EXPECT(mismatch.generation == 5);

    const auto stale = store.verify_current(active, 6);
    EXPECT(stale.error == MapSelectorStoreError::generation_below_floor);
    EXPECT(!stale.exact_match);

    const auto writes_before = storage.write_calls;
    const auto stale_writer = store.save_after_exact(active, 4, 5);
    EXPECT(stale_writer.error == MapSelectorStoreError::state_mismatch);
    EXPECT(storage.write_calls == writes_before);

    const auto advanced = store.save_after_exact(active, 5, 5);
    EXPECT(advanced.saved());
    EXPECT(advanced.generation == 6);
}

void test_trial_restart_count_is_resaved() {
    FakeMapSelectorStorage storage{};
    MapSelectorStore store{storage};
    const auto trial = trial_guard();
    EXPECT(store.save(trial).saved());

    MapActivationGuard restored{};
    const auto loaded = store.restore(
        restored, policy(), candidate(), package(), 500);
    EXPECT(loaded.restored);
    EXPECT(restored.status().state == MapActivationState::trial);
    EXPECT(restored.status().trial_boots == 2);
    const auto resaved = store.save(restored);
    EXPECT(resaved.saved());
    EXPECT(resaved.generation == 2);

    MapSelectorCheckpoint checkpoint{};
    EXPECT(decode_map_selector_checkpoint(
               storage.slots[1].data(), storage.slots[1].size(), checkpoint)
               .succeeded());
    EXPECT(checkpoint.trial_boots == 2);
}

void test_reset_reports_partial_failure_and_success() {
    FakeMapSelectorStorage storage{};
    MapSelectorStore store{storage};
    const auto guard = active_guard();
    EXPECT(store.save(guard).saved());
    EXPECT(store.save(guard).saved());
    storage.fail_erase_slot = 0;
    EXPECT(store.reset() == MapSelectorStoreError::storage_failure);
    EXPECT(storage.present[0]);
    EXPECT(!storage.present[1]);
    storage.fail_erase_slot = -1;
    EXPECT(store.reset() == MapSelectorStoreError::none);
    EXPECT(store.inspect().error == MapSelectorStoreError::no_checkpoint);
}

}  // namespace

int main() {
    test_empty_first_save_and_restore();
    test_rotation_selects_newest_unique_generation();
    test_partial_prepared_write_preserves_previous();
    test_commit_failure_is_uncertain_but_never_guessed();
    test_corrupt_readback_preserves_previous();
    test_degraded_peer_is_repaired_without_overwriting_good_slot();
    test_unreadable_slot_fails_closed();
    test_equal_generation_conflict_fails_closed();
    test_policy_and_package_mismatch_fail_mapless();
    test_generation_floor_and_exhaustion();
    test_current_guard_must_exactly_match_newest_checkpoint();
    test_trial_restart_count_is_resaved();
    test_reset_reports_partial_failure_and_success();

    if (failures != 0) {
        std::cerr << failures << " map selector store assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 13 map selector store scenario groups\n";
    return EXIT_SUCCESS;
}
