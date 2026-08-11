#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_boot.hpp"
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
    std::uint32_t write_calls{0};
};

MapActivationPolicy policy(std::uint8_t maximum_trial_boots = 3) {
    return {8U * 1024U * 1024U, 500, 3, maximum_trial_boots};
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

MapActivationGuard active_guard(
    const MapActivationPolicy& selected_policy = policy()) {
    MapActivationGuard guard{};
    EXPECT(guard.start(
               selected_policy,
               {MapSelectorState::valid, package()}) ==
           MapActivationError::none);
    return guard;
}

MapActivationGuard trial_guard(
    const MapActivationPolicy& selected_policy = policy()) {
    auto guard = active_guard(selected_policy);
    EXPECT(guard.stage(candidate_package()) == MapActivationError::none);
    EXPECT(guard.mark_selector_committed(
               MapSlot::slot_b, 11, 100) ==
           MapActivationError::none);
    return guard;
}

MapActivationGuard fallback_guard() {
    auto guard = trial_guard();
    EXPECT(guard.report_trial_read(false, 101) ==
           MapActivationError::trial_health_failed);
    return guard;
}

std::array<std::uint8_t, kMapSelectorCheckpointBytes> encode_guard(
    const MapActivationGuard& guard,
    std::uint64_t generation,
    std::uint8_t trial_boots_override = 0) {
    MapSelectorCheckpoint checkpoint{};
    EXPECT(guard.export_checkpoint(generation, checkpoint) ==
           MapActivationError::none);
    if (trial_boots_override != 0) {
        checkpoint.trial_boots = trial_boots_override;
    }
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> bytes{};
    EXPECT(encode_map_selector_checkpoint(
               checkpoint, bytes.data(), bytes.size()).succeeded());
    return bytes;
}

MapSelectorBootResult boot(
    FakeStorage& storage,
    MapActivationGuard& live,
    const MapActivationPolicy& selected_policy = policy(),
    const MapPackageEvidence& selected = candidate_package(),
    const MapPackageEvidence& previous = package(),
    std::uint64_t floor = 0) {
    MapSelectorStore store{storage};
    MapSelectorBootCoordinator coordinator{store};
    return coordinator.boot(
        selected_policy, selected, previous, 200, floor, live);
}

void test_active_checkpoint_releases_without_write() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(active_guard(), 4));
    MapActivationGuard live{};
    const auto result = boot(
        storage, live, policy(), package(), {});
    EXPECT(result.state == MapSelectorBootState::active_ready);
    EXPECT(result.operational());
    EXPECT(result.map_exposure_allowed);
    EXPECT(result.active_generation == 4);
    EXPECT(result.repair_required);
    EXPECT(storage.write_calls == 0);
    EXPECT(live.status().state == MapActivationState::active);
}

void test_trial_increment_is_persisted_before_release() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(trial_guard(), 1));
    MapActivationGuard live{};
    const auto result = boot(
        storage,
        live,
        policy(),
        candidate_package(),
        package(),
        1);
    EXPECT(result.state == MapSelectorBootState::trial_ready);
    EXPECT(result.operational());
    EXPECT(result.map_exposure_allowed);
    EXPECT(result.save.saved());
    EXPECT(result.loaded_generation == 1);
    EXPECT(result.active_generation == 2);
    EXPECT(storage.write_calls == 1);
    EXPECT(live.status().state == MapActivationState::trial);
    EXPECT(live.status().trial_boots == 2);

    MapSelectorCheckpoint saved{};
    EXPECT(decode_map_selector_checkpoint(
               storage.slots[1].data(), storage.slots[1].size(), saved)
               .succeeded());
    EXPECT(saved.trial_boots == 2);
    EXPECT(saved.record_generation == 2);
}

void test_existing_fallback_releases_without_map_or_write() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(fallback_guard(), 3));
    MapActivationGuard live{};
    const auto result = boot(storage, live);
    EXPECT(result.state == MapSelectorBootState::fallback_required);
    EXPECT(result.operational());
    EXPECT(result.fallback_required);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(storage.write_calls == 0);
    EXPECT(live.status().state == MapActivationState::fallback_required);
}

void test_trial_limit_transition_is_persisted_before_release() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(trial_guard(), 7, 3));
    MapActivationGuard live{};
    const auto result = boot(storage, live);
    EXPECT(result.state == MapSelectorBootState::fallback_required);
    EXPECT(result.reason == MapSelectorBootReason::trial_boot_limit);
    EXPECT(result.save.saved());
    EXPECT(result.active_generation == 8);
    EXPECT(result.fallback_required);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(live.status().state == MapActivationState::fallback_required);
    EXPECT(live.status().reason ==
           MapActivationReason::trial_boot_limit_reached);
}

void test_prepared_write_failure_keeps_candidate_mapless() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(trial_guard(), 1));
    storage.fail_write_slot = 1;
    MapActivationGuard live{};
    const auto result = boot(storage, live);
    EXPECT(result.state == MapSelectorBootState::service_required);
    EXPECT(result.reason ==
           MapSelectorBootReason::checkpoint_save_failed);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(result.live_guard_published);
    EXPECT(!result.reconciliation_required);
    EXPECT(live.status().state == MapActivationState::mapless);
    EXPECT(live.status().reason == MapActivationReason::selector_unreadable);
}

void test_uncertain_or_bad_commit_never_releases_candidate() {
    FakeStorage uncertain{};
    uncertain.seed(0, encode_guard(trial_guard(), 1));
    uncertain.fail_commit_slot = 1;
    uncertain.commit_then_fail = true;
    MapActivationGuard uncertain_live{};
    const auto commit = boot(uncertain, uncertain_live);
    EXPECT(commit.reason ==
           MapSelectorBootReason::checkpoint_commit_uncertain);
    EXPECT(commit.reconciliation_required);
    EXPECT(!commit.map_exposure_allowed);
    EXPECT(uncertain_live.status().state == MapActivationState::mapless);

    FakeStorage corrupt{};
    corrupt.seed(0, encode_guard(trial_guard(), 1));
    corrupt.corrupt_after_commit = true;
    MapActivationGuard corrupt_live{};
    const auto verification = boot(corrupt, corrupt_live);
    EXPECT(verification.reason ==
           MapSelectorBootReason::checkpoint_verification_failed);
    EXPECT(verification.reconciliation_required);
    EXPECT(!verification.map_exposure_allowed);
    EXPECT(corrupt_live.status().state == MapActivationState::mapless);
}

void test_empty_store_becomes_explicit_mapless() {
    FakeStorage storage{};
    MapActivationGuard live{};
    const auto result = boot(storage, live);
    EXPECT(result.state == MapSelectorBootState::mapless_ready);
    EXPECT(result.reason == MapSelectorBootReason::no_checkpoint);
    EXPECT(result.operational());
    EXPECT(!result.map_exposure_allowed);
    EXPECT(live.status().state == MapActivationState::mapless);
    EXPECT(live.status().reason == MapActivationReason::no_selector);
}

void test_unreadable_conflict_and_rollback_are_typed_mapless() {
    FakeStorage unreadable{};
    unreadable.seed(0, encode_guard(active_guard(), 5));
    unreadable.fail_read_slot = 1;
    MapActivationGuard unreadable_live{};
    const auto unavailable = boot(
        unreadable, unreadable_live, policy(), package(), {});
    EXPECT(unavailable.reason ==
           MapSelectorBootReason::storage_unavailable);
    EXPECT(unreadable_live.status().reason ==
           MapActivationReason::selector_unreadable);

    FakeStorage conflict{};
    conflict.seed(0, encode_guard(active_guard(), 5));
    conflict.seed(1, encode_guard(fallback_guard(), 5));
    MapActivationGuard conflict_live{};
    const auto ambiguous = boot(
        conflict, conflict_live, policy(), package(), {});
    EXPECT(ambiguous.reason ==
           MapSelectorBootReason::generation_conflict);
    EXPECT(ambiguous.reconciliation_required);
    EXPECT(conflict_live.status().reason ==
           MapActivationReason::selector_ambiguous);

    FakeStorage stale{};
    stale.seed(0, encode_guard(active_guard(), 5));
    MapActivationGuard stale_live{};
    const auto rollback = boot(
        stale, stale_live, policy(), package(), {}, 6);
    EXPECT(rollback.reason == MapSelectorBootReason::rollback_detected);
    EXPECT(rollback.reconciliation_required);
    EXPECT(stale_live.status().reason ==
           MapActivationReason::selector_ambiguous);
}

void test_checkpoint_rejection_stays_mapless() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(trial_guard(), 1));
    auto wrong = candidate_package();
    wrong.generation = 99;
    MapActivationGuard live{};
    const auto result = boot(storage, live, policy(), wrong, package());
    EXPECT(result.reason == MapSelectorBootReason::checkpoint_rejected);
    EXPECT(result.guard_error == MapActivationError::verification_required);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(live.status().state == MapActivationState::mapless);
}

void test_invalid_policy_and_dirty_live_guard_are_not_replaced() {
    FakeStorage empty{};
    auto invalid = policy();
    invalid.maximum_trial_boots = 0;
    MapActivationGuard invalid_live{};
    const auto rejected = boot(empty, invalid_live, invalid);
    EXPECT(rejected.reason == MapSelectorBootReason::invalid_policy);
    EXPECT(!rejected.live_guard_published);
    EXPECT(!invalid_live.status().running);

    FakeStorage storage{};
    storage.seed(0, encode_guard(active_guard(), 1));
    auto dirty_live = active_guard();
    const auto dirty = boot(
        storage, dirty_live, policy(), package(), {});
    EXPECT(dirty.reason == MapSelectorBootReason::live_guard_not_clean);
    EXPECT(dirty.guard_error == MapActivationError::invalid_state);
    EXPECT(dirty_live.status().state == MapActivationState::active);
}

}  // namespace

int main() {
    test_active_checkpoint_releases_without_write();
    test_trial_increment_is_persisted_before_release();
    test_existing_fallback_releases_without_map_or_write();
    test_trial_limit_transition_is_persisted_before_release();
    test_prepared_write_failure_keeps_candidate_mapless();
    test_uncertain_or_bad_commit_never_releases_candidate();
    test_empty_store_becomes_explicit_mapless();
    test_unreadable_conflict_and_rollback_are_typed_mapless();
    test_checkpoint_rejection_stays_mapless();
    test_invalid_policy_and_dirty_live_guard_are_not_replaced();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector boot assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 map selector boot scenario groups\n";
    return EXIT_SUCCESS;
}
