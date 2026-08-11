#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_checkpoint.hpp"
#include "opentrail/map_selector_store.hpp"
#include "opentrail/map_selector_trusted_transition.hpp"

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

MapActivationGuard trial_guard() {
    auto guard = active_guard();
    EXPECT(guard.stage(candidate_package()) == MapActivationError::none);
    EXPECT(guard.mark_selector_committed(
               MapSlot::slot_b, 11, 100) ==
           MapActivationError::none);
    return guard;
}

MapActivationGuard promotable_trial_guard() {
    auto guard = trial_guard();
    EXPECT(guard.report_trial_read(true, 110) == MapActivationError::none);
    EXPECT(guard.report_trial_read(true, 120) == MapActivationError::none);
    return guard;
}

MapActivationGuard fallback_guard() {
    auto guard = trial_guard();
    EXPECT(guard.report_trial_read(false, 110) ==
           MapActivationError::trial_health_failed);
    return guard;
}

MapActivationGuard active_with_previous() {
    auto guard = promotable_trial_guard();
    EXPECT(guard.report_trial_read(true, 130) == MapActivationError::none);
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

class Fixture {
public:
    Fixture()
        : store(storage),
          transition(store),
          trusted(source),
          coordinator(transition, trusted) {}

    void seed_current(
        const MapActivationGuard& guard,
        std::uint64_t generation) {
        storage.seed(0, encode_guard(guard, generation));
    }

    FakeStorage storage{};
    MapSelectorStore store;
    MapSelectorTransitionCoordinator transition;
    FakeTrustedSource source{};
    MapSelectorTrustedGeneration trusted;
    MapSelectorTrustedTransitionCoordinator coordinator;
};

void test_volatile_transition_rechecks_exact_trust() {
    auto live = trial_guard();
    Fixture fixture{};
    fixture.seed_current(live, 1);
    fixture.source.generation = 1;

    const auto result = fixture.coordinator.report_trial_read(
        live, policy(), true, 110);
    EXPECT(result.protected_ordering_satisfied());
    EXPECT(result.transition.state ==
           MapSelectorTransitionState::applied_volatile);
    EXPECT(result.trusted_generation_before == 1);
    EXPECT(result.trusted_generation_after == 1);
    EXPECT(fixture.source.read_calls == 2);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(fixture.storage.write_calls == 0);
    EXPECT(live.status().state == MapActivationState::trial);
    EXPECT(live.status().healthy_trial_reads == 1);
}

void test_trial_promotion_advances_trust_before_publish() {
    auto live = promotable_trial_guard();
    Fixture fixture{};
    fixture.seed_current(live, 1);
    fixture.source.generation = 1;

    const auto result = fixture.coordinator.report_trial_read(
        live, policy(), true, 130);
    EXPECT(result.protected_ordering_satisfied());
    EXPECT(result.transition.committed());
    EXPECT(result.transition.active_generation == 2);
    EXPECT(result.trusted_generation_after == 2);
    EXPECT(fixture.source.observed_expected == 1);
    EXPECT(fixture.source.observed_requested == 2);
    EXPECT(fixture.storage.write_calls == 1);
    EXPECT(fixture.source.advance_calls == 1);
    EXPECT(live.status().state == MapActivationState::active);
    EXPECT(result.map_exposure_allowed);
}

void test_other_persistent_operations_advance_exact_trust() {
    auto deadline_live = trial_guard();
    Fixture deadline{};
    deadline.seed_current(deadline_live, 3);
    deadline.source.generation = 3;
    const auto expired = deadline.coordinator.tick(
        deadline_live, policy(), 600);
    EXPECT(expired.protected_ordering_satisfied());
    EXPECT(expired.transition.committed());
    EXPECT(deadline.source.generation == 4);
    EXPECT(deadline_live.status().state ==
           MapActivationState::fallback_required);

    auto fallback_live = fallback_guard();
    Fixture fallback{};
    fallback.seed_current(fallback_live, 5);
    fallback.source.generation = 5;
    const auto restored = fallback.coordinator.complete_fallback(
        fallback_live, policy(), package());
    EXPECT(restored.protected_ordering_satisfied());
    EXPECT(restored.transition.committed());
    EXPECT(fallback.source.generation == 6);
    EXPECT(fallback_live.status().state == MapActivationState::active);

    auto cleanup_live = active_with_previous();
    Fixture cleanup{};
    cleanup.seed_current(cleanup_live, 7);
    cleanup.source.generation = 7;
    const auto removed = cleanup.coordinator.mark_previous_removed(
        cleanup_live, policy(), MapSlot::slot_a, 10);
    EXPECT(removed.protected_ordering_satisfied());
    EXPECT(removed.transition.committed());
    EXPECT(cleanup.source.generation == 8);
    EXPECT(cleanup_live.status().previous_slot == MapSlot::none);
}

void test_rejected_operation_can_retain_exact_trusted_live_state() {
    auto live = active_guard();
    Fixture fixture{};
    fixture.seed_current(live, 2);
    fixture.source.generation = 2;

    const auto result = fixture.coordinator.mark_previous_removed(
        live, policy(), MapSlot::slot_a, 10);
    EXPECT(result.protected_ordering_satisfied());
    EXPECT(result.transition.state == MapSelectorTransitionState::rejected);
    EXPECT(result.transition.reason ==
           MapSelectorTransitionReason::guard_rejected);
    EXPECT(fixture.source.read_calls == 2);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(fixture.storage.write_calls == 0);
    EXPECT(live.status().state == MapActivationState::active);
    EXPECT(result.map_exposure_allowed);
}

void test_unavailable_initial_trust_blocks_store_and_contains_map() {
    auto live = active_guard();
    Fixture fixture{};
    fixture.seed_current(live, 1);
    fixture.source.generation = 1;
    fixture.source.fail_on_read = 1;

    const auto result = fixture.coordinator.tick(
        live, policy(), 200);
    EXPECT(!result.protected_ordering_satisfied());
    EXPECT(result.reason == MapSelectorTrustedTransitionReason::
                                trusted_source_unavailable);
    EXPECT(fixture.storage.read_calls == 0);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(result.live_guard_mapless);
    EXPECT(live.status().reason ==
           MapActivationReason::selector_unreadable);
}

void test_zero_or_stale_trust_cannot_label_live_selector_current() {
    auto zero_live = active_guard();
    Fixture zero{};
    zero.seed_current(zero_live, 1);
    const auto missing = zero.coordinator.mark_previous_removed(
        zero_live, policy(), MapSlot::slot_a, 10);
    EXPECT(missing.reason ==
           MapSelectorTrustedTransitionReason::transition_failed);
    EXPECT(missing.transition.reason ==
           MapSelectorTransitionReason::current_generation_mismatch);
    EXPECT(missing.reconciliation_required);
    EXPECT(zero_live.status().state == MapActivationState::mapless);

    auto stale_live = active_guard();
    Fixture stale{};
    stale.seed_current(stale_live, 1);
    stale.source.generation = 2;
    const auto rollback = stale.coordinator.mark_previous_removed(
        stale_live, policy(), MapSlot::slot_a, 10);
    EXPECT(rollback.reason ==
           MapSelectorTrustedTransitionReason::transition_failed);
    EXPECT(rollback.transition.reason ==
           MapSelectorTransitionReason::rollback_detected);
    EXPECT(rollback.reconciliation_required);
    EXPECT(stale_live.status().state == MapActivationState::mapless);
}

void test_live_selector_mismatch_remains_mapless_without_trust_advance() {
    auto live = trial_guard();
    Fixture fixture{};
    const auto persisted = active_guard();
    fixture.seed_current(persisted, 1);
    fixture.source.generation = 1;

    const auto result = fixture.coordinator.report_trial_read(
        live, policy(), true, 110);
    EXPECT(result.reason ==
           MapSelectorTrustedTransitionReason::transition_failed);
    EXPECT(result.transition.reason ==
           MapSelectorTransitionReason::live_checkpoint_mismatch);
    EXPECT(result.reconciliation_required);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(live.status().state == MapActivationState::mapless);
}

void test_uncertain_trust_advance_contains_newly_saved_transition() {
    auto live = promotable_trial_guard();
    Fixture fixture{};
    fixture.seed_current(live, 1);
    fixture.source.generation = 1;
    fixture.source.advance_error =
        MapSelectorTrustedGenerationSourceError::io_failure;
    fixture.source.apply_on_error = true;

    const auto result = fixture.coordinator.report_trial_read(
        live, policy(), true, 130);
    EXPECT(result.reason == MapSelectorTrustedTransitionReason::
                                trusted_advance_failed);
    EXPECT(result.reconciliation_required);
    EXPECT(result.transition.committed());
    EXPECT(fixture.storage.write_calls == 1);
    EXPECT(fixture.source.advance_calls == 1);
    EXPECT(fixture.source.generation == 2);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(result.live_guard_mapless);
    EXPECT(live.status().reason ==
           MapActivationReason::selector_ambiguous);
}

void test_final_trust_change_contains_volatile_transition() {
    auto live = trial_guard();
    Fixture fixture{};
    fixture.seed_current(live, 1);
    fixture.source.generation = 1;
    fixture.source.jump_on_read = 2;
    fixture.source.jump_to = 2;

    const auto result = fixture.coordinator.report_trial_read(
        live, policy(), true, 110);
    EXPECT(result.reason == MapSelectorTrustedTransitionReason::
                                trusted_source_changed);
    EXPECT(result.reconciliation_required);
    EXPECT(result.transition.state ==
           MapSelectorTransitionState::applied_volatile);
    EXPECT(fixture.storage.write_calls == 0);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(live.status().reason ==
           MapActivationReason::selector_ambiguous);
}

void test_invalid_fallback_clear_retains_protected_history_for_service() {
    auto live = fallback_guard();
    Fixture fixture{};
    fixture.seed_current(live, 1);
    fixture.source.generation = 1;
    auto wrong = package();
    wrong.generation = 99;

    const auto result = fixture.coordinator.complete_fallback(
        live, policy(), wrong);
    EXPECT(result.reason == MapSelectorTrustedTransitionReason::
                                protected_history_retained);
    EXPECT(result.reconciliation_required);
    EXPECT(result.transition.state ==
           MapSelectorTransitionState::mapless_committed);
    EXPECT(result.transition.checkpoint_cleared);
    EXPECT(fixture.storage.erase_calls == 2);
    EXPECT(fixture.store.inspect().error ==
           MapSelectorStoreError::no_checkpoint);
    EXPECT(fixture.source.generation == 1);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(live.status().state == MapActivationState::mapless);
    EXPECT(!result.map_exposure_allowed);
}

void test_nontransitionable_live_state_touches_no_backend() {
    MapActivationGuard stopped{};
    Fixture stopped_fixture{};
    const auto stopped_result = stopped_fixture.coordinator.tick(
        stopped, policy(), 200);
    EXPECT(stopped_result.reason ==
           MapSelectorTrustedTransitionReason::
               live_state_not_transitionable);
    EXPECT(stopped_fixture.source.read_calls == 0);
    EXPECT(stopped_fixture.storage.read_calls == 0);

    MapActivationGuard mapless{};
    EXPECT(mapless.start(
               policy(), {MapSelectorState::missing, {}}) ==
           MapActivationError::none);
    Fixture mapless_fixture{};
    const auto mapless_result = mapless_fixture.coordinator.tick(
        mapless, policy(), 200);
    EXPECT(mapless_result.reason ==
           MapSelectorTrustedTransitionReason::
               live_state_not_transitionable);
    EXPECT(mapless_fixture.source.read_calls == 0);
    EXPECT(mapless_fixture.storage.read_calls == 0);
    EXPECT(mapless.status().state == MapActivationState::mapless);
}

}  // namespace

int main() {
    test_volatile_transition_rechecks_exact_trust();
    test_trial_promotion_advances_trust_before_publish();
    test_other_persistent_operations_advance_exact_trust();
    test_rejected_operation_can_retain_exact_trusted_live_state();
    test_unavailable_initial_trust_blocks_store_and_contains_map();
    test_zero_or_stale_trust_cannot_label_live_selector_current();
    test_live_selector_mismatch_remains_mapless_without_trust_advance();
    test_uncertain_trust_advance_contains_newly_saved_transition();
    test_final_trust_change_contains_volatile_transition();
    test_invalid_fallback_clear_retains_protected_history_for_service();
    test_nontransitionable_live_state_touches_no_backend();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector trusted-transition assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout
        << "PASS: 11 map selector trusted-transition scenario groups\n";
    return EXIT_SUCCESS;
}
