#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_checkpoint.hpp"
#include "opentrail/map_selector_domain_boot.hpp"
#include "opentrail/map_selector_domain_record.hpp"
#include "opentrail/map_selector_domain_store.hpp"
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

MapSelectorDomainId domain(std::uint8_t seed = 1) {
    MapSelectorDomainId value{};
    for (std::size_t index = 0; index < value.size(); ++index) {
        value[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

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

MapSelectorDomainRecord active_fresh() {
    return {
        kMapSelectorDomainRecordVersion,
        MapSelectorDomainRecordState::active,
        MapSelectorDomainRecordOrigin::fresh_device_commissioning,
        domain(41),
        {},
        0,
        1,
        1,
        2};
}

MapSelectorDomainRecord active_replacement() {
    return {
        kMapSelectorDomainRecordVersion,
        MapSelectorDomainRecordState::active,
        MapSelectorDomainRecordOrigin::same_device_replacement,
        domain(61),
        domain(21),
        40,
        41,
        2,
        9};
}

MapSelectorDomainRecord prior_pending(
    const MapSelectorDomainRecord& active) {
    auto pending = active;
    pending.state =
        active.origin ==
                MapSelectorDomainRecordOrigin::fresh_device_commissioning
            ? MapSelectorDomainRecordState::pending_first_baseline
            : MapSelectorDomainRecordState::pending_selector_reseed;
    pending.accepted_selector_generation = 0;
    --pending.record_generation;
    return pending;
}

std::array<std::uint8_t, kMapSelectorDomainRecordBytes> domain_bytes(
    const MapSelectorDomainRecord& record) {
    std::array<std::uint8_t, kMapSelectorDomainRecordBytes> bytes{};
    EXPECT(encode_map_selector_domain_record(
               record, bytes.data(), bytes.size())
               .succeeded());
    return bytes;
}

std::array<std::uint8_t, kMapSelectorCheckpointBytes> stable_selector_bytes(
    const MapPackageEvidence& selected,
    std::uint64_t record_generation) {
    MapActivationGuard guard{};
    EXPECT(guard.start(policy(), {MapSelectorState::valid, selected}) ==
           MapActivationError::none);
    MapSelectorCheckpoint checkpoint{};
    EXPECT(guard.export_checkpoint(record_generation, checkpoint) ==
           MapActivationError::none);
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> bytes{};
    EXPECT(encode_map_selector_checkpoint(
               checkpoint, bytes.data(), bytes.size())
               .succeeded());
    return bytes;
}

std::array<std::uint8_t, kMapSelectorCheckpointBytes> trial_selector_bytes(
    std::uint64_t record_generation) {
    const auto previous = package(MapSlot::slot_a, 10);
    const auto selected = package(MapSlot::slot_b, 20);
    MapActivationGuard guard{};
    EXPECT(guard.start(policy(), {MapSelectorState::valid, previous}) ==
           MapActivationError::none);
    EXPECT(guard.stage(selected) == MapActivationError::none);
    EXPECT(guard.mark_selector_committed(
               selected.slot, selected.generation, 100) ==
           MapActivationError::none);
    MapSelectorCheckpoint checkpoint{};
    EXPECT(guard.export_checkpoint(record_generation, checkpoint) ==
           MapActivationError::none);
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> bytes{};
    EXPECT(encode_map_selector_checkpoint(
               checkpoint, bytes.data(), bytes.size())
               .succeeded());
    return bytes;
}

std::array<std::uint8_t, kMapSelectorCheckpointBytes>
active_cleanup_selector_bytes(std::uint64_t record_generation) {
    const auto previous = package(MapSlot::slot_a, 10);
    const auto selected = package(MapSlot::slot_b, 20);
    MapActivationGuard guard{};
    EXPECT(guard.start(policy(), {MapSelectorState::valid, previous}) ==
           MapActivationError::none);
    EXPECT(guard.stage(selected) == MapActivationError::none);
    EXPECT(guard.mark_selector_committed(
               selected.slot, selected.generation, 100) ==
           MapActivationError::none);
    EXPECT(guard.report_trial_read(true, 101) == MapActivationError::none);
    EXPECT(guard.report_trial_read(true, 102) == MapActivationError::none);
    EXPECT(guard.report_trial_read(true, 103) == MapActivationError::none);
    MapSelectorCheckpoint checkpoint{};
    EXPECT(guard.export_checkpoint(record_generation, checkpoint) ==
           MapActivationError::none);
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> bytes{};
    EXPECT(encode_map_selector_checkpoint(
               checkpoint, bytes.data(), bytes.size())
               .succeeded());
    return bytes;
}

class FakeDomainStorage final : public MapSelectorDomainStorage {
public:
    MapSelectorDomainStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override {
        ++read_calls;
        if (replace_on_read_call != 0 &&
            read_calls == replace_on_read_call) {
            slots = replacement_slots;
            present = replacement_present;
        }
        if (slot >= slots.size() || output == nullptr ||
            size != kMapSelectorDomainRecordBytes) {
            return MapSelectorDomainStorageError::invalid_argument;
        }
        if (fail_read_call == read_calls) {
            return MapSelectorDomainStorageError::io_failure;
        }
        if (!present[slot]) {
            return MapSelectorDomainStorageError::not_found;
        }
        std::copy(slots[slot].begin(), slots[slot].end(), output);
        return MapSelectorDomainStorageError::none;
    }

    MapSelectorDomainStorageError write_slot(
        std::uint8_t,
        const std::uint8_t*,
        std::size_t) override {
        ++write_calls;
        return MapSelectorDomainStorageError::io_failure;
    }

    MapSelectorDomainStorageError commit_slot(
        std::uint8_t,
        std::size_t,
        std::uint8_t) override {
        ++commit_calls;
        return MapSelectorDomainStorageError::io_failure;
    }

    void seed(
        std::uint8_t slot,
        const std::array<
            std::uint8_t,
            kMapSelectorDomainRecordBytes>& bytes) {
        slots[slot] = bytes;
        present[slot] = true;
    }

    std::array<
        std::array<std::uint8_t, kMapSelectorDomainRecordBytes>,
        kMapSelectorDomainSlotCount>
        slots{};
    std::array<bool, kMapSelectorDomainSlotCount> present{};
    std::array<
        std::array<std::uint8_t, kMapSelectorDomainRecordBytes>,
        kMapSelectorDomainSlotCount>
        replacement_slots{};
    std::array<bool, kMapSelectorDomainSlotCount> replacement_present{};
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t commit_calls{0};
    std::uint32_t fail_read_call{0};
    std::uint32_t replace_on_read_call{0};
};

class FakeSelectorStorage final : public MapSelectorStorage {
public:
    MapSelectorStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override {
        ++read_calls;
        if (replace_on_read_call != 0 &&
            read_calls == replace_on_read_call) {
            slots = replacement_slots;
            present = replacement_present;
        }
        if (slot >= slots.size() || output == nullptr ||
            size != kMapSelectorCheckpointBytes) {
            return MapSelectorStorageError::invalid_argument;
        }
        if (fail_read_call == read_calls) {
            return MapSelectorStorageError::io_failure;
        }
        if (!present[slot]) {
            return MapSelectorStorageError::not_found;
        }
        std::copy(slots[slot].begin(), slots[slot].end(), output);
        return MapSelectorStorageError::none;
    }

    MapSelectorStorageError write_slot(
        std::uint8_t,
        const std::uint8_t*,
        std::size_t) override {
        ++write_calls;
        return MapSelectorStorageError::io_failure;
    }

    MapSelectorStorageError commit_slot(
        std::uint8_t,
        std::size_t,
        std::uint8_t) override {
        ++commit_calls;
        return MapSelectorStorageError::io_failure;
    }

    MapSelectorStorageError erase_slot(std::uint8_t) override {
        ++erase_calls;
        return MapSelectorStorageError::io_failure;
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
    std::array<std::array<std::uint8_t, kMapSelectorCheckpointBytes>, 2>
        replacement_slots{};
    std::array<bool, 2> replacement_present{};
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t commit_calls{0};
    std::uint32_t erase_calls{0};
    std::uint32_t fail_read_call{0};
    std::uint32_t replace_on_read_call{0};
};

class FakeProtectedSource final : public MapSelectorDomainProtectedSource {
public:
    MapSelectorDomainProtectedSourceRead read() override {
        ++read_calls;
        if (fail_read_call == read_calls) {
            return {
                error,
                MapSelectorDomainProtectedSourceState::unknown,
                {},
                0};
        }
        if (change_on_read_call == read_calls) {
            current_domain = changed_domain;
            selector_generation = changed_generation;
        }
        return {
            MapSelectorDomainProtectedSourceError::none,
            state,
            current_domain,
            selector_generation};
    }

    MapSelectorDomainProtectedSourceError establish_fresh_domain(
        const MapSelectorDomainProtectedEstablishRequest&) override {
        ++establish_calls;
        return MapSelectorDomainProtectedSourceError::rejected;
    }

    MapSelectorDomainProtectedSourceError advance_selector_generation(
        const MapSelectorDomainProtectedAdvanceRequest&) override {
        ++advance_calls;
        return MapSelectorDomainProtectedSourceError::rejected;
    }

    MapSelectorDomainProtectedSourceState state{
        MapSelectorDomainProtectedSourceState::ready};
    MapSelectorDomainId current_domain{domain(41)};
    std::uint64_t selector_generation{1};
    MapSelectorDomainProtectedSourceError error{
        MapSelectorDomainProtectedSourceError::io_failure};
    std::uint32_t read_calls{0};
    std::uint32_t establish_calls{0};
    std::uint32_t advance_calls{0};
    std::uint32_t fail_read_call{0};
    std::uint32_t change_on_read_call{0};
    MapSelectorDomainId changed_domain{domain(41)};
    std::uint64_t changed_generation{1};
};

struct Fixture {
    FakeDomainStorage domain_storage{};
    FakeSelectorStorage selector_storage{};
    FakeProtectedSource source{};
    MapSelectorDomainStore domain_store{domain_storage};
    MapSelectorStore selector_store{selector_storage};
    MapSelectorDomainBootCoordinator coordinator{
        domain_store, selector_store, source};

    void seed_domain(
        const MapSelectorDomainRecord& active,
        bool include_prior = true) {
        if (include_prior) {
            domain_storage.seed(0, domain_bytes(prior_pending(active)));
        }
        domain_storage.seed(1, domain_bytes(active));
        source.current_domain = active.current_domain;
        source.selector_generation =
            active.accepted_selector_generation;
    }

    void seed_selector(
        const MapPackageEvidence& selected,
        std::uint64_t generation,
        bool both_slots = true) {
        const auto bytes = stable_selector_bytes(selected, generation);
        selector_storage.seed(0, bytes);
        if (both_slots) {
            selector_storage.seed(1, bytes);
        }
    }
};

void expect_no_mutation(const Fixture& fixture) {
    EXPECT(fixture.domain_storage.write_calls == 0);
    EXPECT(fixture.domain_storage.commit_calls == 0);
    EXPECT(fixture.selector_storage.write_calls == 0);
    EXPECT(fixture.selector_storage.commit_calls == 0);
    EXPECT(fixture.selector_storage.erase_calls == 0);
    EXPECT(fixture.source.establish_calls == 0);
    EXPECT(fixture.source.advance_calls == 0);
}

void test_exact_fresh_active_boot() {
    Fixture fixture{};
    const auto record = active_fresh();
    const auto selected = package();
    fixture.seed_domain(record);
    fixture.seed_selector(selected, 1);
    MapActivationGuard live{};

    const auto result = fixture.coordinator.boot(
        policy(), selected, live);

    EXPECT(result.operational());
    EXPECT(result.selector_generation == 1);
    EXPECT(result.domain_record_generation == 2);
    EXPECT(result.domain_epoch == 1);
    EXPECT(result.domain_verified);
    EXPECT(result.selector_restored);
    EXPECT(result.selector_verified);
    EXPECT(result.protected_source_verified);
    EXPECT(live.status().state == MapActivationState::active);
    EXPECT(live.status().map_available);
    EXPECT(fixture.domain_storage.read_calls == 4);
    EXPECT(fixture.selector_storage.read_calls == 4);
    EXPECT(fixture.source.read_calls == 2);
    expect_no_mutation(fixture);
}

void test_replacement_floor_boot() {
    Fixture fixture{};
    const auto record = active_replacement();
    const auto selected = package(MapSlot::slot_b, 90);
    fixture.seed_domain(record);
    fixture.seed_selector(selected, 41);
    MapActivationGuard live{};

    const auto result = fixture.coordinator.boot(
        policy(), selected, live);

    EXPECT(result.operational());
    EXPECT(result.selector_generation == 41);
    EXPECT(result.domain_epoch == 2);
    EXPECT(live.status().active_slot == MapSlot::slot_b);
    EXPECT(live.status().active_generation == 90);
    expect_no_mutation(fixture);
}

void test_dirty_live_owner_isolated() {
    Fixture fixture{};
    fixture.seed_domain(active_fresh());
    fixture.seed_selector(package(), 1);
    MapActivationGuard live{};
    EXPECT(live.start(policy(), {MapSelectorState::valid, package()}) ==
           MapActivationError::none);

    const auto result = fixture.coordinator.boot(
        policy(), package(), live);

    EXPECT(result.state == MapSelectorDomainBootState::rejected);
    EXPECT(result.reason ==
           MapSelectorDomainBootReason::live_guard_not_clean);
    EXPECT(fixture.domain_storage.read_calls == 0);
    EXPECT(fixture.selector_storage.read_calls == 0);
    EXPECT(fixture.source.read_calls == 0);
    EXPECT(live.status().map_available);
    expect_no_mutation(fixture);
}

void test_invalid_input_isolation() {
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(package(), 1);
        auto invalid_policy = policy();
        invalid_policy.maximum_package_bytes = 0;
        MapActivationGuard live{};
        const auto result = fixture.coordinator.boot(
            invalid_policy, package(), live);
        EXPECT(result.state == MapSelectorDomainBootState::rejected);
        EXPECT(result.reason == MapSelectorDomainBootReason::invalid_policy);
        EXPECT(fixture.domain_storage.read_calls == 0);
        EXPECT(fixture.selector_storage.read_calls == 0);
        EXPECT(fixture.source.read_calls == 0);
        expect_no_mutation(fixture);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(package(), 1);
        auto invalid_package = package();
        invalid_package.integrity_verified = false;
        MapActivationGuard live{};
        const auto result = fixture.coordinator.boot(
            policy(), invalid_package, live);
        EXPECT(result.state ==
               MapSelectorDomainBootState::service_required);
        EXPECT(result.reason ==
               MapSelectorDomainBootReason::package_rejected);
        EXPECT(result.live_guard_published);
        EXPECT(!result.map_exposure_allowed);
        EXPECT(fixture.domain_storage.read_calls == 0);
        expect_no_mutation(fixture);
    }
}

void test_domain_state_failures() {
    {
        Fixture fixture{};
        fixture.seed_selector(package(), 1);
        MapActivationGuard live{};
        const auto result = fixture.coordinator.boot(
            policy(), package(), live);
        EXPECT(result.state ==
               MapSelectorDomainBootState::service_required);
        EXPECT(result.reason ==
               MapSelectorDomainBootReason::domain_not_active);
        EXPECT(fixture.source.read_calls == 0);
        EXPECT(fixture.selector_storage.read_calls == 0);
        expect_no_mutation(fixture);
    }
    {
        Fixture fixture{};
        const auto pending = prior_pending(active_fresh());
        fixture.domain_storage.seed(0, domain_bytes(pending));
        fixture.seed_selector(package(), 1);
        MapActivationGuard live{};
        const auto result = fixture.coordinator.boot(
            policy(), package(), live);
        EXPECT(result.state ==
               MapSelectorDomainBootState::reconciliation_required);
        EXPECT(result.reason ==
               MapSelectorDomainBootReason::domain_not_active);
        EXPECT(fixture.source.read_calls == 0);
        expect_no_mutation(fixture);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.domain_storage.fail_read_call = 1;
        fixture.seed_selector(package(), 1);
        MapActivationGuard live{};
        const auto result = fixture.coordinator.boot(
            policy(), package(), live);
        EXPECT(result.reason ==
               MapSelectorDomainBootReason::domain_storage_unavailable);
        EXPECT(fixture.source.read_calls == 0);
        expect_no_mutation(fixture);
    }
}

void test_protected_source_preflight() {
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(package(), 1);
        fixture.source.fail_read_call = 1;
        MapActivationGuard live{};
        const auto result = fixture.coordinator.boot(
            policy(), package(), live);
        EXPECT(result.reason ==
               MapSelectorDomainBootReason::protected_source_unavailable);
        EXPECT(fixture.selector_storage.read_calls == 0);
        expect_no_mutation(fixture);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(package(), 1);
        fixture.source.current_domain = domain(99);
        MapActivationGuard live{};
        const auto result = fixture.coordinator.boot(
            policy(), package(), live);
        EXPECT(result.reason ==
               MapSelectorDomainBootReason::protected_domain_mismatch);
        EXPECT(result.reconciliation_required);
        EXPECT(fixture.selector_storage.read_calls == 0);
        expect_no_mutation(fixture);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(package(), 1);
        fixture.source.selector_generation = 2;
        MapActivationGuard live{};
        const auto result = fixture.coordinator.boot(
            policy(), package(), live);
        EXPECT(result.reason ==
               MapSelectorDomainBootReason::protected_generation_mismatch);
        EXPECT(fixture.selector_storage.read_calls == 0);
        expect_no_mutation(fixture);
    }
}

void test_selector_state_failures() {
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        MapActivationGuard live{};
        const auto result = fixture.coordinator.boot(
            policy(), package(), live);
        EXPECT(result.reason ==
               MapSelectorDomainBootReason::selector_restore_failed);
        EXPECT(result.reconciliation_required);
        expect_no_mutation(fixture);
    }
    {
        Fixture fixture{};
        auto record = active_fresh();
        record.accepted_selector_generation = 2;
        fixture.seed_domain(record);
        fixture.seed_selector(package(), 1);
        MapActivationGuard live{};
        const auto result = fixture.coordinator.boot(
            policy(), package(), live);
        EXPECT(result.reason ==
               MapSelectorDomainBootReason::selector_restore_failed);
        EXPECT(result.selector_load.error ==
               MapSelectorStoreError::generation_below_floor);
        expect_no_mutation(fixture);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(package(), 2);
        MapActivationGuard live{};
        const auto result = fixture.coordinator.boot(
            policy(), package(), live);
        EXPECT(result.reason ==
               MapSelectorDomainBootReason::selector_generation_mismatch);
        EXPECT(result.reconciliation_required);
        expect_no_mutation(fixture);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(package(), 1);
        fixture.selector_storage.fail_read_call = 1;
        MapActivationGuard live{};
        const auto result = fixture.coordinator.boot(
            policy(), package(), live);
        EXPECT(result.reason ==
               MapSelectorDomainBootReason::selector_storage_unavailable);
        expect_no_mutation(fixture);
    }
}

void test_trial_checkpoint_is_not_stable_boot() {
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        const auto trial = trial_selector_bytes(1);
        fixture.selector_storage.seed(0, trial);
        fixture.selector_storage.seed(1, trial);
        MapActivationGuard live{};

        const auto result = fixture.coordinator.boot(
            policy(), package(MapSlot::slot_b, 20), live);

        EXPECT(result.reason ==
               MapSelectorDomainBootReason::selector_restore_failed);
        EXPECT(result.reconciliation_required);
        EXPECT(!result.map_exposure_allowed);
        expect_no_mutation(fixture);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        const auto cleanup = active_cleanup_selector_bytes(1);
        fixture.selector_storage.seed(0, cleanup);
        fixture.selector_storage.seed(1, cleanup);
        MapActivationGuard live{};

        const auto result = fixture.coordinator.boot(
            policy(), package(MapSlot::slot_b, 20), live);

        EXPECT(result.reason ==
               MapSelectorDomainBootReason::
                   selector_verification_failed);
        EXPECT(result.reconciliation_required);
        EXPECT(!result.map_exposure_allowed);
        expect_no_mutation(fixture);
    }
}

void test_domain_change_after_private_restore() {
    Fixture fixture{};
    const auto record = active_fresh();
    fixture.seed_domain(record);
    fixture.seed_selector(package(), 1);
    auto changed = record;
    changed.record_generation = 3;
    fixture.domain_storage.replacement_slots[0] = domain_bytes(changed);
    fixture.domain_storage.replacement_slots[1] = domain_bytes(changed);
    fixture.domain_storage.replacement_present = {true, true};
    fixture.domain_storage.replace_on_read_call = 3;
    MapActivationGuard live{};

    const auto result = fixture.coordinator.boot(
        policy(), package(), live);

    EXPECT(result.reason == MapSelectorDomainBootReason::domain_changed);
    EXPECT(result.reconciliation_required);
    EXPECT(result.selector_restored);
    EXPECT(!result.map_exposure_allowed);
    expect_no_mutation(fixture);
}

void test_selector_change_before_verification() {
    Fixture fixture{};
    fixture.seed_domain(active_fresh());
    fixture.seed_selector(package(), 1);
    const auto changed = stable_selector_bytes(package(), 2);
    fixture.selector_storage.replacement_slots[0] = changed;
    fixture.selector_storage.replacement_slots[1] = changed;
    fixture.selector_storage.replacement_present = {true, true};
    fixture.selector_storage.replace_on_read_call = 3;
    MapActivationGuard live{};

    const auto result = fixture.coordinator.boot(
        policy(), package(), live);

    EXPECT(result.reason ==
           MapSelectorDomainBootReason::selector_verification_failed);
    EXPECT(result.reconciliation_required);
    EXPECT(result.domain_verified);
    EXPECT(!result.map_exposure_allowed);
    expect_no_mutation(fixture);
}

void test_source_change_before_publication() {
    Fixture fixture{};
    fixture.seed_domain(active_fresh());
    fixture.seed_selector(package(), 1);
    fixture.source.change_on_read_call = 2;
    fixture.source.changed_generation = 2;
    MapActivationGuard live{};

    const auto result = fixture.coordinator.boot(
        policy(), package(), live);

    EXPECT(result.reason ==
           MapSelectorDomainBootReason::final_source_changed);
    EXPECT(result.reconciliation_required);
    EXPECT(result.domain_verified);
    EXPECT(result.selector_verified);
    EXPECT(!result.map_exposure_allowed);
    expect_no_mutation(fixture);
}

void test_degraded_exact_records_remain_bootable() {
    Fixture fixture{};
    const auto record = active_fresh();
    fixture.seed_domain(record, false);
    fixture.seed_selector(package(), 1, false);
    MapActivationGuard live{};

    const auto result = fixture.coordinator.boot(
        policy(), package(), live);

    EXPECT(result.operational());
    EXPECT(result.domain_repair_required);
    EXPECT(result.selector_repair_required);
    EXPECT(live.status().map_available);
    expect_no_mutation(fixture);
}

void test_second_read_failures_stay_mapless() {
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(package(), 1);
        fixture.domain_storage.fail_read_call = 3;
        MapActivationGuard live{};
        const auto result = fixture.coordinator.boot(
            policy(), package(), live);
        EXPECT(result.reason ==
               MapSelectorDomainBootReason::domain_storage_unavailable);
        EXPECT(result.selector_restored);
        EXPECT(!result.map_exposure_allowed);
        expect_no_mutation(fixture);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(package(), 1);
        fixture.source.fail_read_call = 2;
        MapActivationGuard live{};
        const auto result = fixture.coordinator.boot(
            policy(), package(), live);
        EXPECT(result.reason ==
               MapSelectorDomainBootReason::protected_source_unavailable);
        EXPECT(result.selector_verified);
        EXPECT(!result.map_exposure_allowed);
        expect_no_mutation(fixture);
    }
}

}  // namespace

int main() {
    test_exact_fresh_active_boot();
    test_replacement_floor_boot();
    test_dirty_live_owner_isolated();
    test_invalid_input_isolation();
    test_domain_state_failures();
    test_protected_source_preflight();
    test_selector_state_failures();
    test_trial_checkpoint_is_not_stable_boot();
    test_domain_change_after_private_restore();
    test_selector_change_before_verification();
    test_source_change_before_publication();
    test_degraded_exact_records_remain_bootable();
    test_second_read_failures_stay_mapless();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 13 map selector domain-boot scenario groups\n";
    return EXIT_SUCCESS;
}
