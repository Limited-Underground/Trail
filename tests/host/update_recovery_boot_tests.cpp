#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/update_checkpoint.hpp"
#include "opentrail/update_recovery_boot.hpp"

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
            if (write_before_error) {
                std::copy(data, data + size, slots[slot].begin());
                present[slot] = true;
            }
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
    bool write_before_error{false};
    std::size_t write_count{0};
};

class FakeTrustedGeneration final : public UpdateTrustedGenerationSource {
public:
    UpdateTrustedGenerationRead read() override {
        ++read_count;
        if (advanced && readback_error != UpdateTrustedGenerationError::none) {
            return {readback_error, generation};
        }
        return {read_error, generation};
    }

    UpdateTrustedGenerationError advance_to(
        std::uint64_t requested_generation) override {
        ++advance_count;
        last_requested = requested_generation;
        if (advance_error != UpdateTrustedGenerationError::none) {
            return advance_error;
        }
        advanced = true;
        if (!freeze_after_advance) {
            generation = requested_generation;
        }
        read_error = UpdateTrustedGenerationError::none;
        return UpdateTrustedGenerationError::none;
    }

    UpdateTrustedGenerationError read_error{
        UpdateTrustedGenerationError::not_initialized};
    UpdateTrustedGenerationError advance_error{
        UpdateTrustedGenerationError::none};
    UpdateTrustedGenerationError readback_error{
        UpdateTrustedGenerationError::none};
    std::uint64_t generation{0};
    std::uint64_t last_requested{0};
    std::size_t read_count{0};
    std::size_t advance_count{0};
    bool advanced{false};
    bool freeze_after_advance{false};
};

UpdateGuardPolicy policy(std::uint8_t maximum_boots = 3) {
    return {
        0x1234U,
        10,
        ImageSlot::slot_a,
        health_bit(TrialHealth::runtime_started) |
            health_bit(TrialHealth::watchdog_healthy) |
            health_bit(TrialHealth::configuration_loaded),
        100,
        500,
        maximum_boots,
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

BootObservation baseline_observation(std::uint32_t session = 1) {
    return {session, 10, ImageSlot::slot_a, 10};
}

BootObservation candidate_observation(std::uint32_t session = 1) {
    return {session, 11, ImageSlot::slot_b, 10};
}

UpdateBootGuard pending_guard(const UpdateGuardPolicy& guard_policy = policy()) {
    UpdateBootGuard guard{};
    EXPECT(guard.start(guard_policy) == UpdateGuardError::none);
    EXPECT(guard.stage(candidate()) == UpdateGuardError::none);
    EXPECT(guard.mark_written({11, ImageSlot::slot_b, true, true}) ==
           UpdateGuardError::none);
    return guard;
}

UpdateBootGuard trial_guard(const UpdateGuardPolicy& guard_policy = policy()) {
    auto guard = pending_guard(guard_policy);
    EXPECT(guard.begin_boot(candidate_observation(41)) ==
           UpdateGuardError::none);
    return guard;
}

UpdateBootGuard rollback_guard(
    const UpdateGuardPolicy& guard_policy = policy()) {
    auto guard = trial_guard(guard_policy);
    EXPECT(guard.request_rollback(
               RollbackReason::explicit_health_failure) ==
           UpdateGuardError::none);
    return guard;
}

UpdateBootGuard confirmed_guard() {
    auto guard = trial_guard();
    EXPECT(guard.report_health(
               41, policy().required_health_mask, 100) ==
           UpdateGuardError::none);
    EXPECT(guard.confirm(41, 110) == UpdateGuardError::none);
    return guard;
}

UpdateBootGuard rolled_back_guard() {
    auto guard = rollback_guard();
    EXPECT(guard.complete_rollback(baseline_observation(42)) ==
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

void ready_trust(FakeTrustedGeneration& trusted, std::uint64_t generation) {
    trusted.read_error = UpdateTrustedGenerationError::none;
    trusted.generation = generation;
}

void test_clean_baseline_becomes_operational() {
    FakeStorage storage{};
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    UpdateRecoveryBootCoordinator coordinator{store, trusted};
    UpdateBootGuard live{};

    const auto result = coordinator.boot(
        policy(), baseline_observation(), live);
    EXPECT(result.state == UpdateRecoveryBootState::baseline_ready);
    EXPECT(result.reason == UpdateRecoveryBootReason::clean_baseline);
    EXPECT(result.operational());
    EXPECT(result.application_allowed);
    EXPECT(!result.confirmation_required);
    EXPECT(live.status().running);
    EXPECT(live.status().state == UpdateState::idle);
    EXPECT(storage.write_count == 0);
    EXPECT(trusted.advance_count == 0);
}

void test_clean_baseline_rejects_wrong_observation() {
    FakeStorage storage{};
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    UpdateRecoveryBootCoordinator coordinator{store, trusted};
    UpdateBootGuard live{};

    const auto result = coordinator.boot(
        policy(), candidate_observation(), live);
    EXPECT(result.state == UpdateRecoveryBootState::safe_mode);
    EXPECT(result.reason ==
           UpdateRecoveryBootReason::boot_observation_rejected);
    EXPECT(!result.application_allowed);
    EXPECT(!live.status().running);
}

void test_uninitialized_trust_rejects_existing_media() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(pending_guard(), 1));
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    UpdateRecoveryBootCoordinator coordinator{store, trusted};
    UpdateBootGuard live{};

    const auto result = coordinator.boot(
        policy(), candidate_observation(), live);
    EXPECT(result.state == UpdateRecoveryBootState::safe_mode);
    EXPECT(result.reason ==
           UpdateRecoveryBootReason::baseline_state_conflict);
    EXPECT(!live.status().running);
    EXPECT(storage.write_count == 0);
}

void test_trusted_source_failure_and_zero_fail_closed() {
    FakeStorage failed_storage{};
    UpdateCheckpointStore failed_store{failed_storage};
    FakeTrustedGeneration failed_trust{};
    failed_trust.read_error = UpdateTrustedGenerationError::io_failure;
    UpdateRecoveryBootCoordinator failed{failed_store, failed_trust};
    UpdateBootGuard failed_live{};
    const auto unavailable = failed.boot(
        policy(), baseline_observation(), failed_live);
    EXPECT(unavailable.state ==
           UpdateRecoveryBootState::service_required);
    EXPECT(unavailable.reason ==
           UpdateRecoveryBootReason::trusted_read_failed);
    EXPECT(!failed_live.status().running);

    FakeStorage zero_storage{};
    UpdateCheckpointStore zero_store{zero_storage};
    FakeTrustedGeneration zero_trust{};
    ready_trust(zero_trust, 0);
    UpdateRecoveryBootCoordinator zero{zero_store, zero_trust};
    UpdateBootGuard zero_live{};
    const auto invalid = zero.boot(
        policy(), baseline_observation(), zero_live);
    EXPECT(invalid.reason ==
           UpdateRecoveryBootReason::trusted_generation_invalid);
    EXPECT(!zero_live.status().running);
}

void test_missing_and_stale_checkpoint_detect_rollback() {
    FakeStorage missing_storage{};
    UpdateCheckpointStore missing_store{missing_storage};
    FakeTrustedGeneration missing_trust{};
    ready_trust(missing_trust, 5);
    UpdateRecoveryBootCoordinator missing{missing_store, missing_trust};
    UpdateBootGuard missing_live{};
    const auto absent = missing.boot(
        policy(), baseline_observation(), missing_live);
    EXPECT(absent.state == UpdateRecoveryBootState::safe_mode);
    EXPECT(absent.reason == UpdateRecoveryBootReason::rollback_detected);
    EXPECT(!missing_live.status().running);

    FakeStorage stale_storage{};
    stale_storage.seed(0, encode_guard(pending_guard(), 4));
    UpdateCheckpointStore stale_store{stale_storage};
    FakeTrustedGeneration stale_trust{};
    ready_trust(stale_trust, 5);
    UpdateRecoveryBootCoordinator stale{stale_store, stale_trust};
    UpdateBootGuard stale_live{};
    const auto stale_result = stale.boot(
        policy(), candidate_observation(), stale_live);
    EXPECT(stale_result.state == UpdateRecoveryBootState::safe_mode);
    EXPECT(stale_result.reason ==
           UpdateRecoveryBootReason::rollback_detected);
    EXPECT(!stale_live.status().running);
}

void test_pending_trial_is_persisted_before_release() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(pending_guard(), 1));
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    ready_trust(trusted, 1);
    UpdateRecoveryBootCoordinator coordinator{store, trusted};
    UpdateBootGuard live{};

    const auto result = coordinator.boot(
        policy(), candidate_observation(2), live);
    EXPECT(result.state == UpdateRecoveryBootState::trial_ready);
    EXPECT(result.operational());
    EXPECT(result.confirmation_required);
    EXPECT(result.save.saved());
    EXPECT(result.active_generation == 2);
    EXPECT(result.trusted_generation == 2);
    EXPECT(trusted.last_requested == 2);
    EXPECT(storage.write_count == 1);
    EXPECT(live.status().state == UpdateState::trial);
    EXPECT(live.status().trial_boots == 1);
    EXPECT(live.status().trial_boot_session_id == 2);
}

void test_trial_resume_increments_and_persists_attempt() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(trial_guard(), 3));
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    ready_trust(trusted, 3);
    UpdateRecoveryBootCoordinator coordinator{store, trusted};
    UpdateBootGuard live{};

    const auto result = coordinator.boot(
        policy(), candidate_observation(42), live);
    EXPECT(result.state == UpdateRecoveryBootState::trial_ready);
    EXPECT(result.active_generation == 4);
    EXPECT(result.trusted_generation == 4);
    EXPECT(live.status().trial_boots == 2);
    EXPECT(live.status().trial_boot_session_id == 42);
}

void test_boot_mismatch_is_persisted_as_rollback() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(pending_guard(), 1));
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    ready_trust(trusted, 1);
    UpdateRecoveryBootCoordinator coordinator{store, trusted};
    UpdateBootGuard live{};

    const auto result = coordinator.boot(
        policy(), baseline_observation(2), live);
    EXPECT(result.state == UpdateRecoveryBootState::rollback_required);
    EXPECT(result.reason == UpdateRecoveryBootReason::boot_mismatch);
    EXPECT(result.reboot_to_baseline_required);
    EXPECT(!result.application_allowed);
    EXPECT(result.save.saved());
    EXPECT(result.trusted_generation == 2);
    EXPECT(live.status().state == UpdateState::rollback_required);
    EXPECT(live.status().rollback_reason == RollbackReason::boot_mismatch);
}

void test_trial_limit_is_persisted_as_rollback() {
    const auto limited_policy = policy(1);
    FakeStorage storage{};
    storage.seed(0, encode_guard(trial_guard(limited_policy), 1));
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    ready_trust(trusted, 1);
    UpdateRecoveryBootCoordinator coordinator{store, trusted};
    UpdateBootGuard live{};

    const auto result = coordinator.boot(
        limited_policy, candidate_observation(42), live);
    EXPECT(result.state == UpdateRecoveryBootState::rollback_required);
    EXPECT(result.reason == UpdateRecoveryBootReason::trial_boot_limit);
    EXPECT(result.guard_error == UpdateGuardError::boot_attempt_limit);
    EXPECT(result.save.saved());
    EXPECT(live.status().rollback_reason ==
           RollbackReason::boot_attempt_limit);
}

void test_rollback_completion_is_persisted_before_baseline_release() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(rollback_guard(), 2));
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    ready_trust(trusted, 2);
    UpdateRecoveryBootCoordinator coordinator{store, trusted};
    UpdateBootGuard live{};

    const auto result = coordinator.boot(
        policy(), baseline_observation(50), live);
    EXPECT(result.state == UpdateRecoveryBootState::baseline_recovered);
    EXPECT(result.operational());
    EXPECT(result.checkpoint_cleanup_required);
    EXPECT(result.save.saved());
    EXPECT(result.active_generation == 3);
    EXPECT(result.trusted_generation == 3);
    EXPECT(live.status().state == UpdateState::rolled_back);
}

void test_wrong_rollback_observation_changes_nothing() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(rollback_guard(), 2));
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    ready_trust(trusted, 2);
    UpdateRecoveryBootCoordinator coordinator{store, trusted};
    UpdateBootGuard live{};
    auto wrong = baseline_observation(50);
    wrong.version = 9;

    const auto result = coordinator.boot(policy(), wrong, live);
    EXPECT(result.state == UpdateRecoveryBootState::safe_mode);
    EXPECT(result.reason ==
           UpdateRecoveryBootReason::rollback_observation_rejected);
    EXPECT(storage.write_count == 0);
    EXPECT(trusted.advance_count == 0);
    EXPECT(!live.status().running);
}

void test_terminal_states_require_cleanup_and_catch_up_trust() {
    FakeStorage confirmed_storage{};
    confirmed_storage.seed(0, encode_guard(confirmed_guard(), 5));
    UpdateCheckpointStore confirmed_store{confirmed_storage};
    FakeTrustedGeneration confirmed_trust{};
    ready_trust(confirmed_trust, 4);
    UpdateRecoveryBootCoordinator confirmed{
        confirmed_store, confirmed_trust};
    UpdateBootGuard confirmed_live{};
    const auto confirmed_result = confirmed.boot(
        policy(), candidate_observation(60), confirmed_live);
    EXPECT(confirmed_result.state ==
           UpdateRecoveryBootState::confirmed_cleanup_required);
    EXPECT(confirmed_result.operational());
    EXPECT(confirmed_result.checkpoint_cleanup_required);
    EXPECT(confirmed_result.trusted_generation == 5);
    EXPECT(confirmed_storage.write_count == 0);

    FakeStorage rolled_storage{};
    rolled_storage.seed(0, encode_guard(rolled_back_guard(), 7));
    UpdateCheckpointStore rolled_store{rolled_storage};
    FakeTrustedGeneration rolled_trust{};
    ready_trust(rolled_trust, 7);
    UpdateRecoveryBootCoordinator rolled{rolled_store, rolled_trust};
    UpdateBootGuard rolled_live{};
    const auto rolled_result = rolled.boot(
        policy(), baseline_observation(61), rolled_live);
    EXPECT(rolled_result.state ==
           UpdateRecoveryBootState::baseline_recovered);
    EXPECT(rolled_result.operational());
    EXPECT(rolled_result.checkpoint_cleanup_required);
    EXPECT(rolled_storage.write_count == 0);
}

void test_trust_advance_and_readback_failures_keep_guard_private() {
    FakeStorage advance_storage{};
    advance_storage.seed(0, encode_guard(pending_guard(), 1));
    UpdateCheckpointStore advance_store{advance_storage};
    FakeTrustedGeneration advance_trust{};
    ready_trust(advance_trust, 1);
    advance_trust.advance_error = UpdateTrustedGenerationError::io_failure;
    UpdateRecoveryBootCoordinator advance{advance_store, advance_trust};
    UpdateBootGuard advance_live{};
    const auto advance_result = advance.boot(
        policy(), candidate_observation(2), advance_live);
    EXPECT(advance_result.reason ==
           UpdateRecoveryBootReason::trusted_advance_failed);
    EXPECT(advance_result.reconciliation_required);
    EXPECT(advance_result.save.saved());
    EXPECT(!advance_live.status().running);

    FakeStorage readback_storage{};
    readback_storage.seed(0, encode_guard(pending_guard(), 1));
    UpdateCheckpointStore readback_store{readback_storage};
    FakeTrustedGeneration readback_trust{};
    ready_trust(readback_trust, 1);
    readback_trust.freeze_after_advance = true;
    UpdateRecoveryBootCoordinator readback{readback_store, readback_trust};
    UpdateBootGuard readback_live{};
    const auto readback_result = readback.boot(
        policy(), candidate_observation(2), readback_live);
    EXPECT(readback_result.reason ==
           UpdateRecoveryBootReason::trusted_readback_failed);
    EXPECT(readback_result.reconciliation_required);
    EXPECT(!readback_live.status().running);
}

void test_uncertain_checkpoint_write_keeps_guard_private() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(pending_guard(), 1));
    storage.fail_write_slot = 1;
    storage.write_before_error = true;
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    ready_trust(trusted, 1);
    UpdateRecoveryBootCoordinator coordinator{store, trusted};
    UpdateBootGuard live{};

    const auto result = coordinator.boot(
        policy(), candidate_observation(2), live);
    EXPECT(result.state == UpdateRecoveryBootState::service_required);
    EXPECT(result.reason ==
           UpdateRecoveryBootReason::checkpoint_commit_uncertain);
    EXPECT(result.reconciliation_required);
    EXPECT(result.save.commit_uncertain);
    EXPECT(trusted.advance_count == 0);
    EXPECT(!live.status().running);
}

void test_conflict_and_unreadable_media_are_typed() {
    FakeStorage conflict_storage{};
    conflict_storage.seed(0, encode_guard(pending_guard(), 5));
    conflict_storage.seed(1, encode_guard(rollback_guard(), 5));
    UpdateCheckpointStore conflict_store{conflict_storage};
    FakeTrustedGeneration conflict_trust{};
    ready_trust(conflict_trust, 5);
    UpdateRecoveryBootCoordinator conflict{
        conflict_store, conflict_trust};
    UpdateBootGuard conflict_live{};
    const auto conflicted = conflict.boot(
        policy(), candidate_observation(), conflict_live);
    EXPECT(conflicted.state == UpdateRecoveryBootState::safe_mode);
    EXPECT(conflicted.reason ==
           UpdateRecoveryBootReason::generation_conflict);
    EXPECT(!conflict_live.status().running);

    FakeStorage unreadable_storage{};
    unreadable_storage.seed(0, encode_guard(pending_guard(), 5));
    unreadable_storage.fail_read_slot = 1;
    UpdateCheckpointStore unreadable_store{unreadable_storage};
    FakeTrustedGeneration unreadable_trust{};
    ready_trust(unreadable_trust, 5);
    UpdateRecoveryBootCoordinator unreadable{
        unreadable_store, unreadable_trust};
    UpdateBootGuard unreadable_live{};
    const auto unreadable_result = unreadable.boot(
        policy(), candidate_observation(), unreadable_live);
    EXPECT(unreadable_result.state ==
           UpdateRecoveryBootState::service_required);
    EXPECT(unreadable_result.reason ==
           UpdateRecoveryBootReason::storage_unavailable);
    EXPECT(!unreadable_live.status().running);
}

}  // namespace

int main() {
    test_clean_baseline_becomes_operational();
    test_clean_baseline_rejects_wrong_observation();
    test_uninitialized_trust_rejects_existing_media();
    test_trusted_source_failure_and_zero_fail_closed();
    test_missing_and_stale_checkpoint_detect_rollback();
    test_pending_trial_is_persisted_before_release();
    test_trial_resume_increments_and_persists_attempt();
    test_boot_mismatch_is_persisted_as_rollback();
    test_trial_limit_is_persisted_as_rollback();
    test_rollback_completion_is_persisted_before_baseline_release();
    test_wrong_rollback_observation_changes_nothing();
    test_terminal_states_require_cleanup_and_catch_up_trust();
    test_trust_advance_and_readback_failures_keep_guard_private();
    test_uncertain_checkpoint_write_keeps_guard_private();
    test_conflict_and_unreadable_media_are_typed();

    if (failures != 0) {
        std::cerr << failures <<
            " update recovery boot assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 15 update recovery boot scenario groups\n";
    return EXIT_SUCCESS;
}
