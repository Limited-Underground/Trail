#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_checkpoint.hpp"
#include "opentrail/map_selector_reseed_authorization.hpp"
#include "opentrail/map_selector_store.hpp"
#include "opentrail/map_selector_trusted_reseed.hpp"

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
        if (inject_on_read_call != 0 &&
            read_calls == inject_on_read_call) {
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
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t commit_calls{0};
    std::uint32_t erase_calls{0};
    std::uint32_t inject_on_read_call{0};
    std::uint8_t inject_slot{1};
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> inject_bytes{};
};

class FakeTrustedSource final
    : public MapSelectorTrustedGenerationSource {
public:
    MapSelectorTrustedGenerationRead read() override {
        const auto index = read_calls;
        ++read_calls;
        if (index < storage_reads_seen.size() && storage != nullptr) {
            storage_reads_seen[index] = storage->read_calls;
        }
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
        if (storage != nullptr) {
            reads_seen_at_advance = storage->read_calls;
            erases_seen_at_advance = storage->erase_calls;
            writes_seen_at_advance = storage->write_calls;
            commits_seen_at_advance = storage->commit_calls;
        }
        if (live_guard != nullptr) {
            map_exposure_seen_at_advance =
                live_guard->status().map_available;
        }
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
    MapActivationGuard* live_guard{nullptr};
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
    std::uint32_t reads_seen_at_advance{0};
    std::uint32_t erases_seen_at_advance{0};
    std::uint32_t writes_seen_at_advance{0};
    std::uint32_t commits_seen_at_advance{0};
    std::array<std::uint32_t, 8> storage_reads_seen{};
    bool map_exposure_seen_at_advance{false};
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

MapActivationGuard mapless_guard(
    MapSelectorState selector = MapSelectorState::ambiguous) {
    MapActivationGuard guard{};
    EXPECT(guard.start(policy(), {selector, {}}) ==
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

MapSelectorTrustedReseedContext context(
    const MapActivationPolicy& activation_policy = policy()) {
    return {activation_policy, 73, 150};
}

class PermitBackend final : public MapSelectorReseedAuthorizationBackend {
public:
    explicit PermitBackend(MapSelectorReseedAuthorizationGrant grant)
        : grant_(grant) {}

    MapSelectorReseedAuthorizationGrant verify_and_consume(
        std::uint64_t,
        const MapSelectorReseedAuthorizationBinding&) override {
        return grant_;
    }

private:
    MapSelectorReseedAuthorizationGrant grant_{};
};

MapSelectorReseedPermit permit_for(
    const MapSelectorTrustedReseedContext& ctx,
    std::uint64_t trusted_generation,
    const MapPackageEvidence& baseline = package()) {
    const MapSelectorReseedAuthorizationBinding binding{
        ctx.policy, trusted_generation, baseline};
    const MapSelectorReseedAuthorizationGrant grant{
        MapSelectorReseedAuthorizationBackendState::authorized,
        MapSelectorReseedAuthorizationScope::selector_reseed,
        MapSelectorReseedServiceTransport::local_usb,
        41,
        ctx.boot_session_id,
        100,
        200,
        9,
        binding,
        {true, true, true, true, true}};
    PermitBackend backend{grant};
    MapSelectorReseedAuthorizer authorizer{backend};
    MapSelectorReseedPermit permit{};
    const auto result = authorizer.authorize(
        {100},
        {41, ctx.boot_session_id, ctx.authorization_use_time_ms},
        binding,
        permit);
    EXPECT(result.authorized());
    EXPECT(permit.available());
    return permit;
}

class Fixture {
public:
    Fixture()
        : store(storage),
          reseed(store),
          trusted(source),
          coordinator(reseed, trusted) {
        source.storage = &storage;
    }

    void bind_live(MapActivationGuard& live) {
        source.live_guard = &live;
    }

    void seed_current(std::uint64_t generation = 5) {
        storage.seed(0, encode_guard(active_guard(), generation));
    }

    FakeStorage storage{};
    MapSelectorStore store;
    MapSelectorReseedCoordinator reseed;
    FakeTrustedSource source{};
    MapSelectorTrustedGeneration trusted;
    MapSelectorTrustedReseedCoordinator coordinator;
};

void test_selector_reseed_advances_trust_before_map_publish() {
    auto live = mapless_guard();
    Fixture fixture{};
    fixture.bind_live(live);
    fixture.seed_current(5);
    fixture.source.generation = 3;
    auto permit = permit_for(context(), 3);

    const auto result = fixture.coordinator.reseed(
        live, context(), package(), permit);
    EXPECT(result.committed());
    EXPECT(result.reseed.active_record_generation == 6);
    EXPECT(result.reseed.generation_base == 5);
    EXPECT(result.trusted_generation_before == 3);
    EXPECT(result.trusted_generation_after == 6);
    EXPECT(fixture.source.storage_reads_seen[0] == 0);
    EXPECT(fixture.source.observed_expected == 3);
    EXPECT(fixture.source.observed_requested == 6);
    EXPECT(fixture.source.erases_seen_at_advance == 2);
    EXPECT(fixture.source.writes_seen_at_advance == 1);
    EXPECT(fixture.source.commits_seen_at_advance == 1);
    EXPECT(!fixture.source.map_exposure_seen_at_advance);
    EXPECT(!permit.available());
    EXPECT(live.status().state == MapActivationState::active);
    EXPECT(result.map_exposure_allowed);
}

void test_empty_selector_reseeds_above_nonzero_protected_history() {
    auto live = mapless_guard();
    Fixture fixture{};
    fixture.bind_live(live);
    fixture.source.generation = 8;
    auto permit = permit_for(context(), 8);

    const auto result = fixture.coordinator.reseed(
        live, context(), package(), permit);
    EXPECT(result.committed());
    EXPECT(result.reseed.prior_observed_record_generation == 0);
    EXPECT(result.reseed.generation_base == 8);
    EXPECT(result.reseed.active_record_generation == 9);
    EXPECT(fixture.source.generation == 9);
    EXPECT(live.status().state == MapActivationState::active);
}

void test_initial_trust_failure_blocks_store_and_preserves_permit() {
    auto live = mapless_guard();
    Fixture fixture{};
    fixture.bind_live(live);
    fixture.source.generation = 4;
    fixture.source.fail_on_read = 1;
    auto permit = permit_for(context(), 4);

    const auto result = fixture.coordinator.reseed(
        live, context(), package(), permit);
    EXPECT(result.reason == MapSelectorTrustedReseedReason::
                                trusted_source_unavailable);
    EXPECT(!result.reconciliation_required);
    EXPECT(fixture.storage.read_calls == 0);
    EXPECT(fixture.storage.erase_calls == 0);
    EXPECT(fixture.storage.write_calls == 0);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(permit.available());
    EXPECT(live.status().state == MapActivationState::mapless);
}

void test_latched_trust_failure_contains_without_store_or_permit_use() {
    auto live = mapless_guard(MapSelectorState::unreadable);
    Fixture fixture{};
    fixture.bind_live(live);
    fixture.source.generation = 4;
    fixture.source.advance_error =
        MapSelectorTrustedGenerationSourceError::io_failure;
    fixture.source.apply_on_error = true;
    const auto latched = fixture.trusted.advance_exact(4, 5);
    EXPECT(latched.reconciliation_required);
    const auto reads_before = fixture.source.read_calls;
    auto permit = permit_for(context(), 4);

    const auto result = fixture.coordinator.reseed(
        live, context(), package(), permit);
    EXPECT(result.reason == MapSelectorTrustedReseedReason::
                                trusted_source_unavailable);
    EXPECT(result.reconciliation_required);
    EXPECT(fixture.source.read_calls == reads_before);
    EXPECT(fixture.storage.read_calls == 0);
    EXPECT(permit.available());
    EXPECT(live.status().reason ==
           MapActivationReason::selector_ambiguous);
}

void test_nonservice_live_states_touch_no_backend_or_permit() {
    auto active = active_guard();
    MapActivationGuard stopped{};
    MapActivationGuard* guards[] = {&active, &stopped};

    for (auto* live : guards) {
        Fixture fixture{};
        fixture.bind_live(*live);
        fixture.source.generation = 4;
        auto permit = permit_for(context(), 4);
        const auto result = fixture.coordinator.reseed(
            *live, context(), package(), permit);
        EXPECT(result.reason == MapSelectorTrustedReseedReason::
                                    live_state_not_service_mapless);
        EXPECT(fixture.source.read_calls == 0);
        EXPECT(fixture.storage.read_calls == 0);
        EXPECT(fixture.storage.erase_calls == 0);
        EXPECT(permit.available());
    }
}

void test_permit_binding_and_clean_first_use_fail_before_mutation() {
    auto mismatch_live = mapless_guard();
    Fixture mismatch{};
    mismatch.bind_live(mismatch_live);
    mismatch.source.generation = 4;
    auto wrong_permit = permit_for(context(), 3);
    const auto wrong = mismatch.coordinator.reseed(
        mismatch_live, context(), package(), wrong_permit);
    EXPECT(wrong.reason == MapSelectorTrustedReseedReason::reseed_failed);
    EXPECT(wrong.reseed.reason == MapSelectorReseedReason::
                                      authorization_binding_mismatch);
    EXPECT(!wrong_permit.available());
    EXPECT(mismatch.storage.read_calls == 0);
    EXPECT(mismatch.source.advance_calls == 0);

    auto first_live = mapless_guard(MapSelectorState::missing);
    Fixture first{};
    first.bind_live(first_live);
    auto first_permit = permit_for(context(), 0);
    const auto routed = first.coordinator.reseed(
        first_live, context(), package(), first_permit);
    EXPECT(routed.reason == MapSelectorTrustedReseedReason::reseed_failed);
    EXPECT(routed.reseed.reason ==
           MapSelectorReseedReason::first_use_requires_baseline);
    EXPECT(first.storage.erase_calls == 0);
    EXPECT(first.storage.write_calls == 0);
    EXPECT(first.source.advance_calls == 0);
    EXPECT(first_live.status().reason == MapActivationReason::no_selector);
}

void test_policy_storage_and_exhaustion_fail_before_trust_advance() {
    auto wrong_live = mapless_guard();
    Fixture wrong{};
    wrong.bind_live(wrong_live);
    wrong.source.generation = 4;
    auto wrong_policy = policy();
    wrong_policy.maximum_trial_boots = 2;
    auto wrong_permit = permit_for(context(wrong_policy), 4);
    const auto policy_failed = wrong.coordinator.reseed(
        wrong_live,
        context(wrong_policy),
        package(),
        wrong_permit);
    EXPECT(policy_failed.reseed.reason ==
           MapSelectorReseedReason::invalid_policy);
    EXPECT(wrong.source.advance_calls == 0);
    EXPECT(wrong.storage.erase_calls == 0);
    EXPECT(!wrong_live.status().running);

    auto unavailable_live = mapless_guard();
    Fixture unavailable{};
    unavailable.bind_live(unavailable_live);
    unavailable.source.generation = 4;
    unavailable.storage.fail_read_slot = 1;
    auto unavailable_permit = permit_for(context(), 4);
    const auto storage_failed = unavailable.coordinator.reseed(
        unavailable_live, context(), package(), unavailable_permit);
    EXPECT(storage_failed.reseed.reason ==
           MapSelectorReseedReason::storage_unavailable);
    EXPECT(unavailable.source.advance_calls == 0);
    EXPECT(unavailable.storage.erase_calls == 0);

    auto exhausted_live = mapless_guard();
    Fixture exhausted{};
    exhausted.bind_live(exhausted_live);
    exhausted.seed_current(std::numeric_limits<std::uint64_t>::max());
    exhausted.source.generation = 4;
    auto exhausted_permit = permit_for(context(), 4);
    const auto exhausted_result = exhausted.coordinator.reseed(
        exhausted_live, context(), package(), exhausted_permit);
    EXPECT(exhausted_result.reseed.reason ==
           MapSelectorReseedReason::generation_exhausted);
    EXPECT(exhausted.source.advance_calls == 0);
    EXPECT(exhausted.storage.erase_calls == 0);
}

void test_clear_failures_never_advance_protected_history() {
    auto partial_live = mapless_guard();
    Fixture partial{};
    partial.bind_live(partial_live);
    partial.seed_current();
    partial.source.generation = 3;
    partial.storage.fail_erase_slot = 0;
    auto partial_permit = permit_for(context(), 3);
    const auto partial_result = partial.coordinator.reseed(
        partial_live, context(), package(), partial_permit);
    EXPECT(partial_result.reseed.reason ==
           MapSelectorReseedReason::selector_reset_failed);
    EXPECT(partial_result.reconciliation_required);
    EXPECT(partial.source.advance_calls == 0);
    EXPECT(partial_live.status().reason ==
           MapActivationReason::selector_ambiguous);

    auto retained_live = mapless_guard();
    Fixture retained{};
    retained.bind_live(retained_live);
    retained.seed_current();
    retained.source.generation = 3;
    retained.storage.pretend_erase_success_slot = 0;
    auto retained_permit = permit_for(context(), 3);
    const auto retained_result = retained.coordinator.reseed(
        retained_live, context(), package(), retained_permit);
    EXPECT(retained_result.reseed.reason ==
           MapSelectorReseedReason::selector_reset_unverified);
    EXPECT(retained.source.advance_calls == 0);
    EXPECT(retained.storage.write_calls == 0);
}

void test_save_failures_never_advance_protected_history() {
    auto write_live = mapless_guard();
    Fixture write{};
    write.bind_live(write_live);
    write.seed_current();
    write.source.generation = 3;
    write.storage.fail_write_slot = 0;
    auto write_permit = permit_for(context(), 3);
    const auto write_result = write.coordinator.reseed(
        write_live, context(), package(), write_permit);
    EXPECT(write_result.reseed.reason ==
           MapSelectorReseedReason::checkpoint_save_failed);
    EXPECT(write.source.advance_calls == 0);
    EXPECT(!write_result.map_exposure_allowed);

    auto commit_live = mapless_guard();
    Fixture commit{};
    commit.bind_live(commit_live);
    commit.seed_current();
    commit.source.generation = 3;
    commit.storage.fail_commit_slot = 0;
    commit.storage.commit_then_fail = true;
    auto commit_permit = permit_for(context(), 3);
    const auto commit_result = commit.coordinator.reseed(
        commit_live, context(), package(), commit_permit);
    EXPECT(commit_result.reseed.reason ==
           MapSelectorReseedReason::checkpoint_commit_uncertain);
    EXPECT(commit_result.reconciliation_required);
    EXPECT(commit.source.advance_calls == 0);
}

void test_selector_race_after_clear_never_advances_trust() {
    auto live = mapless_guard();
    Fixture fixture{};
    fixture.bind_live(live);
    fixture.seed_current();
    fixture.source.generation = 3;
    fixture.storage.inject_on_read_call = 5;
    fixture.storage.inject_slot = 1;
    fixture.storage.inject_bytes = encode_guard(active_guard(), 9);
    auto permit = permit_for(context(), 3);

    const auto result = fixture.coordinator.reseed(
        live, context(), package(), permit);
    EXPECT(result.reseed.reason == MapSelectorReseedReason::selector_changed);
    EXPECT(result.reconciliation_required);
    EXPECT(fixture.storage.write_calls == 0);
    EXPECT(fixture.source.advance_calls == 0);
    EXPECT(live.status().reason == MapActivationReason::selector_ambiguous);
}

void test_uncertain_trust_advance_contains_saved_reseed() {
    auto live = mapless_guard();
    Fixture fixture{};
    fixture.bind_live(live);
    fixture.seed_current();
    fixture.source.generation = 3;
    fixture.source.advance_error =
        MapSelectorTrustedGenerationSourceError::io_failure;
    fixture.source.apply_on_error = true;
    auto permit = permit_for(context(), 3);

    const auto result = fixture.coordinator.reseed(
        live, context(), package(), permit);
    EXPECT(result.reason == MapSelectorTrustedReseedReason::
                                trusted_advance_failed);
    EXPECT(result.reseed.committed());
    EXPECT(result.reconciliation_required);
    EXPECT(fixture.store.inspect().generation == 6);
    EXPECT(fixture.source.generation == 6);
    EXPECT(!result.map_exposure_allowed);
    EXPECT(live.status().reason == MapActivationReason::selector_ambiguous);
}

void test_protected_conflict_or_readback_change_contains_saved_reseed() {
    auto conflict_live = mapless_guard();
    Fixture conflict{};
    conflict.bind_live(conflict_live);
    conflict.seed_current();
    conflict.source.generation = 3;
    conflict.source.jump_on_read = 2;
    conflict.source.jump_to = 6;
    auto conflict_permit = permit_for(context(), 3);
    const auto conflict_result = conflict.coordinator.reseed(
        conflict_live, context(), package(), conflict_permit);
    EXPECT(conflict_result.reason == MapSelectorTrustedReseedReason::
                                         trusted_advance_failed);
    EXPECT(conflict_result.reseed.committed());
    EXPECT(conflict_result.trusted_after.reason ==
           MapSelectorTrustedGenerationReason::generation_conflict);
    EXPECT(conflict.source.advance_calls == 0);
    EXPECT(!conflict_result.map_exposure_allowed);

    auto readback_live = mapless_guard();
    Fixture readback{};
    readback.bind_live(readback_live);
    readback.seed_current();
    readback.source.generation = 3;
    readback.source.jump_on_read = 3;
    readback.source.jump_to = 7;
    auto readback_permit = permit_for(context(), 3);
    const auto readback_result = readback.coordinator.reseed(
        readback_live, context(), package(), readback_permit);
    EXPECT(readback_result.reason == MapSelectorTrustedReseedReason::
                                         trusted_advance_failed);
    EXPECT(readback_result.trusted_after.reason ==
           MapSelectorTrustedGenerationReason::readback_mismatch);
    EXPECT(readback.source.advance_calls == 1);
    EXPECT(readback_result.reconciliation_required);
    EXPECT(readback_live.status().reason ==
           MapActivationReason::selector_ambiguous);
}

}  // namespace

int main() {
    test_selector_reseed_advances_trust_before_map_publish();
    test_empty_selector_reseeds_above_nonzero_protected_history();
    test_initial_trust_failure_blocks_store_and_preserves_permit();
    test_latched_trust_failure_contains_without_store_or_permit_use();
    test_nonservice_live_states_touch_no_backend_or_permit();
    test_permit_binding_and_clean_first_use_fail_before_mutation();
    test_policy_storage_and_exhaustion_fail_before_trust_advance();
    test_clear_failures_never_advance_protected_history();
    test_save_failures_never_advance_protected_history();
    test_selector_race_after_clear_never_advances_trust();
    test_uncertain_trust_advance_contains_saved_reseed();
    test_protected_conflict_or_readback_change_contains_saved_reseed();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector trusted-reseed assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 12 map selector trusted-reseed scenario groups\n";
    return EXIT_SUCCESS;
}
