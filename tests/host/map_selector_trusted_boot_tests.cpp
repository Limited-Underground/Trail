#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_checkpoint.hpp"
#include "opentrail/map_selector_store.hpp"
#include "opentrail/map_selector_trusted_boot.hpp"

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

MapSelectorTrustedBootResult boot(
    FakeStorage& storage,
    FakeTrustedSource& source,
    MapActivationGuard& live,
    const MapPackageEvidence& selected = package(),
    const MapPackageEvidence& previous = {}) {
    MapSelectorStore store{storage};
    MapSelectorBootCoordinator selector_boot{store};
    MapSelectorTrustedGeneration trusted{source};
    MapSelectorTrustedBootCoordinator coordinator{
        selector_boot, trusted};
    return coordinator.boot(
        policy(), selected, previous, 200, live);
}

void test_exact_active_generation_is_rechecked_before_publish() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(active_guard(), 4));
    FakeTrustedSource source{};
    source.generation = 4;
    MapActivationGuard live{};

    const auto result = boot(storage, source, live);
    EXPECT(result.operational());
    EXPECT(result.reason == MapSelectorTrustedBootReason::none);
    EXPECT(result.map_exposure_allowed);
    EXPECT(result.trusted_generation_before == 4);
    EXPECT(result.trusted_generation_after == 4);
    EXPECT(source.read_calls == 2);
    EXPECT(source.advance_calls == 0);
    EXPECT(storage.write_calls == 0);
    EXPECT(live.status().state == MapActivationState::active);
}

void test_trial_save_advances_trust_before_publish() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(trial_guard(), 1));
    FakeTrustedSource source{};
    source.generation = 1;
    MapActivationGuard live{};

    const auto result = boot(
        storage, source, live, candidate_package(), package());
    EXPECT(result.operational());
    EXPECT(result.selector.state == MapSelectorBootState::trial_ready);
    EXPECT(result.selector.save.saved());
    EXPECT(result.selector.active_generation == 2);
    EXPECT(result.trusted_generation_after == 2);
    EXPECT(source.observed_expected == 1);
    EXPECT(source.observed_requested == 2);
    EXPECT(source.advance_calls == 1);
    EXPECT(storage.write_calls == 1);
    EXPECT(storage.commit_calls == 1);
    EXPECT(live.status().state == MapActivationState::trial);
    EXPECT(live.status().trial_boots == 2);
}

void test_valid_store_ahead_of_trust_is_caught_up_before_release() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(active_guard(), 5));
    FakeTrustedSource source{};
    source.generation = 4;
    MapActivationGuard live{};

    const auto result = boot(storage, source, live);
    EXPECT(result.operational());
    EXPECT(result.trusted_generation_before == 4);
    EXPECT(result.trusted_generation_after == 5);
    EXPECT(source.advance_calls == 1);
    EXPECT(source.generation == 5);
    EXPECT(storage.write_calls == 0);
}

void test_zero_trust_and_empty_store_publish_normal_mapless_state() {
    FakeStorage storage{};
    FakeTrustedSource source{};
    MapActivationGuard live{};

    const auto result = boot(storage, source, live);
    EXPECT(result.operational());
    EXPECT(result.selector.state == MapSelectorBootState::mapless_ready);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(result.trusted_generation_after == 0);
    EXPECT(source.read_calls == 2);
    EXPECT(live.status().state == MapActivationState::mapless);
    EXPECT(live.status().reason == MapActivationReason::no_selector);
}

void test_trusted_history_with_empty_store_requires_reconciliation() {
    FakeStorage storage{};
    FakeTrustedSource source{};
    source.generation = 3;
    MapActivationGuard live{};

    const auto result = boot(storage, source, live);
    EXPECT(!result.operational());
    EXPECT(result.reason ==
           MapSelectorTrustedBootReason::trusted_history_missing);
    EXPECT(result.reconciliation_required);
    EXPECT(!result.live_guard_published);
    EXPECT(!live.status().running);
    EXPECT(source.advance_calls == 0);
}

void test_unavailable_trust_blocks_selector_storage_access() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(active_guard(), 4));
    FakeTrustedSource source{};
    source.fail_on_read = 1;
    MapActivationGuard live{};

    const auto result = boot(storage, source, live);
    EXPECT(!result.operational());
    EXPECT(result.reason ==
           MapSelectorTrustedBootReason::trusted_source_unavailable);
    EXPECT(!result.reconciliation_required);
    EXPECT(storage.read_calls == 0);
    EXPECT(!live.status().running);
}

void test_selector_rollback_publishes_only_service_mapless_state() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(active_guard(), 2));
    FakeTrustedSource source{};
    source.generation = 3;
    MapActivationGuard live{};

    const auto result = boot(storage, source, live);
    EXPECT(!result.operational());
    EXPECT(result.reason ==
           MapSelectorTrustedBootReason::selector_boot_failed);
    EXPECT(result.selector.reason ==
           MapSelectorBootReason::rollback_detected);
    EXPECT(result.reconciliation_required);
    EXPECT(result.live_guard_published);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(live.status().state == MapActivationState::mapless);
    EXPECT(live.status().reason ==
           MapActivationReason::selector_ambiguous);
}

void test_uncertain_trust_advance_keeps_saved_selector_private() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(trial_guard(), 1));
    FakeTrustedSource source{};
    source.generation = 1;
    source.advance_error =
        MapSelectorTrustedGenerationSourceError::io_failure;
    source.apply_on_error = true;
    MapActivationGuard live{};

    const auto result = boot(
        storage, source, live, candidate_package(), package());
    EXPECT(!result.operational());
    EXPECT(result.reason ==
           MapSelectorTrustedBootReason::trusted_advance_failed);
    EXPECT(result.reconciliation_required);
    EXPECT(result.selector.save.saved());
    EXPECT(storage.write_calls == 1);
    EXPECT(source.advance_calls == 1);
    EXPECT(source.generation == 2);
    EXPECT(!result.live_guard_published);
    EXPECT(!live.status().running);
}

void test_final_trust_recheck_change_blocks_unchanged_selector() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(active_guard(), 4));
    FakeTrustedSource source{};
    source.generation = 4;
    source.jump_on_read = 2;
    source.jump_to = 5;
    MapActivationGuard live{};

    const auto result = boot(storage, source, live);
    EXPECT(!result.operational());
    EXPECT(result.reason ==
           MapSelectorTrustedBootReason::trusted_source_changed);
    EXPECT(result.reconciliation_required);
    EXPECT(result.trusted_generation_after == 5);
    EXPECT(source.advance_calls == 0);
    EXPECT(!live.status().running);
}

void test_dirty_live_owner_prevents_all_backend_access() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(active_guard(), 4));
    FakeTrustedSource source{};
    source.generation = 4;
    auto live = active_guard();

    const auto result = boot(storage, source, live);
    EXPECT(!result.operational());
    EXPECT(result.reason ==
           MapSelectorTrustedBootReason::live_guard_not_clean);
    EXPECT(source.read_calls == 0);
    EXPECT(storage.read_calls == 0);
    EXPECT(live.status().state == MapActivationState::active);
}

}  // namespace

int main() {
    test_exact_active_generation_is_rechecked_before_publish();
    test_trial_save_advances_trust_before_publish();
    test_valid_store_ahead_of_trust_is_caught_up_before_release();
    test_zero_trust_and_empty_store_publish_normal_mapless_state();
    test_trusted_history_with_empty_store_requires_reconciliation();
    test_unavailable_trust_blocks_selector_storage_access();
    test_selector_rollback_publishes_only_service_mapless_state();
    test_uncertain_trust_advance_keeps_saved_selector_private();
    test_final_trust_recheck_change_blocks_unchanged_selector();
    test_dirty_live_owner_prevents_all_backend_access();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector trusted-boot assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 map selector trusted-boot scenario groups\n";
    return EXIT_SUCCESS;
}
