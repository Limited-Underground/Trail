#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_checkpoint.hpp"
#include "opentrail/map_selector_store.hpp"
#include "opentrail/map_selector_trusted_baseline.hpp"

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

MapActivationGuard clean_mapless_guard() {
    MapActivationGuard guard{};
    EXPECT(guard.start(
               policy(), {MapSelectorState::missing, {}}) ==
           MapActivationError::none);
    return guard;
}

MapActivationGuard active_guard() {
    MapActivationGuard guard{};
    EXPECT(guard.start(
               policy(), {MapSelectorState::valid, package()}) ==
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

class Fixture {
public:
    Fixture()
        : store(storage),
          baseline(store),
          trusted(source),
          coordinator(baseline, trusted) {
        source.storage = &storage;
    }

    FakeStorage storage{};
    MapSelectorStore store;
    MapSelectorBaselineCoordinator baseline;
    FakeTrustedSource source{};
    MapSelectorTrustedGeneration trusted;
    MapSelectorTrustedBaselineCoordinator coordinator;
};

void test_generation_one_save_advances_trust_before_publish() {
    auto live = clean_mapless_guard();
    Fixture fixture{};

    const auto result = fixture.coordinator.establish(
        live, policy(), package());
    EXPECT(result.committed());
    EXPECT(result.baseline.committed());
    EXPECT(result.baseline.active_record_generation == 1);
    EXPECT(result.trusted_generation_before == 0);
    EXPECT(result.trusted_generation_after == 1);
    EXPECT(fixture.source.observed_expected == 0);
    EXPECT(fixture.source.observed_requested == 1);
    EXPECT(fixture.source.writes_seen_at_advance == 1);
    EXPECT(fixture.source.commits_seen_at_advance == 1);
    EXPECT(fixture.source.advance_calls == 1);
    EXPECT(live.status().state == MapActivationState::active);
    EXPECT(live.status().active_slot == MapSlot::slot_a);
    EXPECT(result.map_exposure_allowed);
}

void test_nonzero_trusted_history_blocks_selector_access() {
    auto live = clean_mapless_guard();
    Fixture fixture{};
    fixture.source.generation = 4;

    const auto result = fixture.coordinator.establish(
        live, policy(), package());
    EXPECT(result.reason == MapSelectorTrustedBaselineReason::
                                trusted_history_present);
    EXPECT(result.reconciliation_required);
    EXPECT(result.baseline.reason ==
           MapSelectorBaselineReason::trusted_history_present);
    EXPECT(fixture.storage.read_calls == 0);
    EXPECT(fixture.storage.write_calls == 0);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(live.status().reason ==
           MapActivationReason::selector_ambiguous);
}

void test_retryable_trust_failure_preserves_clean_first_use() {
    auto live = clean_mapless_guard();
    Fixture fixture{};
    fixture.source.fail_on_read = 1;

    const auto result = fixture.coordinator.establish(
        live, policy(), package());
    EXPECT(result.reason == MapSelectorTrustedBaselineReason::
                                trusted_source_unavailable);
    EXPECT(!result.reconciliation_required);
    EXPECT(!result.live_guard_updated);
    EXPECT(fixture.storage.read_calls == 0);
    EXPECT(fixture.storage.write_calls == 0);
    EXPECT(live.status().reason == MapActivationReason::no_selector);
}

void test_latched_trust_failure_contains_clean_first_use() {
    auto live = clean_mapless_guard();
    Fixture fixture{};
    fixture.source.advance_error =
        MapSelectorTrustedGenerationSourceError::io_failure;
    const auto latched = fixture.trusted.advance_exact(0, 1);
    EXPECT(latched.reconciliation_required);
    const auto reads_before = fixture.source.read_calls;

    const auto result = fixture.coordinator.establish(
        live, policy(), package());
    EXPECT(result.reason == MapSelectorTrustedBaselineReason::
                                trusted_source_unavailable);
    EXPECT(result.reconciliation_required);
    EXPECT(fixture.source.read_calls == reads_before);
    EXPECT(fixture.storage.read_calls == 0);
    EXPECT(fixture.storage.write_calls == 0);
    EXPECT(live.status().reason ==
           MapActivationReason::selector_ambiguous);
}

void test_invalid_package_preserves_clean_mapless_state() {
    auto live = clean_mapless_guard();
    Fixture fixture{};
    auto invalid = package();
    invalid.integrity_verified = false;

    const auto result = fixture.coordinator.establish(
        live, policy(), invalid);
    EXPECT(result.reason ==
           MapSelectorTrustedBaselineReason::baseline_failed);
    EXPECT(result.baseline.state == MapSelectorBaselineState::rejected);
    EXPECT(result.baseline.reason ==
           MapSelectorBaselineReason::candidate_rejected);
    EXPECT(fixture.storage.read_calls == 0);
    EXPECT(fixture.storage.write_calls == 0);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(live.status().reason == MapActivationReason::no_selector);
}

void test_existing_selector_never_becomes_first_use() {
    auto live = clean_mapless_guard();
    Fixture fixture{};
    fixture.storage.seed(0, encode_guard(active_guard(), 1));

    const auto result = fixture.coordinator.establish(
        live, policy(), package());
    EXPECT(result.reason ==
           MapSelectorTrustedBaselineReason::baseline_failed);
    EXPECT(result.baseline.reason ==
           MapSelectorBaselineReason::selector_not_empty);
    EXPECT(result.reconciliation_required);
    EXPECT(fixture.storage.write_calls == 0);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(live.status().reason ==
           MapActivationReason::selector_ambiguous);
}

void test_selector_save_failure_never_advances_trust() {
    auto live = clean_mapless_guard();
    Fixture fixture{};
    fixture.storage.fail_write_slot = 0;

    const auto result = fixture.coordinator.establish(
        live, policy(), package());
    EXPECT(result.reason ==
           MapSelectorTrustedBaselineReason::baseline_failed);
    EXPECT(result.baseline.reason ==
           MapSelectorBaselineReason::checkpoint_save_failed);
    EXPECT(fixture.storage.write_calls == 1);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(result.live_guard_mapless);
}

void test_uncertain_trust_advance_keeps_saved_baseline_private() {
    auto live = clean_mapless_guard();
    Fixture fixture{};
    fixture.source.advance_error =
        MapSelectorTrustedGenerationSourceError::io_failure;
    fixture.source.apply_on_error = true;

    const auto result = fixture.coordinator.establish(
        live, policy(), package());
    EXPECT(result.reason == MapSelectorTrustedBaselineReason::
                                trusted_advance_failed);
    EXPECT(result.reconciliation_required);
    EXPECT(result.baseline.committed());
    EXPECT(fixture.store.inspect().generation == 1);
    EXPECT(fixture.source.generation == 1);
    EXPECT(fixture.source.advance_calls == 1);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(live.status().reason ==
           MapActivationReason::selector_ambiguous);
}

void test_protected_conflict_after_save_requires_reconciliation() {
    auto live = clean_mapless_guard();
    Fixture fixture{};
    fixture.source.jump_on_read = 2;
    fixture.source.jump_to = 1;

    const auto result = fixture.coordinator.establish(
        live, policy(), package());
    EXPECT(result.reason == MapSelectorTrustedBaselineReason::
                                trusted_advance_failed);
    EXPECT(result.reconciliation_required);
    EXPECT(result.baseline.committed());
    EXPECT(result.trusted_after.reason ==
           MapSelectorTrustedGenerationReason::generation_conflict);
    EXPECT(fixture.store.inspect().generation == 1);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(live.status().reason ==
           MapActivationReason::selector_ambiguous);
}

void test_invalid_policy_failure_stays_private() {
    auto live = clean_mapless_guard();
    Fixture fixture{};
    auto wrong_policy = policy();
    wrong_policy.maximum_trial_boots = 2;

    const auto result = fixture.coordinator.establish(
        live, wrong_policy, package());
    EXPECT(result.reason ==
           MapSelectorTrustedBaselineReason::baseline_failed);
    EXPECT(result.baseline.reason ==
           MapSelectorBaselineReason::invalid_policy);
    EXPECT(fixture.storage.read_calls == 0);
    EXPECT(fixture.storage.write_calls == 0);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(!live.status().running);
    EXPECT(!result.map_exposure_allowed);
}

void test_nonbaseline_live_states_touch_no_backend() {
    MapActivationGuard stopped{};
    auto active = active_guard();
    auto unreadable = unreadable_mapless_guard();
    MapActivationGuard* guards[] = {&stopped, &active, &unreadable};

    for (auto* guard : guards) {
        Fixture fixture{};
        const auto result = fixture.coordinator.establish(
            *guard, policy(), package());
        EXPECT(result.reason == MapSelectorTrustedBaselineReason::
                                    live_state_not_clean_baseline);
        EXPECT(fixture.source.read_calls == 0);
        EXPECT(fixture.source.advance_calls == 0);
        EXPECT(fixture.storage.read_calls == 0);
        EXPECT(fixture.storage.write_calls == 0);
    }
}

}  // namespace

int main() {
    test_generation_one_save_advances_trust_before_publish();
    test_nonzero_trusted_history_blocks_selector_access();
    test_retryable_trust_failure_preserves_clean_first_use();
    test_latched_trust_failure_contains_clean_first_use();
    test_invalid_package_preserves_clean_mapless_state();
    test_existing_selector_never_becomes_first_use();
    test_selector_save_failure_never_advances_trust();
    test_uncertain_trust_advance_keeps_saved_baseline_private();
    test_protected_conflict_after_save_requires_reconciliation();
    test_invalid_policy_failure_stays_private();
    test_nonbaseline_live_states_touch_no_backend();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector trusted-baseline assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout
        << "PASS: 11 map selector trusted-baseline scenario groups\n";
    return EXIT_SUCCESS;
}
