#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_checkpoint.hpp"
#include "opentrail/map_selector_reseed.hpp"
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
        if (inject_on_read_call != 0 && read_calls == inject_on_read_call) {
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
        ++erase_calls;
        if (fail_erase_slot == static_cast<int>(slot)) {
            return MapSelectorStorageError::io_failure;
        }
        if (pretend_erase_success_slot == static_cast<int>(slot)) {
            return MapSelectorStorageError::none;
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
    int pretend_erase_success_slot{-1};
    bool commit_then_fail{false};
    bool corrupt_after_commit{false};
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t commit_calls{0};
    std::uint32_t erase_calls{0};
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

MapActivationGuard mapless_guard(
    MapSelectorState selector = MapSelectorState::ambiguous) {
    MapActivationGuard guard{};
    EXPECT(guard.start(policy(), {selector, {}}) ==
           MapActivationError::none);
    return guard;
}

MapActivationGuard active_guard(
    MapSlot slot = MapSlot::slot_a,
    std::uint64_t generation = 10) {
    MapActivationGuard guard{};
    EXPECT(guard.start(policy(), {MapSelectorState::valid,
                                  package(slot, generation)}) ==
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

MapSelectorReseedAuthorizationEvidence authorization() {
    return {true, true, true, true, true};
}

MapSelectorReseedContext context(
    std::uint64_t trusted_minimum_generation = 0) {
    return {policy(), trusted_minimum_generation, authorization()};
}

void test_every_authorization_acknowledgement_is_required() {
    std::array<MapSelectorReseedAuthorizationEvidence, 5> incomplete{};
    incomplete.fill(authorization());
    incomplete[0].explicit_operator_confirmation = false;
    incomplete[1].map_unavailability_acknowledged = false;
    incomplete[2].selector_only_scope_confirmed = false;
    incomplete[3].package_retention_confirmed = false;
    incomplete[4].trusted_generation_reviewed = false;

    for (const auto& evidence : incomplete) {
        auto live = mapless_guard();
        FakeStorage storage{};
        MapSelectorStore store{storage};
        MapSelectorReseedCoordinator coordinator{store};
        auto ctx = context(4);
        ctx.authorization = evidence;
        const auto result = coordinator.reseed(live, ctx, package());
        EXPECT(result.state == MapSelectorReseedState::rejected);
        EXPECT(result.reason ==
               MapSelectorReseedReason::authorization_required);
        EXPECT(storage.read_calls == 0);
        EXPECT(storage.erase_calls == 0);
        EXPECT(storage.write_calls == 0);
    }
}

void test_only_running_mapless_service_owner_can_reseed() {
    auto active = active_guard();
    MapActivationGuard stopped{};
    MapActivationGuard* guards[] = {&active, &stopped};
    for (auto* guard : guards) {
        FakeStorage storage{};
        MapSelectorStore store{storage};
        MapSelectorReseedCoordinator coordinator{store};
        const auto result = coordinator.reseed(
            *guard, context(4), package());
        EXPECT(result.state == MapSelectorReseedState::rejected);
        EXPECT(result.reason ==
               (guard == &stopped
                    ? MapSelectorReseedReason::live_guard_not_running
                    : MapSelectorReseedReason::mapless_service_required));
        EXPECT(storage.erase_calls == 0);
    }
    EXPECT(active.status().map_available);
}

void test_clean_first_use_is_routed_to_baseline_coordinator() {
    auto live = mapless_guard(MapSelectorState::missing);
    FakeStorage storage{};
    MapSelectorStore store{storage};
    MapSelectorReseedCoordinator coordinator{store};
    const auto result = coordinator.reseed(live, context(), package());
    EXPECT(result.state == MapSelectorReseedState::rejected);
    EXPECT(result.reason ==
           MapSelectorReseedReason::first_use_requires_baseline);
    EXPECT(storage.erase_calls == 0);
    EXPECT(storage.write_calls == 0);
    EXPECT(live.status().reason == MapActivationReason::no_selector);
}

void test_valid_selector_reseeds_above_observed_generation() {
    auto live = mapless_guard();
    FakeStorage storage{};
    storage.seed(0, encode_guard(active_guard(), 5));
    MapSelectorStore store{storage};
    MapSelectorReseedCoordinator coordinator{store};
    const auto result = coordinator.reseed(live, context(3), package());
    EXPECT(result.committed());
    EXPECT(result.prior_observed_record_generation == 5);
    EXPECT(result.generation_base == 5);
    EXPECT(result.active_record_generation == 6);
    EXPECT(result.selector_clear_verified);
    EXPECT(result.map_exposure_allowed);
    EXPECT(live.status().state == MapActivationState::active);
    EXPECT(live.status().active_slot == MapSlot::slot_a);

    MapActivationGuard restored{};
    const auto loaded = store.restore_at_or_above(
        restored, policy(), package(), {}, 100, 6);
    EXPECT(loaded.restored);
    EXPECT(loaded.generation == 6);
    EXPECT(restored.status().state == MapActivationState::active);
}

void test_trusted_floor_and_conflict_generation_are_preserved() {
    auto trusted_live = mapless_guard();
    FakeStorage trusted_storage{};
    trusted_storage.seed(0, encode_guard(active_guard(), 5));
    MapSelectorStore trusted_store{trusted_storage};
    MapSelectorReseedCoordinator trusted{trusted_store};
    const auto above_trusted = trusted.reseed(
        trusted_live, context(8), package());
    EXPECT(above_trusted.committed());
    EXPECT(above_trusted.generation_base == 8);
    EXPECT(above_trusted.active_record_generation == 9);

    auto conflict_live = mapless_guard();
    FakeStorage conflict_storage{};
    conflict_storage.seed(0, encode_guard(active_guard(), 7));
    conflict_storage.seed(
        1, encode_guard(active_guard(MapSlot::slot_b, 11), 7));
    MapSelectorStore conflict_store{conflict_storage};
    MapSelectorReseedCoordinator conflict{conflict_store};
    const auto resolved = conflict.reseed(
        conflict_live, context(2), package());
    EXPECT(resolved.committed());
    EXPECT(resolved.inspection.error ==
           MapSelectorStoreError::generation_conflict);
    EXPECT(resolved.prior_observed_record_generation == 7);
    EXPECT(resolved.active_record_generation == 8);
}

void test_dirty_selector_can_reseed_from_reviewed_trusted_floor() {
    auto live = mapless_guard();
    FakeStorage storage{};
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> dirty{};
    dirty.fill(0xAAU);
    storage.seed(0, dirty);
    MapSelectorStore store{storage};
    MapSelectorReseedCoordinator coordinator{store};
    const auto result = coordinator.reseed(live, context(4), package());
    EXPECT(result.committed());
    EXPECT(result.inspection.error == MapSelectorStoreError::invalid_state);
    EXPECT(result.prior_observed_record_generation == 0);
    EXPECT(result.active_record_generation == 5);
}

void test_candidate_and_policy_fail_before_selector_mutation() {
    auto invalid_live = mapless_guard();
    FakeStorage invalid_storage{};
    MapSelectorStore invalid_store{invalid_storage};
    MapSelectorReseedCoordinator invalid_coordinator{invalid_store};
    auto invalid = package();
    invalid.integrity_verified = false;
    const auto rejected = invalid_coordinator.reseed(
        invalid_live, context(4), invalid);
    EXPECT(rejected.reason == MapSelectorReseedReason::candidate_rejected);
    EXPECT(invalid_storage.read_calls == 0);
    EXPECT(invalid_storage.erase_calls == 0);

    auto policy_live = mapless_guard();
    FakeStorage policy_storage{};
    MapSelectorStore policy_store{policy_storage};
    MapSelectorReseedCoordinator policy_coordinator{policy_store};
    auto wrong = context(4);
    wrong.policy.maximum_trial_boots = 2;
    const auto mismatch = policy_coordinator.reseed(
        policy_live, wrong, package());
    EXPECT(mismatch.reason == MapSelectorReseedReason::invalid_policy);
    EXPECT(mismatch.state == MapSelectorReseedState::service_required);
    EXPECT(!policy_live.status().running);
    EXPECT(policy_storage.erase_calls == 0);
}

void test_storage_failure_and_generation_exhaustion_prevent_erase() {
    auto unavailable_live = mapless_guard();
    FakeStorage unavailable_storage{};
    unavailable_storage.fail_read_slot = 1;
    MapSelectorStore unavailable_store{unavailable_storage};
    MapSelectorReseedCoordinator unavailable{unavailable_store};
    const auto failed = unavailable.reseed(
        unavailable_live, context(4), package());
    EXPECT(failed.reason == MapSelectorReseedReason::storage_unavailable);
    EXPECT(failed.state == MapSelectorReseedState::service_required);
    EXPECT(unavailable_storage.erase_calls == 0);

    auto exhausted_live = mapless_guard();
    FakeStorage exhausted_storage{};
    exhausted_storage.seed(
        0,
        encode_guard(
            active_guard(), std::numeric_limits<std::uint64_t>::max()));
    MapSelectorStore exhausted_store{exhausted_storage};
    MapSelectorReseedCoordinator exhausted{exhausted_store};
    const auto blocked = exhausted.reseed(
        exhausted_live, context(), package());
    EXPECT(blocked.reason == MapSelectorReseedReason::generation_exhausted);
    EXPECT(exhausted_storage.erase_calls == 0);
    EXPECT(exhausted_storage.present[0]);
}

void test_partial_or_unverified_clear_requires_reconciliation() {
    auto partial_live = mapless_guard();
    FakeStorage partial_storage{};
    partial_storage.seed(0, encode_guard(active_guard(), 5));
    partial_storage.fail_erase_slot = 0;
    MapSelectorStore partial_store{partial_storage};
    MapSelectorReseedCoordinator partial{partial_store};
    const auto failed = partial.reseed(
        partial_live, context(), package());
    EXPECT(failed.reason == MapSelectorReseedReason::selector_reset_failed);
    EXPECT(failed.reconciliation_required);
    EXPECT(!failed.selector_clear_verified);
    EXPECT(partial_storage.write_calls == 0);
    EXPECT(partial_live.status().reason ==
           MapActivationReason::selector_ambiguous);

    auto retained_live = mapless_guard();
    FakeStorage retained_storage{};
    retained_storage.seed(0, encode_guard(active_guard(), 5));
    retained_storage.pretend_erase_success_slot = 0;
    MapSelectorStore retained_store{retained_storage};
    MapSelectorReseedCoordinator retained_coordinator{retained_store};
    const auto retained = retained_coordinator.reseed(
        retained_live, context(), package());
    EXPECT(retained.reason ==
           MapSelectorReseedReason::selector_reset_unverified);
    EXPECT(retained.reconciliation_required);
    EXPECT(retained.reset.slot_a == MapSelectorSlotState::valid);
    EXPECT(retained_storage.write_calls == 0);
}

void test_prepared_write_failure_stays_mapless_after_verified_clear() {
    auto live = mapless_guard();
    FakeStorage storage{};
    storage.seed(0, encode_guard(active_guard(), 5));
    storage.fail_write_slot = 0;
    MapSelectorStore store{storage};
    MapSelectorReseedCoordinator coordinator{store};
    const auto result = coordinator.reseed(live, context(), package());
    EXPECT(result.reason ==
           MapSelectorReseedReason::checkpoint_save_failed);
    EXPECT(result.state == MapSelectorReseedState::service_required);
    EXPECT(result.selector_clear_verified);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(live.status().state == MapActivationState::mapless);
}

void test_uncertain_commit_and_bad_readback_require_reconciliation() {
    auto uncertain_live = mapless_guard();
    FakeStorage uncertain_storage{};
    uncertain_storage.seed(0, encode_guard(active_guard(), 5));
    uncertain_storage.fail_commit_slot = 0;
    uncertain_storage.commit_then_fail = true;
    MapSelectorStore uncertain_store{uncertain_storage};
    MapSelectorReseedCoordinator uncertain{uncertain_store};
    const auto commit_failed = uncertain.reseed(
        uncertain_live, context(), package());
    EXPECT(commit_failed.reason ==
           MapSelectorReseedReason::checkpoint_commit_uncertain);
    EXPECT(commit_failed.reconciliation_required);
    EXPECT(commit_failed.selector_clear_verified);

    auto corrupt_live = mapless_guard();
    FakeStorage corrupt_storage{};
    corrupt_storage.seed(0, encode_guard(active_guard(), 5));
    corrupt_storage.corrupt_after_commit = true;
    MapSelectorStore corrupt_store{corrupt_storage};
    MapSelectorReseedCoordinator corrupt{corrupt_store};
    const auto bad_readback = corrupt.reseed(
        corrupt_live, context(), package());
    EXPECT(bad_readback.reason ==
           MapSelectorReseedReason::checkpoint_verification_failed);
    EXPECT(bad_readback.reconciliation_required);
    EXPECT(corrupt_live.status().state == MapActivationState::mapless);
}

void test_selector_race_after_clear_is_never_overwritten() {
    auto live = mapless_guard();
    FakeStorage storage{};
    storage.seed(0, encode_guard(active_guard(), 5));
    storage.inject_on_read_call = 5;
    storage.inject_slot = 1;
    storage.inject_bytes = encode_guard(active_guard(), 9);
    MapSelectorStore store{storage};
    MapSelectorReseedCoordinator coordinator{store};
    const auto result = coordinator.reseed(live, context(), package());
    EXPECT(result.reason == MapSelectorReseedReason::selector_changed);
    EXPECT(result.save.error == MapSelectorStoreError::state_mismatch);
    EXPECT(result.reconciliation_required);
    EXPECT(result.selector_clear_verified);
    EXPECT(storage.write_calls == 0);
    EXPECT(storage.present[1]);
    EXPECT(live.status().reason == MapActivationReason::selector_ambiguous);
}

}  // namespace

int main() {
    test_every_authorization_acknowledgement_is_required();
    test_only_running_mapless_service_owner_can_reseed();
    test_clean_first_use_is_routed_to_baseline_coordinator();
    test_valid_selector_reseeds_above_observed_generation();
    test_trusted_floor_and_conflict_generation_are_preserved();
    test_dirty_selector_can_reseed_from_reviewed_trusted_floor();
    test_candidate_and_policy_fail_before_selector_mutation();
    test_storage_failure_and_generation_exhaustion_prevent_erase();
    test_partial_or_unverified_clear_requires_reconciliation();
    test_prepared_write_failure_stays_mapless_after_verified_clear();
    test_uncertain_commit_and_bad_readback_require_reconciliation();
    test_selector_race_after_clear_is_never_overwritten();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector reseed assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 12 map selector reseed scenario groups\n";
    return EXIT_SUCCESS;
}
