#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/update_checkpoint.hpp"
#include "opentrail/update_recovery_transition.hpp"

namespace {

using namespace opentrail::update;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

class FakeStorage final : public UpdateCheckpointStorage {
public:
    UpdateCheckpointStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override {
        if (slot >= slots.size() || output == nullptr ||
            size != kUpdateCheckpointRecordBytes) {
            return UpdateCheckpointStorageError::invalid_argument;
        }
        ++read_count;
        if (fail_read_slot == static_cast<int>(slot)) {
            return UpdateCheckpointStorageError::io_failure;
        }
        if (!present[slot]) {
            return UpdateCheckpointStorageError::not_found;
        }
        std::copy(slots[slot].begin(), slots[slot].end(), output);
        return UpdateCheckpointStorageError::none;
    }

    UpdateCheckpointStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) override {
        if (slot >= slots.size() || data == nullptr ||
            size != kUpdateCheckpointRecordBytes) {
            return UpdateCheckpointStorageError::invalid_argument;
        }
        ++write_count;
        if (fail_write_slot == static_cast<int>(slot)) {
            return UpdateCheckpointStorageError::io_failure;
        }
        std::copy(data, data + size, slots[slot].begin());
        present[slot] = true;
        return UpdateCheckpointStorageError::none;
    }

    UpdateCheckpointStorageError erase_slot(std::uint8_t slot) override {
        if (slot >= slots.size()) {
            return UpdateCheckpointStorageError::invalid_argument;
        }
        present[slot] = false;
        slots[slot].fill(0);
        return UpdateCheckpointStorageError::none;
    }

    void seed(
        std::uint8_t slot,
        const std::array<std::uint8_t, kUpdateCheckpointRecordBytes>& bytes) {
        slots[slot] = bytes;
        present[slot] = true;
    }

    std::array<std::array<std::uint8_t, kUpdateCheckpointRecordBytes>, 2>
        slots{};
    std::array<bool, 2> present{};
    int fail_read_slot{-1};
    int fail_write_slot{-1};
    std::size_t read_count{0};
    std::size_t write_count{0};
};

class FakeTrustedGeneration final : public UpdateTrustedGenerationSource {
public:
    UpdateTrustedGenerationRead read() override {
        ++read_count;
        return {read_error, generation};
    }

    UpdateTrustedGenerationError advance_to(
        std::uint64_t requested_generation) override {
        ++advance_count;
        if (advance_error != UpdateTrustedGenerationError::none) {
            return advance_error;
        }
        if (!freeze_after_advance) {
            generation = requested_generation;
        }
        return UpdateTrustedGenerationError::none;
    }

    UpdateTrustedGenerationError read_error{
        UpdateTrustedGenerationError::none};
    UpdateTrustedGenerationError advance_error{
        UpdateTrustedGenerationError::none};
    std::uint64_t generation{1};
    std::size_t read_count{0};
    std::size_t advance_count{0};
    bool freeze_after_advance{false};
};

UpdateGuardPolicy policy() {
    return {
        0x1234U,
        10,
        ImageSlot::slot_a,
        health_bit(TrialHealth::runtime_started) |
            health_bit(TrialHealth::watchdog_healthy) |
            health_bit(TrialHealth::configuration_loaded),
        100,
        500,
        3,
        4U * 1024U * 1024U};
}

VerifiedUpdateCandidate candidate() {
    return {
        0x1234U,
        11,
        ImageSlot::slot_b,
        1024U * 1024U,
        true,
        true,
        true,
        true};
}

UpdateBootGuard trial_guard() {
    UpdateBootGuard guard{};
    EXPECT(guard.start(policy()) == UpdateGuardError::none);
    EXPECT(guard.stage(candidate()) == UpdateGuardError::none);
    EXPECT(guard.mark_written({11, ImageSlot::slot_b, true, true}) ==
           UpdateGuardError::none);
    EXPECT(guard.begin_boot({41, 11, ImageSlot::slot_b, 10}) ==
           UpdateGuardError::none);
    return guard;
}

std::array<std::uint8_t, kUpdateCheckpointRecordBytes> encode_guard(
    const UpdateBootGuard& guard,
    std::uint64_t generation) {
    UpdateGuardCheckpoint checkpoint{};
    EXPECT(guard.export_checkpoint(generation, checkpoint) ==
           UpdateGuardError::none);
    std::array<std::uint8_t, kUpdateCheckpointRecordBytes> bytes{};
    EXPECT(encode_update_checkpoint(
               checkpoint, bytes.data(), bytes.size()).succeeded());
    return bytes;
}

void seed_current(FakeStorage& storage, const UpdateBootGuard& guard) {
    storage.seed(0, encode_guard(guard, 1));
}

void test_confirmation_is_published_only_after_commit() {
    auto live = trial_guard();
    EXPECT(live.report_health(
               41, policy().required_health_mask, 100) ==
           UpdateGuardError::none);
    FakeStorage storage{};
    seed_current(storage, live);
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    UpdateRecoveryTransitionCoordinator coordinator{store, trusted};

    const auto result = coordinator.confirm(live, 41, 110);
    EXPECT(result.committed());
    EXPECT(result.guard_error == UpdateGuardError::none);
    EXPECT(result.before_state == UpdateState::trial);
    EXPECT(result.attempted_state == UpdateState::confirmed);
    EXPECT(result.live_state == UpdateState::confirmed);
    EXPECT(result.persistence_required);
    EXPECT(!result.live_guard_stopped);
    EXPECT(live.status().running);
    EXPECT(live.status().state == UpdateState::confirmed);
    EXPECT(storage.write_count == 1);
    EXPECT(trusted.generation == 2);
}

void test_explicit_rollback_is_published_only_after_commit() {
    auto live = trial_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    UpdateRecoveryTransitionCoordinator coordinator{store, trusted};

    const auto result = coordinator.request_rollback(
        live, RollbackReason::explicit_health_failure);
    EXPECT(result.committed());
    EXPECT(result.guard_error == UpdateGuardError::none);
    EXPECT(result.attempted_state == UpdateState::rollback_required);
    EXPECT(live.status().state == UpdateState::rollback_required);
    EXPECT(live.status().rollback_reason ==
           RollbackReason::explicit_health_failure);
    EXPECT(storage.write_count == 1);
    EXPECT(trusted.generation == 2);
}

void test_boot_local_health_and_time_do_not_write_checkpoint() {
    auto live = trial_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    UpdateRecoveryTransitionCoordinator coordinator{store, trusted};

    const auto health = coordinator.report_health(
        live, 41, health_bit(TrialHealth::runtime_started), 50);
    EXPECT(health.state == UpdateTransitionState::applied_volatile);
    EXPECT(health.guard_error == UpdateGuardError::none);
    EXPECT(!health.persistence_required);
    EXPECT(live.status().observed_health_mask ==
           health_bit(TrialHealth::runtime_started));
    EXPECT(live.status().last_monotonic_ms == 50);

    const auto ticked = coordinator.tick(live, 60);
    EXPECT(ticked.state == UpdateTransitionState::applied_volatile);
    EXPECT(live.status().last_monotonic_ms == 60);
    EXPECT(storage.write_count == 0);
    EXPECT(trusted.read_count == 0);
}

void test_rejected_confirmation_retains_clock_evidence_without_write() {
    auto live = trial_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    UpdateRecoveryTransitionCoordinator coordinator{store, trusted};

    const auto early = coordinator.confirm(live, 41, 50);
    EXPECT(early.state == UpdateTransitionState::rejected);
    EXPECT(early.guard_error ==
           UpdateGuardError::insufficient_stable_time);
    EXPECT(!early.persistence_required);
    EXPECT(live.status().last_monotonic_ms == 50);

    const auto unhealthy = coordinator.confirm(live, 41, 110);
    EXPECT(unhealthy.state == UpdateTransitionState::rejected);
    EXPECT(unhealthy.guard_error == UpdateGuardError::insufficient_health);
    EXPECT(live.status().last_monotonic_ms == 110);
    EXPECT(storage.write_count == 0);
    EXPECT(trusted.read_count == 0);
}

void test_tick_timeout_durably_publishes_rollback() {
    auto live = trial_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    UpdateRecoveryTransitionCoordinator coordinator{store, trusted};

    const auto result = coordinator.tick(live, 510);
    EXPECT(result.committed());
    EXPECT(result.guard_error == UpdateGuardError::confirmation_timeout);
    EXPECT(result.attempted_state == UpdateState::rollback_required);
    EXPECT(live.status().state == UpdateState::rollback_required);
    EXPECT(live.status().rollback_reason ==
           RollbackReason::confirmation_timeout);
}

void test_health_timeout_durably_publishes_rollback() {
    auto live = trial_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    UpdateRecoveryTransitionCoordinator coordinator{store, trusted};

    const auto result = coordinator.report_health(
        live, 41, policy().required_health_mask, 510);
    EXPECT(result.committed());
    EXPECT(result.guard_error == UpdateGuardError::confirmation_timeout);
    EXPECT(live.status().state == UpdateState::rollback_required);
    EXPECT(live.status().rollback_reason ==
           RollbackReason::confirmation_timeout);
}

void test_invalid_requests_do_not_touch_storage_or_trust() {
    auto live = trial_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    UpdateRecoveryTransitionCoordinator coordinator{store, trusted};

    const auto wrong_session = coordinator.confirm(live, 99, 110);
    EXPECT(wrong_session.state == UpdateTransitionState::rejected);
    EXPECT(wrong_session.guard_error == UpdateGuardError::invalid_state);
    EXPECT(live.status().state == UpdateState::trial);

    const auto invalid_reason = coordinator.request_rollback(
        live, RollbackReason::none);
    EXPECT(invalid_reason.state == UpdateTransitionState::rejected);
    EXPECT(invalid_reason.guard_error == UpdateGuardError::invalid_state);
    EXPECT(storage.write_count == 0);
    EXPECT(trusted.read_count == 0);
}

void test_uncertain_write_stops_live_guard_without_publishing_attempt() {
    auto live = trial_guard();
    FakeStorage storage{};
    seed_current(storage, live);
    storage.fail_write_slot = 1;
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    UpdateRecoveryTransitionCoordinator coordinator{store, trusted};

    const auto result = coordinator.request_rollback(
        live, RollbackReason::explicit_health_failure);
    EXPECT(result.state ==
           UpdateTransitionState::reboot_reconcile_required);
    EXPECT(result.persistence.reason ==
           UpdatePersistenceReason::commit_uncertain);
    EXPECT(result.attempted_state == UpdateState::rollback_required);
    EXPECT(result.live_state == UpdateState::trial);
    EXPECT(result.live_guard_stopped);
    EXPECT(!live.status().running);
    EXPECT(live.status().state == UpdateState::trial);
    EXPECT(trusted.advance_count == 0);
}

void test_generation_mismatch_stops_live_guard_without_write() {
    auto behind_live = trial_guard();
    FakeStorage behind_storage{};
    seed_current(behind_storage, behind_live);
    UpdateCheckpointStore behind_store{behind_storage};
    FakeTrustedGeneration ahead_trust{};
    ahead_trust.generation = 2;
    UpdateRecoveryTransitionCoordinator behind{
        behind_store, ahead_trust};
    const auto rollback = behind.request_rollback(
        behind_live, RollbackReason::explicit_health_failure);
    EXPECT(rollback.state == UpdateTransitionState::safe_mode);
    EXPECT(rollback.persistence.reason ==
           UpdatePersistenceReason::rollback_detected);
    EXPECT(!behind_live.status().running);
    EXPECT(behind_storage.write_count == 0);

    auto ahead_live = trial_guard();
    FakeStorage ahead_storage{};
    ahead_storage.seed(0, encode_guard(ahead_live, 2));
    UpdateCheckpointStore ahead_store{ahead_storage};
    FakeTrustedGeneration behind_trust{};
    UpdateRecoveryTransitionCoordinator ahead{ahead_store, behind_trust};
    const auto reconcile = ahead.request_rollback(
        ahead_live, RollbackReason::explicit_health_failure);
    EXPECT(reconcile.state ==
           UpdateTransitionState::reboot_reconcile_required);
    EXPECT(reconcile.persistence.reason ==
           UpdatePersistenceReason::trusted_reconciliation_required);
    EXPECT(!ahead_live.status().running);
    EXPECT(ahead_storage.write_count == 0);
}

void test_post_write_trust_failure_stops_live_guard() {
    auto advance_live = trial_guard();
    FakeStorage advance_storage{};
    seed_current(advance_storage, advance_live);
    UpdateCheckpointStore advance_store{advance_storage};
    FakeTrustedGeneration failed_trust{};
    failed_trust.advance_error = UpdateTrustedGenerationError::io_failure;
    UpdateRecoveryTransitionCoordinator advance{
        advance_store, failed_trust};
    const auto advance_result = advance.request_rollback(
        advance_live, RollbackReason::explicit_health_failure);
    EXPECT(advance_result.state ==
           UpdateTransitionState::reboot_reconcile_required);
    EXPECT(advance_result.persistence.reason ==
           UpdatePersistenceReason::trusted_advance_failed);
    EXPECT(advance_result.persistence.save.saved());
    EXPECT(!advance_live.status().running);
    EXPECT(advance_live.status().state == UpdateState::trial);

    auto readback_live = trial_guard();
    FakeStorage readback_storage{};
    seed_current(readback_storage, readback_live);
    UpdateCheckpointStore readback_store{readback_storage};
    FakeTrustedGeneration frozen_trust{};
    frozen_trust.freeze_after_advance = true;
    UpdateRecoveryTransitionCoordinator readback{
        readback_store, frozen_trust};
    const auto readback_result = readback.request_rollback(
        readback_live, RollbackReason::explicit_health_failure);
    EXPECT(readback_result.state ==
           UpdateTransitionState::reboot_reconcile_required);
    EXPECT(readback_result.persistence.reason ==
           UpdatePersistenceReason::trusted_readback_failed);
    EXPECT(!readback_live.status().running);
    EXPECT(readback_live.status().state == UpdateState::trial);
}

}  // namespace

int main() {
    test_confirmation_is_published_only_after_commit();
    test_explicit_rollback_is_published_only_after_commit();
    test_boot_local_health_and_time_do_not_write_checkpoint();
    test_rejected_confirmation_retains_clock_evidence_without_write();
    test_tick_timeout_durably_publishes_rollback();
    test_health_timeout_durably_publishes_rollback();
    test_invalid_requests_do_not_touch_storage_or_trust();
    test_uncertain_write_stops_live_guard_without_publishing_attempt();
    test_generation_mismatch_stops_live_guard_without_write();
    test_post_write_trust_failure_stops_live_guard();

    if (failures != 0) {
        std::cerr << failures <<
            " update recovery transition assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 update recovery transition scenario groups\n";
    return EXIT_SUCCESS;
}
