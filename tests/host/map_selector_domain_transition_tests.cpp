#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_checkpoint.hpp"
#include "opentrail/map_selector_domain_record.hpp"
#include "opentrail/map_selector_domain_store.hpp"
#include "opentrail/map_selector_domain_transition.hpp"
#include "opentrail/map_selector_domain_trial_boot.hpp"
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

struct MutationOrder {
    std::uint32_t next{0};
    std::uint32_t selector_commit{0};
    std::uint32_t protected_advance{0};
    std::uint32_t domain_commit{0};

    std::uint32_t mark() {
        return ++next;
    }
};

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

MapPackageEvidence candidate_package() {
    return package(MapSlot::slot_b, 20);
}

MapSelectorDomainRecord active_record(
    std::uint64_t accepted_generation,
    std::uint64_t record_generation) {
    return {
        kMapSelectorDomainRecordVersion,
        MapSelectorDomainRecordState::active,
        MapSelectorDomainRecordOrigin::fresh_device_commissioning,
        domain(41),
        {},
        0,
        accepted_generation,
        1,
        record_generation};
}

MapSelectorDomainRecord prior_record(
    const MapSelectorDomainRecord& active) {
    auto prior = active;
    prior.state =
        MapSelectorDomainRecordState::pending_first_baseline;
    prior.accepted_selector_generation = 0;
    --prior.record_generation;
    return prior;
}

std::array<std::uint8_t, kMapSelectorDomainRecordBytes> domain_bytes(
    const MapSelectorDomainRecord& record) {
    std::array<std::uint8_t, kMapSelectorDomainRecordBytes> bytes{};
    EXPECT(encode_map_selector_domain_record(
               record, bytes.data(), bytes.size())
               .succeeded());
    return bytes;
}

std::array<std::uint8_t, kMapSelectorCheckpointBytes> selector_bytes(
    const MapActivationGuard& guard,
    std::uint64_t record_generation) {
    MapSelectorCheckpoint checkpoint{};
    EXPECT(guard.export_checkpoint(record_generation, checkpoint) ==
           MapActivationError::none);
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> bytes{};
    EXPECT(encode_map_selector_checkpoint(
               checkpoint, bytes.data(), bytes.size())
               .succeeded());
    return bytes;
}

class MemoryDomainStorage final : public MapSelectorDomainStorage {
public:
    MapSelectorDomainStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override {
        ++read_calls;
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
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) override {
        ++write_calls;
        if (slot >= slots.size() || data == nullptr ||
            size != kMapSelectorDomainRecordBytes) {
            return MapSelectorDomainStorageError::invalid_argument;
        }
        if (fail_write) {
            return MapSelectorDomainStorageError::io_failure;
        }
        std::copy(data, data + size, slots[slot].begin());
        present[slot] = true;
        return MapSelectorDomainStorageError::none;
    }

    MapSelectorDomainStorageError commit_slot(
        std::uint8_t slot,
        std::size_t offset,
        std::uint8_t value) override {
        ++commit_calls;
        if (order != nullptr && order->domain_commit == 0) {
            order->domain_commit = order->mark();
        }
        if (slot >= slots.size() || !present[slot] ||
            offset != kMapSelectorDomainRecordCommitOffset ||
            value != kMapSelectorDomainRecordCommitMarker) {
            return MapSelectorDomainStorageError::invalid_argument;
        }
        if (fail_commit) {
            if (commit_then_fail) {
                slots[slot][offset] = value;
            }
            return MapSelectorDomainStorageError::io_failure;
        }
        slots[slot][offset] = value;
        return MapSelectorDomainStorageError::none;
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
    MutationOrder* order{nullptr};
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t commit_calls{0};
    std::uint32_t fail_read_call{0};
    bool fail_write{false};
    bool fail_commit{false};
    bool commit_then_fail{false};
};

class MemorySelectorStorage final : public MapSelectorStorage {
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
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) override {
        ++write_calls;
        if (slot >= slots.size() || data == nullptr ||
            size != kMapSelectorCheckpointBytes) {
            return MapSelectorStorageError::invalid_argument;
        }
        if (fail_write) {
            return MapSelectorStorageError::io_failure;
        }
        std::copy(data, data + size, slots[slot].begin());
        present[slot] = true;
        return MapSelectorStorageError::none;
    }

    MapSelectorStorageError commit_slot(
        std::uint8_t slot,
        std::size_t offset,
        std::uint8_t value) override {
        ++commit_calls;
        if (order != nullptr && order->selector_commit == 0) {
            order->selector_commit = order->mark();
        }
        if (slot >= slots.size() || !present[slot] ||
            offset != kMapSelectorCommitOffset ||
            value != kMapSelectorCommitMarker) {
            return MapSelectorStorageError::invalid_argument;
        }
        if (fail_commit) {
            if (commit_then_fail) {
                slots[slot][offset] = value;
            }
            return MapSelectorStorageError::io_failure;
        }
        slots[slot][offset] = value;
        return MapSelectorStorageError::none;
    }

    MapSelectorStorageError erase_slot(std::uint8_t slot) override {
        ++erase_calls;
        if (slot >= slots.size()) {
            return MapSelectorStorageError::invalid_argument;
        }
        if (fail_erase) {
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
    MutationOrder* order{nullptr};
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t commit_calls{0};
    std::uint32_t erase_calls{0};
    std::uint32_t fail_read_call{0};
    bool fail_write{false};
    bool fail_commit{false};
    bool commit_then_fail{false};
    bool fail_erase{false};
};

class MemoryProtectedSource final
    : public MapSelectorDomainProtectedSource {
public:
    MapSelectorDomainProtectedSourceRead read() override {
        ++read_calls;
        if (fail_read_call == read_calls) {
            return {
                read_error,
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
        return MapSelectorDomainProtectedSourceError::rejected;
    }

    MapSelectorDomainProtectedSourceError advance_selector_generation(
        const MapSelectorDomainProtectedAdvanceRequest& request) override {
        ++advance_calls;
        last_advance = request;
        if (order != nullptr && order->protected_advance == 0) {
            order->protected_advance = order->mark();
        }
        if (request.expected_domain != current_domain ||
            request.expected_selector_generation != selector_generation ||
            request.proposed_selector_generation <= selector_generation) {
            return MapSelectorDomainProtectedSourceError::conflict;
        }
        if (advance_error ==
                MapSelectorDomainProtectedSourceError::none ||
            apply_then_fail) {
            selector_generation = request.proposed_selector_generation;
        }
        return advance_error;
    }

    MutationOrder* order{nullptr};
    MapSelectorDomainProtectedSourceState state{
        MapSelectorDomainProtectedSourceState::ready};
    MapSelectorDomainId current_domain{domain(41)};
    std::uint64_t selector_generation{2};
    MapSelectorDomainProtectedSourceError advance_error{
        MapSelectorDomainProtectedSourceError::none};
    MapSelectorDomainProtectedSourceError read_error{
        MapSelectorDomainProtectedSourceError::io_failure};
    std::uint32_t read_calls{0};
    std::uint32_t advance_calls{0};
    std::uint32_t fail_read_call{0};
    std::uint32_t change_on_read_call{0};
    MapSelectorDomainId changed_domain{domain(41)};
    std::uint64_t changed_generation{2};
    bool apply_then_fail{false};
    MapSelectorDomainProtectedAdvanceRequest last_advance{};
};

struct Fixture {
    MemoryDomainStorage domain_storage{};
    MemorySelectorStorage selector_storage{};
    MemoryProtectedSource source{};
    MapSelectorDomainStore domain_store{domain_storage};
    MapSelectorStore selector_store{selector_storage};
    MapSelectorDomainTransitionCoordinator coordinator{
        domain_store, selector_store, source};

    void seed_domain(std::uint64_t generation = 2) {
        const auto active = active_record(generation, generation + 1);
        domain_storage.seed(0, domain_bytes(prior_record(active)));
        domain_storage.seed(1, domain_bytes(active));
        source.current_domain = active.current_domain;
        source.selector_generation = generation;
    }

    void seed_selector(
        const MapActivationGuard& guard,
        std::uint64_t generation = 2) {
        selector_storage.seed(0, selector_bytes(guard, generation));
    }

    MapActivationGuard trial(std::uint64_t generation = 2) {
        MapActivationGuard guard{};
        EXPECT(guard.start(
                   policy(), {MapSelectorState::valid, package()}) ==
               MapActivationError::none);
        EXPECT(guard.stage(candidate_package()) ==
               MapActivationError::none);
        EXPECT(guard.mark_selector_committed(
                   MapSlot::slot_b, 20, 100) ==
               MapActivationError::none);
        seed_domain(generation);
        seed_selector(guard, generation);
        return guard;
    }

    MapActivationGuard fallback(std::uint64_t generation = 2) {
        auto guard = trial(generation);
        EXPECT(guard.report_trial_read(false, 110) ==
               MapActivationError::trial_health_failed);
        seed_selector(guard, generation);
        return guard;
    }
};

void expect_mapless(
    const MapSelectorDomainTransitionResult& result,
    const MapActivationGuard& live) {
    EXPECT(result.live_guard_updated);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(live.status().running);
    EXPECT(live.status().state == MapActivationState::mapless);
}

void test_volatile_health_rechecks_without_persistence() {
    Fixture fixture{};
    auto live = fixture.trial();

    const auto result = fixture.coordinator.report_trial_read(
        live, policy(), true, 110);

    EXPECT(result.completed());
    EXPECT(result.state ==
           MapSelectorDomainTransitionState::applied_volatile);
    EXPECT(live.status().state == MapActivationState::trial);
    EXPECT(live.status().healthy_trial_reads == 1);
    EXPECT(fixture.selector_storage.write_calls == 0);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(fixture.domain_storage.write_calls == 0);
}

void test_promotion_orders_selector_source_and_domain() {
    Fixture fixture{};
    MutationOrder order{};
    fixture.selector_storage.order = &order;
    fixture.source.order = &order;
    fixture.domain_storage.order = &order;
    auto live = fixture.trial();
    EXPECT(fixture.coordinator.report_trial_read(
               live, policy(), true, 110)
               .completed());
    EXPECT(fixture.coordinator.report_trial_read(
               live, policy(), true, 120)
               .completed());

    const auto result = fixture.coordinator.report_trial_read(
        live, policy(), true, 130);

    EXPECT(result.completed());
    EXPECT(result.state == MapSelectorDomainTransitionState::committed);
    EXPECT(result.selector_generation_before == 2);
    EXPECT(result.selector_generation_after == 3);
    EXPECT(order.selector_commit != 0);
    EXPECT(order.selector_commit < order.protected_advance);
    EXPECT(order.protected_advance < order.domain_commit);
    EXPECT(fixture.source.selector_generation == 3);
    EXPECT(live.status().state == MapActivationState::active);
    EXPECT(live.status().previous_cleanup_permitted);
}

void test_deadline_fallback_and_completion_are_durable() {
    Fixture fixture{};
    auto live = fixture.trial();

    const auto deadline = fixture.coordinator.tick(
        live, policy(), 601);
    EXPECT(deadline.completed());
    EXPECT(deadline.state == MapSelectorDomainTransitionState::committed);
    EXPECT(live.status().state == MapActivationState::fallback_required);
    EXPECT(fixture.source.selector_generation == 3);

    const auto completed = fixture.coordinator.complete_fallback(
        live, policy(), package());
    EXPECT(completed.completed());
    EXPECT(completed.selector_generation_before == 3);
    EXPECT(completed.selector_generation_after == 4);
    EXPECT(live.status().state == MapActivationState::active);
    EXPECT(live.status().active_slot == MapSlot::slot_a);
    EXPECT(live.status().previous_slot == MapSlot::none);
}

void test_previous_package_cleanup_is_durable() {
    Fixture fixture{};
    auto live = fixture.trial();
    EXPECT(fixture.coordinator.report_trial_read(
               live, policy(), true, 110)
               .completed());
    EXPECT(fixture.coordinator.report_trial_read(
               live, policy(), true, 120)
               .completed());
    EXPECT(fixture.coordinator.report_trial_read(
               live, policy(), true, 130)
               .completed());

    const auto cleanup = fixture.coordinator.mark_previous_removed(
        live, policy(), MapSlot::slot_a, 10);

    EXPECT(cleanup.completed());
    EXPECT(cleanup.selector_generation_before == 3);
    EXPECT(cleanup.selector_generation_after == 4);
    EXPECT(live.status().previous_slot == MapSlot::none);
    EXPECT(!live.status().previous_cleanup_permitted);
}

void test_rejected_operation_rechecks_without_mutation() {
    Fixture fixture{};
    auto live = fixture.trial();

    const auto result = fixture.coordinator.mark_previous_removed(
        live, policy(), MapSlot::slot_a, 10);

    EXPECT(result.state == MapSelectorDomainTransitionState::rejected);
    EXPECT(result.reason ==
           MapSelectorDomainTransitionReason::transition_rejected);
    EXPECT(result.domain_generation_verified);
    EXPECT(result.selector_verified);
    EXPECT(result.protected_source_verified);
    EXPECT(fixture.selector_storage.write_calls == 0);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(fixture.domain_storage.write_calls == 0);
    EXPECT(live.status().state == MapActivationState::trial);
}

void test_initial_generation_mismatch_contains_before_selector_write() {
    Fixture fixture{};
    auto live = fixture.trial();
    fixture.source.selector_generation = 1;

    const auto result = fixture.coordinator.tick(
        live, policy(), 110);

    EXPECT(result.state ==
           MapSelectorDomainTransitionState::reconciliation_required);
    EXPECT(result.reason == MapSelectorDomainTransitionReason::
                                protected_generation_mismatch);
    EXPECT(fixture.selector_storage.write_calls == 0);
    expect_mapless(result, live);
}

void test_selector_save_failure_never_advances_protected_state() {
    Fixture fixture{};
    auto live = fixture.trial();
    EXPECT(fixture.coordinator.report_trial_read(
               live, policy(), true, 110)
               .completed());
    EXPECT(fixture.coordinator.report_trial_read(
               live, policy(), true, 120)
               .completed());
    fixture.selector_storage.fail_commit = true;

    const auto result = fixture.coordinator.report_trial_read(
        live, policy(), true, 130);

    EXPECT(result.state == MapSelectorDomainTransitionState::
                               reconciliation_required);
    EXPECT(result.reason ==
           MapSelectorDomainTransitionReason::transition_failed);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(fixture.domain_storage.write_calls == 0);
    expect_mapless(result, live);
}

void test_interrupted_promotion_recovers_at_restart() {
    Fixture fixture{};
    auto live = fixture.trial();
    EXPECT(fixture.coordinator.report_trial_read(
               live, policy(), true, 110)
               .completed());
    EXPECT(fixture.coordinator.report_trial_read(
               live, policy(), true, 120)
               .completed());
    fixture.source.advance_error =
        MapSelectorDomainProtectedSourceError::io_failure;

    const auto failed = fixture.coordinator.report_trial_read(
        live, policy(), true, 130);
    EXPECT(failed.state ==
           MapSelectorDomainTransitionState::reconciliation_required);
    EXPECT(failed.selector_persisted);
    EXPECT(fixture.source.selector_generation == 2);
    expect_mapless(failed, live);

    fixture.source.advance_error =
        MapSelectorDomainProtectedSourceError::none;
    MapActivationGuard restarted{};
    MapSelectorDomainTrialBootCoordinator boot{
        fixture.domain_store, fixture.selector_store, fixture.source};
    const auto recovered = boot.boot(
        policy(), candidate_package(), package(), 200, restarted);

    EXPECT(recovered.completed());
    EXPECT(recovered.state ==
           MapSelectorDomainTrialBootState::active_ready);
    EXPECT(fixture.source.selector_generation == 3);
    EXPECT(restarted.status().state == MapActivationState::active);
    EXPECT(restarted.status().previous_cleanup_permitted);
}

void test_uncertain_domain_commit_recovers_at_restart() {
    Fixture fixture{};
    auto live = fixture.trial();
    EXPECT(fixture.coordinator.report_trial_read(
               live, policy(), true, 110)
               .completed());
    EXPECT(fixture.coordinator.report_trial_read(
               live, policy(), true, 120)
               .completed());
    fixture.domain_storage.fail_commit = true;
    fixture.domain_storage.commit_then_fail = true;

    const auto failed = fixture.coordinator.report_trial_read(
        live, policy(), true, 130);
    EXPECT(failed.state ==
           MapSelectorDomainTransitionState::reconciliation_required);
    EXPECT(failed.reason ==
           MapSelectorDomainTransitionReason::domain_commit_uncertain);
    expect_mapless(failed, live);

    fixture.domain_storage.fail_commit = false;
    MapActivationGuard restarted{};
    MapSelectorDomainTrialBootCoordinator boot{
        fixture.domain_store, fixture.selector_store, fixture.source};
    const auto recovered = boot.boot(
        policy(), candidate_package(), package(), 200, restarted);

    EXPECT(recovered.completed());
    EXPECT(recovered.state ==
           MapSelectorDomainTrialBootState::active_ready);
    EXPECT(restarted.status().map_available);
}

void test_invalid_fallback_retains_domain_history_for_service() {
    Fixture fixture{};
    auto live = fixture.fallback();

    const auto result = fixture.coordinator.complete_fallback(
        live, policy(), candidate_package());

    EXPECT(result.state ==
           MapSelectorDomainTransitionState::service_required);
    EXPECT(result.reason == MapSelectorDomainTransitionReason::
                                protected_history_retained);
    EXPECT(result.reconciliation_required);
    EXPECT(fixture.source.selector_generation == 2);
    EXPECT(fixture.domain_storage.write_calls == 0);
    EXPECT(fixture.selector_storage.erase_calls == 2);
    expect_mapless(result, live);
}

void test_final_source_change_blocks_publication() {
    Fixture fixture{};
    auto live = fixture.trial();
    EXPECT(fixture.coordinator.report_trial_read(
               live, policy(), true, 110)
               .completed());
    EXPECT(fixture.coordinator.report_trial_read(
               live, policy(), true, 120)
               .completed());
    fixture.source.change_on_read_call = fixture.source.read_calls + 3;
    fixture.source.changed_generation = 99;

    const auto result = fixture.coordinator.report_trial_read(
        live, policy(), true, 130);

    EXPECT(result.state ==
           MapSelectorDomainTransitionState::reconciliation_required);
    EXPECT(result.reason ==
           MapSelectorDomainTransitionReason::final_source_changed);
    expect_mapless(result, live);
}

}  // namespace

int main() {
    test_volatile_health_rechecks_without_persistence();
    test_promotion_orders_selector_source_and_domain();
    test_deadline_fallback_and_completion_are_durable();
    test_previous_package_cleanup_is_durable();
    test_rejected_operation_rechecks_without_mutation();
    test_initial_generation_mismatch_contains_before_selector_write();
    test_selector_save_failure_never_advances_protected_state();
    test_interrupted_promotion_recovers_at_restart();
    test_uncertain_domain_commit_recovers_at_restart();
    test_invalid_fallback_retains_domain_history_for_service();
    test_final_source_change_blocks_publication();

    if (failures != 0) {
        std::cerr << failures
                  << " domain-aware map transition assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "PASS: 11 domain-aware map transition scenario groups\n";
    return EXIT_SUCCESS;
}
