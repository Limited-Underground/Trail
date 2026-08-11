#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_checkpoint.hpp"
#include "opentrail/map_selector_domain_activation.hpp"
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

class FakeDomainStorage final : public MapSelectorDomainStorage {
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
        if (fail_read_slot == static_cast<int>(slot)) {
            return MapSelectorDomainStorageError::io_failure;
        }
        if (!present[slot]) {
            return MapSelectorDomainStorageError::not_found;
        }
        if (corrupt_read_after_commit && commit_calls != 0 && slot == 1) {
            auto corrupted = slots[slot];
            corrupted[8] ^= 0x5AU;
            std::copy(corrupted.begin(), corrupted.end(), output);
            return MapSelectorDomainStorageError::none;
        }
        std::copy(slots[slot].begin(), slots[slot].end(), output);
        return MapSelectorDomainStorageError::none;
    }

    MapSelectorDomainStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) override {
        if (slot >= slots.size() || data == nullptr ||
            size != kMapSelectorDomainRecordBytes) {
            return MapSelectorDomainStorageError::invalid_argument;
        }
        ++write_calls;
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
        if (slot >= slots.size() || !present[slot] ||
            offset != kMapSelectorDomainRecordCommitOffset ||
            value != kMapSelectorDomainRecordCommitMarker) {
            return MapSelectorDomainStorageError::invalid_argument;
        }
        ++commit_calls;
        if (order != nullptr && order->domain_commit == 0) {
            order->domain_commit = order->mark();
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
        const std::array<std::uint8_t, kMapSelectorDomainRecordBytes>& bytes) {
        slots[slot] = bytes;
        present[slot] = true;
    }

    std::array<
        std::array<std::uint8_t, kMapSelectorDomainRecordBytes>,
        kMapSelectorDomainSlotCount>
        slots{};
    std::array<bool, kMapSelectorDomainSlotCount> present{};
    MutationOrder* order{nullptr};
    int fail_read_slot{-1};
    bool fail_write{false};
    bool fail_commit{false};
    bool commit_then_fail{false};
    bool corrupt_read_after_commit{false};
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t commit_calls{0};
};

class FakeSelectorStorage final : public MapSelectorStorage {
public:
    MapSelectorStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override {
        ++read_calls;
        if (inject_on_read_call != 0 &&
            read_calls == inject_on_read_call) {
            slots[inject_slot] = injected_bytes;
            present[inject_slot] = true;
        }
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
        if (corrupt_read_after_commit && commit_calls != 0 &&
            slot == last_committed_slot) {
            auto corrupted = slots[slot];
            corrupted[8] ^= 0x33U;
            std::copy(corrupted.begin(), corrupted.end(), output);
            return MapSelectorStorageError::none;
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
        if (slot >= slots.size() || !present[slot] ||
            offset != kMapSelectorCommitOffset ||
            value != kMapSelectorCommitMarker) {
            return MapSelectorStorageError::invalid_argument;
        }
        ++commit_calls;
        last_committed_slot = slot;
        if (order != nullptr && order->selector_commit == 0) {
            order->selector_commit = order->mark();
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
    MutationOrder* order{nullptr};
    int fail_read_slot{-1};
    bool fail_write{false};
    bool fail_commit{false};
    bool commit_then_fail{false};
    bool corrupt_read_after_commit{false};
    std::uint8_t last_committed_slot{0};
    std::uint32_t inject_on_read_call{0};
    std::uint8_t inject_slot{1};
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> injected_bytes{};
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t commit_calls{0};
};

class FakeProtectedSource final : public MapSelectorDomainProtectedSource {
public:
    MapSelectorDomainProtectedSourceRead read() override {
        ++read_calls;
        if (fail_read_call == static_cast<int>(read_calls)) {
            return {
                read_error,
                MapSelectorDomainProtectedSourceState::unknown,
                {},
                0};
        }
        if (change_on_read_call == read_calls) {
            selector_generation = changed_generation;
        }
        auto visible_domain = current_domain;
        auto visible_generation = selector_generation;
        if (wrong_readback && advance_calls != 0) {
            visible_domain = domain(201);
            visible_generation = 99;
        }
        return {
            MapSelectorDomainProtectedSourceError::none,
            state,
            visible_domain,
            visible_generation};
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
        if (advance_error == MapSelectorDomainProtectedSourceError::none ||
            apply_then_fail) {
            selector_generation = request.proposed_selector_generation;
        }
        return advance_error;
    }

    MutationOrder* order{nullptr};
    MapSelectorDomainProtectedSourceState state{
        MapSelectorDomainProtectedSourceState::ready};
    MapSelectorDomainId current_domain{domain(41)};
    std::uint64_t selector_generation{0};
    MapSelectorDomainProtectedSourceError advance_error{
        MapSelectorDomainProtectedSourceError::none};
    MapSelectorDomainProtectedSourceError read_error{
        MapSelectorDomainProtectedSourceError::io_failure};
    int fail_read_call{-1};
    std::uint32_t change_on_read_call{0};
    std::uint64_t changed_generation{0};
    bool apply_then_fail{false};
    bool wrong_readback{false};
    std::uint32_t read_calls{0};
    std::uint32_t advance_calls{0};
    MapSelectorDomainProtectedAdvanceRequest last_advance{};
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

MapActivationGuard active_guard(
    const MapPackageEvidence& selected = package()) {
    MapActivationGuard guard{};
    EXPECT(guard.start(policy(), {MapSelectorState::valid, selected}) ==
           MapActivationError::none);
    return guard;
}

MapActivationGuard mapless_guard() {
    MapActivationGuard guard{};
    EXPECT(guard.start(policy(), {MapSelectorState::missing, {}}) ==
           MapActivationError::none);
    return guard;
}

MapSelectorDomainRecord pending_fresh(
    std::uint64_t record_generation = 1) {
    return {
        kMapSelectorDomainRecordVersion,
        MapSelectorDomainRecordState::pending_first_baseline,
        MapSelectorDomainRecordOrigin::fresh_device_commissioning,
        domain(41),
        {},
        0,
        0,
        1,
        record_generation};
}

MapSelectorDomainRecord pending_replacement(
    std::uint64_t floor = 5,
    std::uint64_t record_generation = 3) {
    return {
        kMapSelectorDomainRecordVersion,
        MapSelectorDomainRecordState::pending_selector_reseed,
        MapSelectorDomainRecordOrigin::same_device_replacement,
        domain(41),
        domain(1),
        floor,
        0,
        2,
        record_generation};
}

MapSelectorDomainRecord active_replacement() {
    auto record = pending_replacement();
    record.state = MapSelectorDomainRecordState::active;
    record.accepted_selector_generation = 6;
    record.record_generation = 4;
    return record;
}

std::array<std::uint8_t, kMapSelectorDomainRecordBytes> encoded_domain(
    const MapSelectorDomainRecord& record) {
    std::array<std::uint8_t, kMapSelectorDomainRecordBytes> bytes{};
    EXPECT(encode_map_selector_domain_record(
               record, bytes.data(), bytes.size())
               .succeeded());
    return bytes;
}

std::array<std::uint8_t, kMapSelectorCheckpointBytes> encoded_selector(
    std::uint64_t record_generation,
    const MapPackageEvidence& selected = package()) {
    const auto guard = active_guard(selected);
    MapSelectorCheckpoint checkpoint{};
    EXPECT(guard.export_checkpoint(record_generation, checkpoint) ==
           MapActivationError::none);
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> bytes{};
    EXPECT(encode_map_selector_checkpoint(
               checkpoint, bytes.data(), bytes.size())
               .succeeded());
    return bytes;
}

struct Fixture {
    Fixture()
        : domain_store(domain_storage),
          selector_store(selector_storage),
          coordinator(domain_store, selector_store, protected_source) {
        domain_storage.order = &order;
        selector_storage.order = &order;
        protected_source.order = &order;
    }

    void seed_domain(const MapSelectorDomainRecord& record) {
        domain_storage.seed(0, encoded_domain(record));
    }

    MutationOrder order{};
    FakeDomainStorage domain_storage{};
    FakeSelectorStorage selector_storage{};
    FakeProtectedSource protected_source{};
    MapSelectorDomainStore domain_store;
    MapSelectorStore selector_store;
    MapSelectorDomainActivationCoordinator coordinator;
};

void test_fresh_activation_orders_all_durable_steps() {
    Fixture fixture{};
    fixture.seed_domain(pending_fresh());
    MapActivationGuard guard{};
    const auto result = fixture.coordinator.activate(
        guard, policy(), package());
    EXPECT(result.activated());
    EXPECT(result.selector_generation == 1);
    EXPECT(result.domain_record_generation == 2);
    EXPECT(result.selector_persisted);
    EXPECT(result.protected_advance_called);
    EXPECT(result.active_domain_saved);
    EXPECT(fixture.order.selector_commit != 0);
    EXPECT(fixture.order.selector_commit <
           fixture.order.protected_advance);
    EXPECT(fixture.order.protected_advance <
           fixture.order.domain_commit);
    EXPECT(guard.status().state == MapActivationState::active);
    EXPECT(guard.status().map_available);
    const auto stored = fixture.domain_store.inspect();
    EXPECT(stored.record.state == MapSelectorDomainRecordState::active);
    EXPECT(stored.record.accepted_selector_generation == 1);
}

void test_replacement_jumps_above_retired_floor() {
    Fixture fixture{};
    fixture.seed_domain(pending_replacement());
    auto guard = mapless_guard();
    const auto result = fixture.coordinator.activate(
        guard, policy(), package());
    EXPECT(result.activated());
    EXPECT(result.selector_generation == 6);
    EXPECT(result.retired_selector_floor == 5);
    EXPECT(fixture.protected_source.last_advance.
               expected_selector_generation == 0);
    EXPECT(fixture.protected_source.last_advance.
               proposed_selector_generation == 6);
    const auto stored = fixture.domain_store.inspect();
    EXPECT(stored.record.state == MapSelectorDomainRecordState::active);
    EXPECT(stored.record.retired_selector_generation == 5);
    EXPECT(stored.record.accepted_selector_generation == 6);
}

void test_invalid_live_policy_candidate_and_domain_do_not_mutate() {
    Fixture live_fixture{};
    live_fixture.seed_domain(pending_fresh());
    auto live = active_guard();
    const auto live_result = live_fixture.coordinator.activate(
        live, policy(), package());
    EXPECT(live_result.reason ==
           MapSelectorDomainActivationReason::map_unavailable_required);
    EXPECT(live_fixture.selector_storage.write_calls == 0);
    EXPECT(live_fixture.protected_source.advance_calls == 0);

    Fixture policy_fixture{};
    policy_fixture.seed_domain(pending_fresh());
    auto mapless = mapless_guard();
    auto other_policy = policy();
    ++other_policy.required_healthy_reads;
    const auto policy_result = policy_fixture.coordinator.activate(
        mapless, other_policy, package());
    EXPECT(policy_result.reason ==
           MapSelectorDomainActivationReason::invalid_policy);

    Fixture package_fixture{};
    package_fixture.seed_domain(pending_fresh());
    auto invalid_package = package();
    invalid_package.integrity_verified = false;
    MapActivationGuard stopped{};
    const auto package_result = package_fixture.coordinator.activate(
        stopped, policy(), invalid_package);
    EXPECT(package_result.reason ==
           MapSelectorDomainActivationReason::candidate_rejected);

    Fixture domain_fixture{};
    MapActivationGuard no_domain{};
    const auto domain_result = domain_fixture.coordinator.activate(
        no_domain, policy(), package());
    EXPECT(domain_result.reason ==
           MapSelectorDomainActivationReason::domain_state_mismatch);
    EXPECT(domain_fixture.selector_storage.read_calls == 0);
}

void test_selector_state_and_generation_exhaustion_fail_closed() {
    Fixture dirty{};
    dirty.seed_domain(pending_replacement());
    dirty.selector_storage.seed(0, encoded_selector(5));
    MapActivationGuard dirty_guard{};
    const auto dirty_result = dirty.coordinator.activate(
        dirty_guard, policy(), package());
    EXPECT(dirty_result.reason ==
           MapSelectorDomainActivationReason::selector_state_mismatch);
    EXPECT(dirty.protected_source.read_calls == 0);

    Fixture unreadable{};
    unreadable.seed_domain(pending_fresh());
    unreadable.selector_storage.fail_read_slot = 1;
    MapActivationGuard unreadable_guard{};
    const auto unreadable_result = unreadable.coordinator.activate(
        unreadable_guard, policy(), package());
    EXPECT(unreadable_result.reason ==
           MapSelectorDomainActivationReason::
               selector_storage_unavailable);

    Fixture exhausted{};
    exhausted.seed_domain(pending_replacement(
        std::numeric_limits<std::uint64_t>::max()));
    MapActivationGuard exhausted_guard{};
    const auto exhausted_result = exhausted.coordinator.activate(
        exhausted_guard, policy(), package());
    EXPECT(exhausted_result.reason ==
           MapSelectorDomainActivationReason::
               domain_generation_exhausted);
    EXPECT(exhausted.selector_storage.read_calls == 0);
}

void test_protected_preflight_failure_never_writes_selector() {
    Fixture unavailable{};
    unavailable.seed_domain(pending_fresh());
    unavailable.protected_source.fail_read_call = 1;
    MapActivationGuard unavailable_guard{};
    const auto unavailable_result = unavailable.coordinator.activate(
        unavailable_guard, policy(), package());
    EXPECT(unavailable_result.reason ==
           MapSelectorDomainActivationReason::
               protected_source_unavailable);
    EXPECT(unavailable.selector_storage.write_calls == 0);

    Fixture wrong_domain{};
    wrong_domain.seed_domain(pending_fresh());
    wrong_domain.protected_source.current_domain = domain(99);
    MapActivationGuard wrong_domain_guard{};
    const auto wrong_domain_result = wrong_domain.coordinator.activate(
        wrong_domain_guard, policy(), package());
    EXPECT(wrong_domain_result.reason ==
           MapSelectorDomainActivationReason::protected_source_mismatch);
    EXPECT(wrong_domain.selector_storage.write_calls == 0);

    Fixture wrong_generation{};
    wrong_generation.seed_domain(pending_fresh());
    wrong_generation.protected_source.selector_generation = 1;
    MapActivationGuard wrong_generation_guard{};
    const auto wrong_generation_result =
        wrong_generation.coordinator.activate(
            wrong_generation_guard, policy(), package());
    EXPECT(wrong_generation_result.reason ==
           MapSelectorDomainActivationReason::protected_source_mismatch);
    EXPECT(wrong_generation.selector_storage.write_calls == 0);
}

void test_selector_save_failures_do_not_advance_source() {
    Fixture write_failure{};
    write_failure.seed_domain(pending_fresh());
    write_failure.selector_storage.fail_write = true;
    MapActivationGuard write_guard{};
    const auto write_result = write_failure.coordinator.activate(
        write_guard, policy(), package());
    EXPECT(write_result.reason ==
           MapSelectorDomainActivationReason::selector_save_failed);
    EXPECT(write_failure.protected_source.advance_calls == 0);
    EXPECT(!write_guard.status().map_available);

    Fixture commit_failure{};
    commit_failure.seed_domain(pending_fresh());
    commit_failure.selector_storage.fail_commit = true;
    MapActivationGuard commit_guard{};
    const auto commit_result = commit_failure.coordinator.activate(
        commit_guard, policy(), package());
    EXPECT(commit_result.reason ==
           MapSelectorDomainActivationReason::selector_commit_uncertain);
    EXPECT(commit_result.reconciliation_required);
    EXPECT(commit_failure.protected_source.advance_calls == 0);
}

void test_protected_advance_failure_leaves_resumable_selector() {
    Fixture fixture{};
    fixture.seed_domain(pending_replacement());
    fixture.protected_source.advance_error =
        MapSelectorDomainProtectedSourceError::io_failure;
    MapActivationGuard guard{};
    const auto result = fixture.coordinator.activate(
        guard, policy(), package());
    EXPECT(result.reason ==
           MapSelectorDomainActivationReason::protected_advance_failed);
    EXPECT(result.selector_persisted);
    EXPECT(result.reconciliation_required);
    EXPECT(fixture.selector_store.inspect().generation == 6);
    EXPECT(fixture.domain_store.inspect().record.state ==
           MapSelectorDomainRecordState::pending_selector_reseed);
    EXPECT(fixture.domain_storage.commit_calls == 0);
    EXPECT(!guard.status().map_available);
}

void test_retry_resumes_selector_then_advances_and_activates() {
    Fixture fixture{};
    fixture.seed_domain(pending_replacement());
    fixture.selector_storage.seed(0, encoded_selector(6));
    MapActivationGuard guard{};
    const auto result = fixture.coordinator.activate(
        guard, policy(), package());
    EXPECT(result.activated());
    EXPECT(result.resumed_selector);
    EXPECT(fixture.selector_storage.write_calls == 0);
    EXPECT(fixture.protected_source.advance_calls == 1);
    EXPECT(fixture.domain_storage.commit_calls == 1);
}

void test_applied_then_failed_advance_resumes_without_reapply() {
    Fixture fixture{};
    fixture.seed_domain(pending_replacement());
    fixture.protected_source.advance_error =
        MapSelectorDomainProtectedSourceError::io_failure;
    fixture.protected_source.apply_then_fail = true;
    MapActivationGuard first_guard{};
    const auto failed = fixture.coordinator.activate(
        first_guard, policy(), package());
    EXPECT(failed.reason ==
           MapSelectorDomainActivationReason::protected_advance_failed);
    EXPECT(fixture.protected_source.selector_generation == 6);
    EXPECT(fixture.protected_source.advance_calls == 1);

    fixture.protected_source.advance_error =
        MapSelectorDomainProtectedSourceError::none;
    fixture.protected_source.apply_then_fail = false;
    MapActivationGuard retry_guard{};
    const auto retried = fixture.coordinator.activate(
        retry_guard, policy(), package());
    EXPECT(retried.activated());
    EXPECT(retried.resumed_selector);
    EXPECT(!retried.protected_advance_called);
    EXPECT(fixture.protected_source.advance_calls == 1);
}

void test_domain_commit_failure_resumes_pending_or_active() {
    Fixture pending_retry{};
    pending_retry.seed_domain(pending_fresh());
    pending_retry.domain_storage.fail_commit = true;
    MapActivationGuard first_guard{};
    const auto failed = pending_retry.coordinator.activate(
        first_guard, policy(), package());
    EXPECT(failed.reason ==
           MapSelectorDomainActivationReason::domain_commit_uncertain);
    EXPECT(failed.selector_verified);
    EXPECT(failed.protected_source_verified);
    EXPECT(!first_guard.status().map_available);

    pending_retry.domain_storage.fail_commit = false;
    MapActivationGuard retry_guard{};
    const auto retried = pending_retry.coordinator.activate(
        retry_guard, policy(), package());
    EXPECT(retried.activated());
    EXPECT(retried.resumed_selector);
    EXPECT(!retried.protected_advance_called);

    Fixture active_retry{};
    active_retry.seed_domain(pending_fresh());
    active_retry.domain_storage.fail_commit = true;
    active_retry.domain_storage.commit_then_fail = true;
    MapActivationGuard uncertain_guard{};
    const auto uncertain = active_retry.coordinator.activate(
        uncertain_guard, policy(), package());
    EXPECT(uncertain.reason ==
           MapSelectorDomainActivationReason::domain_commit_uncertain);
    active_retry.domain_storage.fail_commit = false;
    active_retry.domain_storage.commit_then_fail = false;
    MapActivationGuard active_guard_retry{};
    const auto active_result = active_retry.coordinator.activate(
        active_guard_retry, policy(), package());
    EXPECT(active_result.activated());
    EXPECT(active_result.resumed_active_domain);
    EXPECT(!active_result.active_domain_saved);
}

void test_active_record_restart_requires_exact_selector_source_package() {
    Fixture exact{};
    exact.seed_domain(active_replacement());
    exact.selector_storage.seed(0, encoded_selector(6));
    exact.protected_source.selector_generation = 6;
    MapActivationGuard exact_guard{};
    const auto exact_result = exact.coordinator.activate(
        exact_guard, policy(), package());
    EXPECT(exact_result.activated());
    EXPECT(exact_result.resumed_active_domain);
    EXPECT(exact.domain_storage.write_calls == 0);
    EXPECT(exact.selector_storage.write_calls == 0);
    EXPECT(exact.protected_source.advance_calls == 0);

    Fixture wrong_package{};
    wrong_package.seed_domain(active_replacement());
    wrong_package.selector_storage.seed(0, encoded_selector(6));
    wrong_package.protected_source.selector_generation = 6;
    auto other_package = package(MapSlot::slot_b, 20);
    MapActivationGuard wrong_package_guard{};
    const auto package_result = wrong_package.coordinator.activate(
        wrong_package_guard, policy(), other_package);
    EXPECT(package_result.reason ==
           MapSelectorDomainActivationReason::selector_restore_failed);
    EXPECT(!wrong_package_guard.status().map_available);

    Fixture wrong_source{};
    wrong_source.seed_domain(active_replacement());
    wrong_source.selector_storage.seed(0, encoded_selector(6));
    wrong_source.protected_source.selector_generation = 7;
    MapActivationGuard wrong_source_guard{};
    const auto source_result = wrong_source.coordinator.activate(
        wrong_source_guard, policy(), package());
    EXPECT(source_result.reason ==
           MapSelectorDomainActivationReason::protected_source_mismatch);
}

void test_selector_race_after_advance_blocks_domain_activation() {
    Fixture fixture{};
    fixture.seed_domain(pending_fresh());
    fixture.selector_storage.inject_on_read_call = 6;
    fixture.selector_storage.injected_bytes =
        encoded_selector(1, package(MapSlot::slot_b, 20));
    MapActivationGuard guard{};
    const auto result = fixture.coordinator.activate(
        guard, policy(), package());
    EXPECT(result.reason ==
           MapSelectorDomainActivationReason::selector_changed);
    EXPECT(result.reconciliation_required);
    EXPECT(fixture.protected_source.selector_generation == 1);
    EXPECT(fixture.domain_storage.write_calls == 0);
    EXPECT(!guard.status().map_available);
}

void test_source_readback_and_final_change_never_publish_map() {
    Fixture readback{};
    readback.seed_domain(pending_fresh());
    readback.protected_source.wrong_readback = true;
    MapActivationGuard readback_guard{};
    const auto readback_result = readback.coordinator.activate(
        readback_guard, policy(), package());
    EXPECT(readback_result.reason ==
           MapSelectorDomainActivationReason::
               protected_readback_mismatch);
    EXPECT(readback.domain_storage.write_calls == 0);
    EXPECT(!readback_guard.status().map_available);

    Fixture final_change{};
    final_change.seed_domain(pending_fresh());
    final_change.protected_source.change_on_read_call = 3;
    final_change.protected_source.changed_generation = 2;
    MapActivationGuard final_guard{};
    const auto final_result = final_change.coordinator.activate(
        final_guard, policy(), package());
    EXPECT(final_result.reason ==
           MapSelectorDomainActivationReason::final_source_changed);
    EXPECT(final_change.domain_store.inspect().record.state ==
           MapSelectorDomainRecordState::active);
    EXPECT(!final_guard.status().map_available);
}

void test_domain_and_selector_verification_failures_are_recoverable() {
    Fixture selector_verify{};
    selector_verify.seed_domain(pending_fresh());
    selector_verify.selector_storage.corrupt_read_after_commit = true;
    MapActivationGuard selector_guard{};
    const auto selector_result = selector_verify.coordinator.activate(
        selector_guard, policy(), package());
    EXPECT(selector_result.reason ==
           MapSelectorDomainActivationReason::
               selector_verification_failed);
    EXPECT(selector_verify.protected_source.advance_calls == 0);

    Fixture domain_verify{};
    domain_verify.seed_domain(pending_fresh());
    domain_verify.domain_storage.corrupt_read_after_commit = true;
    MapActivationGuard domain_guard{};
    const auto domain_result = domain_verify.coordinator.activate(
        domain_guard, policy(), package());
    EXPECT(domain_result.reason ==
           MapSelectorDomainActivationReason::
               domain_verification_failed);
    EXPECT(domain_result.reconciliation_required);
    EXPECT(!domain_guard.status().map_available);
}

}  // namespace

int main() {
    test_fresh_activation_orders_all_durable_steps();
    test_replacement_jumps_above_retired_floor();
    test_invalid_live_policy_candidate_and_domain_do_not_mutate();
    test_selector_state_and_generation_exhaustion_fail_closed();
    test_protected_preflight_failure_never_writes_selector();
    test_selector_save_failures_do_not_advance_source();
    test_protected_advance_failure_leaves_resumable_selector();
    test_retry_resumes_selector_then_advances_and_activates();
    test_applied_then_failed_advance_resumes_without_reapply();
    test_domain_commit_failure_resumes_pending_or_active();
    test_active_record_restart_requires_exact_selector_source_package();
    test_selector_race_after_advance_blocks_domain_activation();
    test_source_readback_and_final_change_never_publish_map();
    test_domain_and_selector_verification_failures_are_recoverable();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector domain-activation assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout <<
        "PASS: 14 map selector domain-activation scenario groups\n";
    return EXIT_SUCCESS;
}
