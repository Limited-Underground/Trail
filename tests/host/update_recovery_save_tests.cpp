#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/update_checkpoint.hpp"
#include "opentrail/update_recovery_save.hpp"

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
        UpdateTrustedGenerationError::none};
    UpdateTrustedGenerationError advance_error{
        UpdateTrustedGenerationError::none};
    UpdateTrustedGenerationError readback_error{
        UpdateTrustedGenerationError::none};
    std::uint64_t generation{1};
    std::uint64_t last_requested{0};
    std::size_t read_count{0};
    std::size_t advance_count{0};
    bool advanced{false};
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

UpdateBootGuard pending_guard() {
    UpdateBootGuard guard{};
    EXPECT(guard.start(policy()) == UpdateGuardError::none);
    EXPECT(guard.stage(candidate()) == UpdateGuardError::none);
    EXPECT(guard.mark_written({11, ImageSlot::slot_b, true, true}) ==
           UpdateGuardError::none);
    return guard;
}

UpdateBootGuard trial_guard() {
    auto guard = pending_guard();
    EXPECT(guard.begin_boot({41, 11, ImageSlot::slot_b, 10}) ==
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

UpdateBootGuard rollback_guard() {
    auto guard = trial_guard();
    EXPECT(guard.request_rollback(
               RollbackReason::explicit_health_failure) ==
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

void test_confirmed_transition_commits_then_advances_trust() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(trial_guard(), 1));
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    UpdateRecoverySaveCoordinator coordinator{store, trusted};
    const auto confirmed = confirmed_guard();

    const auto result = coordinator.save(confirmed);
    EXPECT(result.committed());
    EXPECT(result.inspection.generation == 1);
    EXPECT(result.committed_generation == 2);
    EXPECT(result.observed_trusted_readback == 2);
    EXPECT(result.save.saved());
    EXPECT(trusted.generation == 2);
    EXPECT(trusted.last_requested == 2);
    EXPECT(storage.write_count == 1);
}

void test_trusted_read_and_zero_generation_fail_closed() {
    FakeStorage failed_storage{};
    failed_storage.seed(0, encode_guard(trial_guard(), 1));
    UpdateCheckpointStore failed_store{failed_storage};
    FakeTrustedGeneration failed_trust{};
    failed_trust.read_error = UpdateTrustedGenerationError::io_failure;
    UpdateRecoverySaveCoordinator failed{failed_store, failed_trust};
    const auto failed_result = failed.save(confirmed_guard());
    EXPECT(failed_result.state ==
           UpdatePersistenceState::service_required);
    EXPECT(failed_result.reason ==
           UpdatePersistenceReason::trusted_read_failed);
    EXPECT(failed_storage.write_count == 0);

    FakeStorage zero_storage{};
    zero_storage.seed(0, encode_guard(trial_guard(), 1));
    UpdateCheckpointStore zero_store{zero_storage};
    FakeTrustedGeneration zero_trust{};
    zero_trust.generation = 0;
    UpdateRecoverySaveCoordinator zero{zero_store, zero_trust};
    const auto zero_result = zero.save(confirmed_guard());
    EXPECT(zero_result.reason ==
           UpdatePersistenceReason::trusted_generation_invalid);
    EXPECT(zero_storage.write_count == 0);
}

void test_missing_and_invalid_checkpoint_enter_safe_mode() {
    FakeStorage missing_storage{};
    UpdateCheckpointStore missing_store{missing_storage};
    FakeTrustedGeneration missing_trust{};
    UpdateRecoverySaveCoordinator missing{missing_store, missing_trust};
    const auto missing_result = missing.save(confirmed_guard());
    EXPECT(missing_result.state == UpdatePersistenceState::safe_mode);
    EXPECT(missing_result.reason ==
           UpdatePersistenceReason::recovery_missing);

    FakeStorage invalid_storage{};
    invalid_storage.present[0] = true;
    invalid_storage.slots[0].fill(0xA5U);
    UpdateCheckpointStore invalid_store{invalid_storage};
    FakeTrustedGeneration invalid_trust{};
    UpdateRecoverySaveCoordinator invalid{invalid_store, invalid_trust};
    const auto invalid_result = invalid.save(confirmed_guard());
    EXPECT(invalid_result.state == UpdatePersistenceState::safe_mode);
    EXPECT(invalid_result.reason ==
           UpdatePersistenceReason::checkpoint_rejected);
}

void test_local_behind_trust_is_rollback() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(trial_guard(), 1));
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    trusted.generation = 2;
    UpdateRecoverySaveCoordinator coordinator{store, trusted};

    const auto result = coordinator.save(confirmed_guard());
    EXPECT(result.state == UpdatePersistenceState::safe_mode);
    EXPECT(result.reason == UpdatePersistenceReason::rollback_detected);
    EXPECT(storage.write_count == 0);
    EXPECT(trusted.advance_count == 0);
}

void test_local_ahead_requires_boot_reconciliation() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(trial_guard(), 2));
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    trusted.generation = 1;
    UpdateRecoverySaveCoordinator coordinator{store, trusted};

    const auto result = coordinator.save(confirmed_guard());
    EXPECT(result.state ==
           UpdatePersistenceState::reboot_reconcile_required);
    EXPECT(result.reason ==
           UpdatePersistenceReason::trusted_reconciliation_required);
    EXPECT(storage.write_count == 0);
    EXPECT(trusted.advance_count == 0);
}

void test_conflict_and_unreadable_media_refuse_save() {
    FakeStorage conflict_storage{};
    conflict_storage.seed(0, encode_guard(trial_guard(), 1));
    conflict_storage.seed(1, encode_guard(rollback_guard(), 1));
    UpdateCheckpointStore conflict_store{conflict_storage};
    FakeTrustedGeneration conflict_trust{};
    UpdateRecoverySaveCoordinator conflict{
        conflict_store, conflict_trust};
    const auto conflict_result = conflict.save(confirmed_guard());
    EXPECT(conflict_result.state == UpdatePersistenceState::safe_mode);
    EXPECT(conflict_result.reason ==
           UpdatePersistenceReason::generation_conflict);
    EXPECT(conflict_storage.write_count == 0);

    FakeStorage unreadable_storage{};
    unreadable_storage.seed(0, encode_guard(trial_guard(), 1));
    unreadable_storage.fail_read_slot = 1;
    UpdateCheckpointStore unreadable_store{unreadable_storage};
    FakeTrustedGeneration unreadable_trust{};
    UpdateRecoverySaveCoordinator unreadable{
        unreadable_store, unreadable_trust};
    const auto unreadable_result = unreadable.save(confirmed_guard());
    EXPECT(unreadable_result.state ==
           UpdatePersistenceState::service_required);
    EXPECT(unreadable_result.reason ==
           UpdatePersistenceReason::storage_failure);
    EXPECT(unreadable_result.inspection.checkpoint_available);
    EXPECT(unreadable_storage.write_count == 0);
}

void test_generation_exhaustion_prevents_export_and_write() {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    FakeStorage storage{};
    storage.seed(0, encode_guard(trial_guard(), maximum));
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    trusted.generation = maximum;
    UpdateRecoverySaveCoordinator coordinator{store, trusted};

    const auto result = coordinator.save(confirmed_guard());
    EXPECT(result.state ==
           UpdatePersistenceState::service_required);
    EXPECT(result.reason ==
           UpdatePersistenceReason::generation_exhausted);
    EXPECT(storage.write_count == 0);
    EXPECT(trusted.advance_count == 0);
}

void test_nonpersistent_guard_is_rejected() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(trial_guard(), 1));
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    UpdateRecoverySaveCoordinator coordinator{store, trusted};
    UpdateBootGuard stopped{};

    const auto stopped_result = coordinator.save(stopped);
    EXPECT(stopped_result.state == UpdatePersistenceState::safe_mode);
    EXPECT(stopped_result.reason ==
           UpdatePersistenceReason::live_guard_not_running);

    UpdateBootGuard idle{};
    EXPECT(idle.start(policy()) == UpdateGuardError::none);
    const auto idle_result = coordinator.save(idle);
    EXPECT(idle_result.state == UpdatePersistenceState::safe_mode);
    EXPECT(idle_result.reason ==
           UpdatePersistenceReason::checkpoint_rejected);
}

void test_uncertain_write_requires_reboot_reconciliation() {
    FakeStorage storage{};
    storage.seed(0, encode_guard(trial_guard(), 1));
    storage.fail_write_slot = 1;
    UpdateCheckpointStore store{storage};
    FakeTrustedGeneration trusted{};
    UpdateRecoverySaveCoordinator coordinator{store, trusted};

    const auto result = coordinator.save(confirmed_guard());
    EXPECT(result.state ==
           UpdatePersistenceState::reboot_reconcile_required);
    EXPECT(result.reason == UpdatePersistenceReason::commit_uncertain);
    EXPECT(result.save.commit_uncertain);
    EXPECT(trusted.advance_count == 0);
}

void test_trust_advance_and_readback_failures_require_reboot() {
    FakeStorage advance_storage{};
    advance_storage.seed(0, encode_guard(trial_guard(), 1));
    UpdateCheckpointStore advance_store{advance_storage};
    FakeTrustedGeneration advance_trust{};
    advance_trust.advance_error = UpdateTrustedGenerationError::io_failure;
    UpdateRecoverySaveCoordinator advance{advance_store, advance_trust};
    const auto advance_result = advance.save(confirmed_guard());
    EXPECT(advance_result.state ==
           UpdatePersistenceState::reboot_reconcile_required);
    EXPECT(advance_result.reason ==
           UpdatePersistenceReason::trusted_advance_failed);
    EXPECT(advance_result.save.saved());

    FakeStorage readback_storage{};
    readback_storage.seed(0, encode_guard(trial_guard(), 1));
    UpdateCheckpointStore readback_store{readback_storage};
    FakeTrustedGeneration readback_trust{};
    readback_trust.freeze_after_advance = true;
    UpdateRecoverySaveCoordinator readback{readback_store, readback_trust};
    const auto readback_result = readback.save(confirmed_guard());
    EXPECT(readback_result.state ==
           UpdatePersistenceState::reboot_reconcile_required);
    EXPECT(readback_result.reason ==
           UpdatePersistenceReason::trusted_readback_failed);
    EXPECT(readback_result.observed_trusted_readback == 1);
    EXPECT(readback_result.save.saved());
}

}  // namespace

int main() {
    test_confirmed_transition_commits_then_advances_trust();
    test_trusted_read_and_zero_generation_fail_closed();
    test_missing_and_invalid_checkpoint_enter_safe_mode();
    test_local_behind_trust_is_rollback();
    test_local_ahead_requires_boot_reconciliation();
    test_conflict_and_unreadable_media_refuse_save();
    test_generation_exhaustion_prevents_export_and_write();
    test_nonpersistent_guard_is_rejected();
    test_uncertain_write_requires_reboot_reconciliation();
    test_trust_advance_and_readback_failures_require_reboot();

    if (failures != 0) {
        std::cerr << failures <<
            " update recovery save assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 update recovery save scenario groups\n";
    return EXIT_SUCCESS;
}
