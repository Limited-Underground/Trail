#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_checkpoint.hpp"
#include "opentrail/map_selector_store.hpp"
#include "opentrail/map_selector_trusted_candidate.hpp"

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
        ++read_calls;
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
        ++write_calls;
        if (slot >= slots.size() || data == nullptr ||
            size != kMapSelectorCheckpointBytes) {
            return MapSelectorStorageError::invalid_argument;
        }
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
        ++commit_calls;
        if (slot >= slots.size() || !present[slot] ||
            offset != kMapSelectorCommitOffset ||
            value != kMapSelectorCommitMarker) {
            return MapSelectorStorageError::invalid_argument;
        }
        slots[slot][offset] = value;
        return MapSelectorStorageError::none;
    }

    MapSelectorStorageError erase_slot(std::uint8_t slot) override {
        ++erase_calls;
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
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t commit_calls{0};
    std::uint32_t erase_calls{0};
};

class FakeTrustedSource final
    : public MapSelectorTrustedGenerationSource {
public:
    MapSelectorTrustedGenerationRead read() override {
        ++read_calls;
        if (jump_on_read != 0 && read_calls == jump_on_read) {
            generation = jump_to;
        }
        if (fail_on_read != 0 && read_calls == fail_on_read) {
            return {read_error, generation};
        }
        return {MapSelectorTrustedGenerationSourceError::none, generation};
    }

    MapSelectorTrustedGenerationSourceError compare_and_advance(
        std::uint64_t expected_current_generation,
        std::uint64_t requested_generation) override {
        ++advance_calls;
        observed_expected = expected_current_generation;
        observed_requested = requested_generation;
        writes_seen_at_advance =
            storage == nullptr ? 0 : storage->write_calls;
        commits_seen_at_advance =
            storage == nullptr ? 0 : storage->commit_calls;
        if (advance_error !=
            MapSelectorTrustedGenerationSourceError::none) {
            if (apply_on_error) {
                generation = requested_generation;
            }
            return advance_error;
        }
        generation = requested_generation;
        return MapSelectorTrustedGenerationSourceError::none;
    }

    FakeStorage* storage{nullptr};
    MapSelectorTrustedGenerationSourceError read_error{
        MapSelectorTrustedGenerationSourceError::not_ready};
    MapSelectorTrustedGenerationSourceError advance_error{
        MapSelectorTrustedGenerationSourceError::none};
    std::uint64_t generation{0};
    std::uint64_t observed_expected{0};
    std::uint64_t observed_requested{0};
    std::uint64_t jump_to{0};
    std::uint32_t read_calls{0};
    std::uint32_t advance_calls{0};
    std::uint32_t fail_on_read{0};
    std::uint32_t jump_on_read{0};
    std::uint32_t writes_seen_at_advance{0};
    std::uint32_t commits_seen_at_advance{0};
    bool apply_on_error{false};
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

MapActivationGuard staged_guard() {
    auto guard = active_guard();
    EXPECT(guard.stage(candidate_package()) == MapActivationError::none);
    return guard;
}

MapActivationGuard trial_guard() {
    auto guard = staged_guard();
    EXPECT(guard.mark_selector_committed(
               MapSlot::slot_b, 11, 100) ==
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

class Fixture {
public:
    Fixture()
        : store(storage),
          candidate(store),
          trusted(source),
          coordinator(candidate, trusted) {
        source.storage = &storage;
    }

    void seed_current(
        const MapActivationGuard& guard,
        std::uint64_t generation = 1) {
        storage.seed(0, encode_guard(guard, generation));
    }

    FakeStorage storage{};
    MapSelectorStore store;
    MapSelectorCandidateCoordinator candidate;
    FakeTrustedSource source{};
    MapSelectorTrustedGeneration trusted;
    MapSelectorTrustedCandidateCoordinator coordinator;
};

void test_candidate_save_advances_trust_before_trial_publish() {
    auto live = active_guard();
    Fixture fixture{};
    fixture.seed_current(live);
    fixture.source.generation = 1;

    const auto result = fixture.coordinator.activate(
        live, policy(), candidate_package(), 100);
    EXPECT(result.committed());
    EXPECT(result.candidate.committed());
    EXPECT(result.candidate.active_record_generation == 2);
    EXPECT(result.trusted_generation_before == 1);
    EXPECT(result.trusted_generation_after == 2);
    EXPECT(fixture.source.observed_expected == 1);
    EXPECT(fixture.source.observed_requested == 2);
    EXPECT(fixture.source.writes_seen_at_advance == 1);
    EXPECT(fixture.source.commits_seen_at_advance == 1);
    EXPECT(fixture.source.advance_calls == 1);
    EXPECT(live.status().state == MapActivationState::trial);
    EXPECT(live.status().active_slot == MapSlot::slot_b);
    EXPECT(result.map_exposure_allowed);
}

void test_rejected_candidate_rechecks_trust_and_preserves_active_map() {
    auto live = active_guard();
    Fixture fixture{};
    fixture.seed_current(live);
    fixture.source.generation = 1;
    auto invalid = candidate_package();
    invalid.integrity_verified = false;

    const auto result = fixture.coordinator.activate(
        live, policy(), invalid, 100);
    EXPECT(result.protected_ordering_satisfied());
    EXPECT(!result.committed());
    EXPECT(result.candidate.state == MapSelectorCandidateState::rejected);
    EXPECT(result.candidate.reason ==
           MapSelectorCandidateReason::candidate_rejected);
    EXPECT(fixture.source.read_calls == 2);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(fixture.storage.write_calls == 0);
    EXPECT(live.status().state == MapActivationState::active);
    EXPECT(live.status().active_slot == MapSlot::slot_a);
    EXPECT(result.map_exposure_allowed);
}

void test_initial_trust_failure_blocks_storage_and_contains_map() {
    auto live = active_guard();
    Fixture fixture{};
    fixture.seed_current(live);
    fixture.source.generation = 1;
    fixture.source.fail_on_read = 1;

    const auto result = fixture.coordinator.activate(
        live, policy(), candidate_package(), 100);
    EXPECT(result.reason == MapSelectorTrustedCandidateReason::
                                trusted_source_unavailable);
    EXPECT(fixture.storage.read_calls == 0);
    EXPECT(fixture.storage.write_calls == 0);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(result.live_guard_mapless);
    EXPECT(live.status().reason ==
           MapActivationReason::selector_unreadable);
}

void test_zero_or_stale_trust_cannot_label_selector_current() {
    auto zero_live = active_guard();
    Fixture zero{};
    zero.seed_current(zero_live);
    const auto missing = zero.coordinator.activate(
        zero_live, policy(), candidate_package(), 100);
    EXPECT(missing.reason ==
           MapSelectorTrustedCandidateReason::candidate_failed);
    EXPECT(missing.candidate.reason ==
           MapSelectorCandidateReason::current_generation_mismatch);
    EXPECT(missing.reconciliation_required);
    EXPECT(zero.source.advance_calls == 0);
    EXPECT(zero_live.status().state == MapActivationState::mapless);

    auto stale_live = active_guard();
    Fixture stale{};
    stale.seed_current(stale_live);
    stale.source.generation = 2;
    const auto rollback = stale.coordinator.activate(
        stale_live, policy(), candidate_package(), 100);
    EXPECT(rollback.reason ==
           MapSelectorTrustedCandidateReason::candidate_failed);
    EXPECT(rollback.candidate.reason ==
           MapSelectorCandidateReason::rollback_detected);
    EXPECT(rollback.reconciliation_required);
    EXPECT(stale.source.advance_calls == 0);
    EXPECT(stale_live.status().state == MapActivationState::mapless);
}

void test_live_selector_mismatch_remains_mapless() {
    auto live = active_guard();
    Fixture fixture{};
    fixture.seed_current(trial_guard());
    fixture.source.generation = 1;

    const auto result = fixture.coordinator.activate(
        live, policy(), candidate_package(), 100);
    EXPECT(result.reason ==
           MapSelectorTrustedCandidateReason::candidate_failed);
    EXPECT(result.candidate.reason ==
           MapSelectorCandidateReason::live_checkpoint_mismatch);
    EXPECT(result.reconciliation_required);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(live.status().state == MapActivationState::mapless);
}

void test_selector_save_failure_never_advances_trust() {
    auto live = active_guard();
    Fixture fixture{};
    fixture.seed_current(live);
    fixture.source.generation = 1;
    fixture.storage.fail_write_slot = 1;

    const auto result = fixture.coordinator.activate(
        live, policy(), candidate_package(), 100);
    EXPECT(result.reason ==
           MapSelectorTrustedCandidateReason::candidate_failed);
    EXPECT(result.candidate.reason ==
           MapSelectorCandidateReason::checkpoint_save_failed);
    EXPECT(fixture.storage.write_calls == 1);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(result.live_guard_mapless);
}

void test_uncertain_trust_advance_contains_saved_candidate() {
    auto live = active_guard();
    Fixture fixture{};
    fixture.seed_current(live);
    fixture.source.generation = 1;
    fixture.source.advance_error =
        MapSelectorTrustedGenerationSourceError::io_failure;
    fixture.source.apply_on_error = true;

    const auto result = fixture.coordinator.activate(
        live, policy(), candidate_package(), 100);
    EXPECT(result.reason == MapSelectorTrustedCandidateReason::
                                trusted_advance_failed);
    EXPECT(result.reconciliation_required);
    EXPECT(result.candidate.committed());
    EXPECT(fixture.store.inspect().generation == 2);
    EXPECT(fixture.source.generation == 2);
    EXPECT(fixture.source.advance_calls == 1);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(result.live_guard_mapless);
    EXPECT(live.status().reason ==
           MapActivationReason::selector_ambiguous);
}

void test_protected_conflict_after_save_requires_reconciliation() {
    auto live = active_guard();
    Fixture fixture{};
    fixture.seed_current(live);
    fixture.source.generation = 1;
    fixture.source.jump_on_read = 2;
    fixture.source.jump_to = 2;

    const auto result = fixture.coordinator.activate(
        live, policy(), candidate_package(), 100);
    EXPECT(result.reason == MapSelectorTrustedCandidateReason::
                                trusted_advance_failed);
    EXPECT(result.reconciliation_required);
    EXPECT(result.candidate.committed());
    EXPECT(result.trusted_after.reason ==
           MapSelectorTrustedGenerationReason::generation_conflict);
    EXPECT(fixture.store.inspect().generation == 2);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(result.live_guard_mapless);
}

void test_final_trust_change_after_rejection_contains_active_map() {
    auto live = active_guard();
    Fixture fixture{};
    fixture.seed_current(live);
    fixture.source.generation = 1;
    fixture.source.jump_on_read = 2;
    fixture.source.jump_to = 2;
    auto invalid = candidate_package();
    invalid.integrity_verified = false;

    const auto result = fixture.coordinator.activate(
        live, policy(), invalid, 100);
    EXPECT(result.reason == MapSelectorTrustedCandidateReason::
                                trusted_source_changed);
    EXPECT(result.reconciliation_required);
    EXPECT(result.candidate.state == MapSelectorCandidateState::rejected);
    EXPECT(fixture.storage.write_calls == 0);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(live.status().reason ==
           MapActivationReason::selector_ambiguous);
}

void test_invalid_policy_failure_stays_private_and_mapless() {
    auto live = active_guard();
    Fixture fixture{};
    fixture.seed_current(live);
    fixture.source.generation = 1;
    auto wrong_policy = policy();
    wrong_policy.maximum_trial_boots = 2;

    const auto result = fixture.coordinator.activate(
        live, wrong_policy, candidate_package(), 100);
    EXPECT(result.reason ==
           MapSelectorTrustedCandidateReason::candidate_failed);
    EXPECT(result.candidate.reason ==
           MapSelectorCandidateReason::invalid_policy);
    EXPECT(fixture.storage.write_calls == 0);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(result.live_guard_mapless);
}

void test_nonbaseline_live_states_touch_no_backend() {
    MapActivationGuard stopped{};
    auto mapless = mapless_guard();
    auto staged = staged_guard();
    auto trial = trial_guard();
    MapActivationGuard* guards[] = {
        &stopped, &mapless, &staged, &trial};

    for (auto* guard : guards) {
        Fixture fixture{};
        const auto result = fixture.coordinator.activate(
            *guard, policy(), candidate_package(), 100);
        EXPECT(result.reason == MapSelectorTrustedCandidateReason::
                                    live_state_not_candidate_baseline);
        EXPECT(fixture.source.read_calls == 0);
        EXPECT(fixture.source.advance_calls == 0);
        EXPECT(fixture.storage.read_calls == 0);
        EXPECT(fixture.storage.write_calls == 0);
    }
}

}  // namespace

int main() {
    test_candidate_save_advances_trust_before_trial_publish();
    test_rejected_candidate_rechecks_trust_and_preserves_active_map();
    test_initial_trust_failure_blocks_storage_and_contains_map();
    test_zero_or_stale_trust_cannot_label_selector_current();
    test_live_selector_mismatch_remains_mapless();
    test_selector_save_failure_never_advances_trust();
    test_uncertain_trust_advance_contains_saved_candidate();
    test_protected_conflict_after_save_requires_reconciliation();
    test_final_trust_change_after_rejection_contains_active_map();
    test_invalid_policy_failure_stays_private_and_mapless();
    test_nonbaseline_live_states_touch_no_backend();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector trusted-candidate assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout
        << "PASS: 11 map selector trusted-candidate scenario groups\n";
    return EXIT_SUCCESS;
}
