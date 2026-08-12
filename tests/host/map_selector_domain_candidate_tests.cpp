#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_checkpoint.hpp"
#include "opentrail/map_selector_domain_candidate.hpp"
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

MapActivationGuard active_guard() {
    MapActivationGuard guard{};
    EXPECT(guard.start(policy(), {MapSelectorState::valid, package()}) ==
           MapActivationError::none);
    return guard;
}

MapSelectorDomainRecord active_fresh(
    std::uint64_t accepted_generation = 1,
    std::uint64_t record_generation = 2) {
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

MapSelectorDomainRecord prior_record(
    const MapSelectorDomainRecord& active) {
    auto prior = active;
    prior.state =
        active.origin ==
                MapSelectorDomainRecordOrigin::fresh_device_commissioning
            ? MapSelectorDomainRecordState::pending_first_baseline
            : MapSelectorDomainRecordState::pending_selector_reseed;
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
        if (corrupt_after_commit && commit_calls != 0 &&
            slot == last_committed_slot) {
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
        last_committed_slot = slot;
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
    std::array<
        std::array<std::uint8_t, kMapSelectorDomainRecordBytes>,
        kMapSelectorDomainSlotCount>
        replacement_slots{};
    std::array<bool, kMapSelectorDomainSlotCount> replacement_present{};
    MutationOrder* order{nullptr};
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t commit_calls{0};
    std::uint32_t fail_read_call{0};
    std::uint32_t replace_on_read_call{0};
    std::uint8_t last_committed_slot{0};
    bool fail_write{false};
    bool fail_commit{false};
    bool commit_then_fail{false};
    bool corrupt_after_commit{false};
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
        if (corrupt_after_commit && commit_calls != 0 &&
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
        last_committed_slot = slot;
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
    MutationOrder* order{nullptr};
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t commit_calls{0};
    std::uint32_t erase_calls{0};
    std::uint32_t fail_read_call{0};
    std::uint32_t replace_on_read_call{0};
    std::uint8_t last_committed_slot{0};
    bool fail_write{false};
    bool fail_commit{false};
    bool commit_then_fail{false};
    bool corrupt_after_commit{false};
};

class FakeProtectedSource final : public MapSelectorDomainProtectedSource {
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
        if (wrong_readback && advance_calls != 0) {
            return {
                MapSelectorDomainProtectedSourceError::none,
                MapSelectorDomainProtectedSourceState::ready,
                domain(201),
                99};
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
    std::uint64_t selector_generation{1};
    MapSelectorDomainProtectedSourceError advance_error{
        MapSelectorDomainProtectedSourceError::none};
    MapSelectorDomainProtectedSourceError read_error{
        MapSelectorDomainProtectedSourceError::io_failure};
    std::uint32_t read_calls{0};
    std::uint32_t advance_calls{0};
    std::uint32_t establish_calls{0};
    std::uint32_t fail_read_call{0};
    std::uint32_t change_on_read_call{0};
    MapSelectorDomainId changed_domain{domain(41)};
    std::uint64_t changed_generation{1};
    bool apply_then_fail{false};
    bool wrong_readback{false};
    MapSelectorDomainProtectedAdvanceRequest last_advance{};
};

struct Fixture {
    FakeDomainStorage domain_storage{};
    FakeSelectorStorage selector_storage{};
    FakeProtectedSource source{};
    MapSelectorDomainStore domain_store{domain_storage};
    MapSelectorStore selector_store{selector_storage};
    MapSelectorDomainCandidateCoordinator coordinator{
        domain_store, selector_store, source};

    void seed_domain(
        const MapSelectorDomainRecord& active,
        bool include_prior = true) {
        if (include_prior) {
            domain_storage.seed(0, domain_bytes(prior_record(active)));
        }
        domain_storage.seed(1, domain_bytes(active));
        source.current_domain = active.current_domain;
        source.selector_generation =
            active.accepted_selector_generation;
    }

    void seed_selector(std::uint64_t generation) {
        selector_storage.seed(0, selector_bytes(active_guard(), generation));
    }
};

void expect_mapless(const MapSelectorDomainCandidateResult& result,
                    const MapActivationGuard& live) {
    EXPECT(result.live_guard_updated);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(live.status().running);
    EXPECT(live.status().state == MapActivationState::mapless);
    EXPECT(!live.status().map_available);
}

void test_successful_candidate_ordering() {
    Fixture fixture{};
    MutationOrder order{};
    fixture.domain_storage.order = &order;
    fixture.selector_storage.order = &order;
    fixture.source.order = &order;
    fixture.seed_domain(active_fresh());
    fixture.seed_selector(1);
    auto live = active_guard();

    const auto result = fixture.coordinator.activate(
        live, policy(), candidate_package(), 100);

    EXPECT(result.committed());
    EXPECT(result.prior_selector_generation == 1);
    EXPECT(result.trial_selector_generation == 2);
    EXPECT(result.domain_record_generation == 3);
    EXPECT(result.domain_epoch == 1);
    EXPECT(result.protected_advance_called);
    EXPECT(result.protected_source_verified);
    EXPECT(result.domain_generation_saved);
    EXPECT(result.domain_generation_verified);
    EXPECT(result.selector_verified);
    EXPECT(order.selector_commit != 0);
    EXPECT(order.selector_commit < order.protected_advance);
    EXPECT(order.protected_advance < order.domain_commit);
    EXPECT(live.status().state == MapActivationState::trial);
    EXPECT(live.status().active_slot == MapSlot::slot_b);
    EXPECT(live.status().previous_slot == MapSlot::slot_a);
    EXPECT(fixture.source.selector_generation == 2);
    const auto domain_final = fixture.domain_store.inspect();
    EXPECT(domain_final.record.accepted_selector_generation == 2);
}

void test_replacement_domain_candidate() {
    Fixture fixture{};
    const auto record = active_replacement();
    fixture.seed_domain(record);
    fixture.seed_selector(41);
    auto live = active_guard();

    const auto result = fixture.coordinator.activate(
        live, policy(), candidate_package(), 200);

    EXPECT(result.committed());
    EXPECT(result.prior_selector_generation == 41);
    EXPECT(result.trial_selector_generation == 42);
    EXPECT(result.domain_record_generation == 10);
    EXPECT(fixture.source.selector_generation == 42);
}

void test_live_and_policy_isolation() {
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(1);
        MapActivationGuard live{};
        const auto result = fixture.coordinator.activate(
            live, policy(), candidate_package(), 100);
        EXPECT(result.state == MapSelectorDomainCandidateState::rejected);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::live_baseline_required);
        EXPECT(fixture.domain_storage.read_calls == 0);
        EXPECT(fixture.selector_storage.read_calls == 0);
        EXPECT(fixture.source.read_calls == 0);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(1);
        auto live = active_guard();
        auto wrong_policy = policy();
        ++wrong_policy.required_healthy_reads;
        const auto result = fixture.coordinator.activate(
            live, wrong_policy, candidate_package(), 100);
        EXPECT(result.state == MapSelectorDomainCandidateState::rejected);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::invalid_policy);
        EXPECT(fixture.domain_storage.read_calls == 0);
        EXPECT(fixture.selector_storage.read_calls == 0);
        EXPECT(fixture.source.read_calls == 0);
        EXPECT(live.status().map_available);
    }
}

void test_domain_and_source_preflight() {
    {
        Fixture fixture{};
        fixture.seed_selector(1);
        auto live = active_guard();
        const auto result = fixture.coordinator.activate(
            live, policy(), candidate_package(), 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::domain_not_active);
        EXPECT(fixture.selector_storage.read_calls == 0);
        EXPECT(fixture.source.read_calls == 0);
        expect_mapless(result, live);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(1);
        fixture.source.selector_generation = 2;
        auto live = active_guard();
        const auto result = fixture.coordinator.activate(
            live, policy(), candidate_package(), 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::
                   protected_generation_mismatch);
        EXPECT(fixture.selector_storage.read_calls == 0);
        EXPECT(fixture.source.advance_calls == 0);
        expect_mapless(result, live);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh(
            std::numeric_limits<std::uint64_t>::max(), 8));
        fixture.seed_selector(std::numeric_limits<std::uint64_t>::max());
        auto live = active_guard();
        const auto result = fixture.coordinator.activate(
            live, policy(), candidate_package(), 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::
                   domain_generation_exhausted);
        EXPECT(fixture.selector_storage.read_calls == 0);
        EXPECT(fixture.source.advance_calls == 0);
        expect_mapless(result, live);
    }
}

void test_candidate_rejection_preserves_exact_baseline() {
    Fixture fixture{};
    fixture.seed_domain(active_fresh());
    fixture.seed_selector(1);
    auto live = active_guard();
    auto invalid_candidate = candidate_package();
    invalid_candidate.integrity_verified = false;

    const auto result = fixture.coordinator.activate(
        live, policy(), invalid_candidate, 100);

    EXPECT(result.state == MapSelectorDomainCandidateState::rejected);
    EXPECT(result.reason ==
           MapSelectorDomainCandidateReason::candidate_rejected);
    EXPECT(result.domain_generation_verified);
    EXPECT(result.selector_verified);
    EXPECT(result.protected_source_verified);
    EXPECT(live.status().state == MapActivationState::active);
    EXPECT(live.status().map_available);
    EXPECT(fixture.selector_storage.write_calls == 0);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(fixture.domain_storage.write_calls == 0);
}

void test_rejection_recheck_change_contains_map() {
    {
        Fixture fixture{};
        const auto record = active_fresh();
        fixture.seed_domain(record);
        fixture.seed_selector(1);
        auto changed = record;
        changed.record_generation = 3;
        fixture.domain_storage.replacement_slots[0] =
            domain_bytes(changed);
        fixture.domain_storage.replacement_slots[1] =
            domain_bytes(changed);
        fixture.domain_storage.replacement_present = {true, true};
        fixture.domain_storage.replace_on_read_call = 3;
        auto live = active_guard();
        auto invalid_candidate = candidate_package();
        invalid_candidate.integrity_verified = false;
        const auto result = fixture.coordinator.activate(
            live, policy(), invalid_candidate, 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::domain_changed);
        EXPECT(result.reconciliation_required);
        expect_mapless(result, live);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(1);
        fixture.source.change_on_read_call = 2;
        fixture.source.changed_generation = 2;
        auto live = active_guard();
        auto invalid_candidate = candidate_package();
        invalid_candidate.integrity_verified = false;
        const auto result = fixture.coordinator.activate(
            live, policy(), invalid_candidate, 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::final_source_changed);
        EXPECT(result.reconciliation_required);
        expect_mapless(result, live);
    }
}

void test_selector_failures_do_not_advance() {
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(1);
        fixture.selector_storage.fail_read_call = 1;
        auto live = active_guard();
        const auto result = fixture.coordinator.activate(
            live, policy(), candidate_package(), 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::candidate_failed);
        EXPECT(fixture.source.advance_calls == 0);
        EXPECT(fixture.domain_storage.write_calls == 0);
        expect_mapless(result, live);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(1);
        fixture.selector_storage.fail_write = true;
        auto live = active_guard();
        const auto result = fixture.coordinator.activate(
            live, policy(), candidate_package(), 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::candidate_failed);
        EXPECT(fixture.source.advance_calls == 0);
        EXPECT(fixture.domain_storage.write_calls == 0);
        expect_mapless(result, live);
    }
}

void test_races_before_protected_advance() {
    {
        Fixture fixture{};
        const auto record = active_fresh();
        fixture.seed_domain(record);
        fixture.seed_selector(1);
        auto changed = record;
        changed.record_generation = 3;
        fixture.domain_storage.replacement_slots[0] =
            domain_bytes(changed);
        fixture.domain_storage.replacement_slots[1] =
            domain_bytes(changed);
        fixture.domain_storage.replacement_present = {true, true};
        fixture.domain_storage.replace_on_read_call = 3;
        auto live = active_guard();
        const auto result = fixture.coordinator.activate(
            live, policy(), candidate_package(), 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::domain_changed);
        EXPECT(result.selector_persisted);
        EXPECT(fixture.source.advance_calls == 0);
        EXPECT(fixture.domain_storage.write_calls == 0);
        expect_mapless(result, live);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(1);
        const auto changed = selector_bytes(active_guard(), 3);
        fixture.selector_storage.replacement_slots[0] = changed;
        fixture.selector_storage.replacement_slots[1] = changed;
        fixture.selector_storage.replacement_present = {true, true};
        fixture.selector_storage.replace_on_read_call = 6;
        auto live = active_guard();
        const auto result = fixture.coordinator.activate(
            live, policy(), candidate_package(), 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::selector_changed);
        EXPECT(fixture.source.advance_calls == 0);
        EXPECT(fixture.domain_storage.write_calls == 0);
        expect_mapless(result, live);
    }
}

void test_protected_advance_failures() {
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(1);
        fixture.source.advance_error =
            MapSelectorDomainProtectedSourceError::io_failure;
        auto live = active_guard();
        const auto result = fixture.coordinator.activate(
            live, policy(), candidate_package(), 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::protected_advance_failed);
        EXPECT(result.selector_persisted);
        EXPECT(fixture.source.selector_generation == 1);
        EXPECT(fixture.domain_storage.write_calls == 0);
        expect_mapless(result, live);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(1);
        fixture.source.advance_error =
            MapSelectorDomainProtectedSourceError::io_failure;
        fixture.source.apply_then_fail = true;
        auto live = active_guard();
        const auto result = fixture.coordinator.activate(
            live, policy(), candidate_package(), 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::protected_advance_failed);
        EXPECT(fixture.source.selector_generation == 2);
        EXPECT(fixture.domain_storage.write_calls == 0);
        expect_mapless(result, live);
    }
}

void test_protected_readback_failures() {
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(1);
        fixture.source.fail_read_call = 2;
        auto live = active_guard();
        const auto result = fixture.coordinator.activate(
            live, policy(), candidate_package(), 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::protected_readback_failed);
        EXPECT(fixture.domain_storage.write_calls == 0);
        expect_mapless(result, live);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(1);
        fixture.source.wrong_readback = true;
        auto live = active_guard();
        const auto result = fixture.coordinator.activate(
            live, policy(), candidate_package(), 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::
                   protected_readback_mismatch);
        EXPECT(fixture.domain_storage.write_calls == 0);
        expect_mapless(result, live);
    }
}

void test_domain_save_failures() {
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(1);
        fixture.domain_storage.fail_write = true;
        auto live = active_guard();
        const auto result = fixture.coordinator.activate(
            live, policy(), candidate_package(), 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::domain_save_failed);
        EXPECT(result.protected_source_verified);
        EXPECT(fixture.source.selector_generation == 2);
        expect_mapless(result, live);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(1);
        fixture.domain_storage.fail_commit = true;
        fixture.domain_storage.commit_then_fail = true;
        auto live = active_guard();
        const auto result = fixture.coordinator.activate(
            live, policy(), candidate_package(), 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::domain_commit_uncertain);
        EXPECT(result.domain_save.commit_uncertain);
        expect_mapless(result, live);
    }
}

void test_final_rechecks_block_publication() {
    {
        Fixture fixture{};
        const auto record = active_fresh();
        fixture.seed_domain(record);
        fixture.seed_selector(1);
        auto changed = record;
        changed.accepted_selector_generation = 2;
        changed.record_generation = 4;
        fixture.domain_storage.replacement_slots[0] =
            domain_bytes(changed);
        fixture.domain_storage.replacement_slots[1] =
            domain_bytes(changed);
        fixture.domain_storage.replacement_present = {true, true};
        fixture.domain_storage.replace_on_read_call = 8;
        auto live = active_guard();
        const auto result = fixture.coordinator.activate(
            live, policy(), candidate_package(), 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::final_domain_changed);
        EXPECT(result.domain_generation_saved);
        expect_mapless(result, live);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(1);
        const auto changed = selector_bytes(active_guard(), 3);
        fixture.selector_storage.replacement_slots[0] = changed;
        fixture.selector_storage.replacement_slots[1] = changed;
        fixture.selector_storage.replacement_present = {true, true};
        fixture.selector_storage.replace_on_read_call = 10;
        auto live = active_guard();
        const auto result = fixture.coordinator.activate(
            live, policy(), candidate_package(), 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::final_selector_changed);
        EXPECT(result.domain_generation_verified);
        expect_mapless(result, live);
    }
    {
        Fixture fixture{};
        fixture.seed_domain(active_fresh());
        fixture.seed_selector(1);
        fixture.source.change_on_read_call = 3;
        fixture.source.changed_generation = 3;
        auto live = active_guard();
        const auto result = fixture.coordinator.activate(
            live, policy(), candidate_package(), 100);
        EXPECT(result.reason ==
               MapSelectorDomainCandidateReason::final_source_changed);
        EXPECT(result.domain_generation_verified);
        EXPECT(result.selector_verified);
        expect_mapless(result, live);
    }
}

void test_degraded_domain_is_repaired_by_successor() {
    Fixture fixture{};
    fixture.seed_domain(active_fresh(), false);
    fixture.seed_selector(1);
    auto live = active_guard();

    const auto result = fixture.coordinator.activate(
        live, policy(), candidate_package(), 100);

    EXPECT(result.committed());
    EXPECT(result.domain_repair_required);
    const auto final = fixture.domain_store.inspect();
    EXPECT(final.error == MapSelectorDomainStoreError::none);
    EXPECT(final.slot_a == MapSelectorDomainSlotState::valid);
    EXPECT(final.slot_b == MapSelectorDomainSlotState::valid);
}

}  // namespace

int main() {
    test_successful_candidate_ordering();
    test_replacement_domain_candidate();
    test_live_and_policy_isolation();
    test_domain_and_source_preflight();
    test_candidate_rejection_preserves_exact_baseline();
    test_rejection_recheck_change_contains_map();
    test_selector_failures_do_not_advance();
    test_races_before_protected_advance();
    test_protected_advance_failures();
    test_protected_readback_failures();
    test_domain_save_failures();
    test_final_rechecks_block_publication();
    test_degraded_domain_is_repaired_by_successor();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 13 map selector domain-candidate scenario groups\n";
    return EXIT_SUCCESS;
}
