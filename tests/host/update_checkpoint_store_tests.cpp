#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/update_boot_guard.hpp"
#include "opentrail/update_checkpoint.hpp"
#include "opentrail/update_checkpoint_store.hpp"

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

class FakeUpdateStorage final : public UpdateCheckpointStorage {
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
        present[slot] = true;
        if (partial_write_bytes < size) {
            slots[slot].fill(0);
            std::copy(data, data + partial_write_bytes,
                      slots[slot].begin());
            partial_write_bytes = size;
            return UpdateCheckpointStorageError::io_failure;
        }
        std::copy(data, data + size, slots[slot].begin());
        if (corrupt_after_write) {
            slots[slot][20] ^= 0x01U;
            corrupt_after_write = false;
        }
        return UpdateCheckpointStorageError::none;
    }

    UpdateCheckpointStorageError erase_slot(std::uint8_t slot) override {
        if (slot >= slots.size()) {
            return UpdateCheckpointStorageError::invalid_argument;
        }
        if (fail_erase_slot == static_cast<int>(slot)) {
            return UpdateCheckpointStorageError::io_failure;
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
    int fail_erase_slot{-1};
    std::size_t partial_write_bytes{kUpdateCheckpointRecordBytes};
    bool corrupt_after_write{false};
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

std::array<std::uint8_t, kUpdateCheckpointRecordBytes> encoded_checkpoint(
    const UpdateGuardCheckpoint& checkpoint) {
    std::array<std::uint8_t, kUpdateCheckpointRecordBytes> bytes{};
    EXPECT(encode_update_checkpoint(
               checkpoint, bytes.data(), bytes.size()).succeeded());
    return bytes;
}

UpdateGuardCheckpoint checkpoint(
    const UpdateBootGuard& guard,
    std::uint64_t generation) {
    UpdateGuardCheckpoint value{};
    EXPECT(guard.export_checkpoint(generation, value) ==
           UpdateGuardError::none);
    return value;
}

void test_empty_first_save_and_restore() {
    FakeUpdateStorage storage{};
    UpdateCheckpointStore store{storage};
    auto target = pending_guard();
    UpdateBootGuard empty_restore{};
    EXPECT(empty_restore.start(policy()) == UpdateGuardError::none);
    const auto empty = store.restore(empty_restore);
    EXPECT(empty.error == UpdateCheckpointStoreError::no_checkpoint);
    EXPECT(!empty.restored);

    const auto saved = store.save(target);
    EXPECT(saved.saved());
    EXPECT(saved.generation == 1);
    EXPECT(saved.written_slot == UpdateCheckpointSource::slot_a);
    EXPECT(!saved.commit_uncertain);

    UpdateBootGuard restored{};
    EXPECT(restored.start(policy()) == UpdateGuardError::none);
    const auto loaded = store.restore(restored);
    EXPECT(loaded.error == UpdateCheckpointStoreError::none);
    EXPECT(loaded.restored);
    EXPECT(loaded.generation == 1);
    EXPECT(loaded.source == UpdateCheckpointSource::slot_a);
    EXPECT(loaded.recovery_required);
    EXPECT(restored.status().state == UpdateState::pending_reboot);
}

void test_rotation_selects_newest_unique_generation() {
    FakeUpdateStorage storage{};
    UpdateCheckpointStore store{storage};
    const auto guard = pending_guard();
    EXPECT(store.save(guard).generation == 1);
    const auto second = store.save(guard);
    EXPECT(second.saved());
    EXPECT(second.generation == 2);
    EXPECT(second.written_slot == UpdateCheckpointSource::slot_b);

    UpdateBootGuard restored{};
    EXPECT(restored.start(policy()) == UpdateGuardError::none);
    const auto loaded = store.restore(restored);
    EXPECT(loaded.restored);
    EXPECT(loaded.generation == 2);
    EXPECT(loaded.source == UpdateCheckpointSource::slot_b);
    EXPECT(!loaded.recovery_required);
}

void test_partial_write_preserves_previous_generation() {
    FakeUpdateStorage storage{};
    UpdateCheckpointStore store{storage};
    const auto guard = pending_guard();
    EXPECT(store.save(guard).saved());
    storage.partial_write_bytes = 20;
    const auto interrupted = store.save(guard);
    EXPECT(interrupted.error == UpdateCheckpointStoreError::storage_failure);
    EXPECT(interrupted.commit_uncertain);
    EXPECT(interrupted.generation == 2);

    UpdateBootGuard restored{};
    EXPECT(restored.start(policy()) == UpdateGuardError::none);
    const auto loaded = store.restore(restored);
    EXPECT(loaded.restored);
    EXPECT(loaded.generation == 1);
    EXPECT(loaded.source == UpdateCheckpointSource::slot_a);
    EXPECT(loaded.slot_b == UpdateCheckpointSlotState::invalid);
    EXPECT(loaded.recovery_required);
}

void test_corrupt_readback_preserves_previous_generation() {
    FakeUpdateStorage storage{};
    UpdateCheckpointStore store{storage};
    const auto guard = pending_guard();
    EXPECT(store.save(guard).saved());
    storage.corrupt_after_write = true;
    const auto failed = store.save(guard);
    EXPECT(failed.error ==
           UpdateCheckpointStoreError::verification_failure);
    EXPECT(failed.commit_uncertain);

    UpdateBootGuard restored{};
    EXPECT(restored.start(policy()) == UpdateGuardError::none);
    const auto loaded = store.restore(restored);
    EXPECT(loaded.restored);
    EXPECT(loaded.generation == 1);
    EXPECT(loaded.recovery_required);
}

void test_invalid_peer_is_repaired_without_overwriting_good_slot() {
    FakeUpdateStorage storage{};
    UpdateCheckpointStore store{storage};
    const auto guard = pending_guard();
    EXPECT(store.save(guard).saved());
    storage.present[1] = true;
    storage.slots[1].fill(0xA5U);
    const auto repaired = store.save(guard);
    EXPECT(repaired.saved());
    EXPECT(repaired.repaired_peer);
    EXPECT(repaired.generation == 2);
    EXPECT(repaired.written_slot == UpdateCheckpointSource::slot_b);

    UpdateBootGuard restored{};
    EXPECT(restored.start(policy()) == UpdateGuardError::none);
    const auto loaded = store.restore(restored);
    EXPECT(loaded.restored);
    EXPECT(loaded.generation == 2);
    EXPECT(!loaded.recovery_required);
}

void test_unreadable_slot_fails_closed() {
    FakeUpdateStorage storage{};
    UpdateCheckpointStore store{storage};
    const auto guard = pending_guard();
    EXPECT(store.save(guard).saved());
    storage.fail_read_slot = 1;

    UpdateBootGuard restored{};
    EXPECT(restored.start(policy()) == UpdateGuardError::none);
    const auto loaded = store.restore(restored);
    EXPECT(loaded.error == UpdateCheckpointStoreError::storage_failure);
    EXPECT(loaded.recovery_required);
    EXPECT(restored.status().state == UpdateState::idle);
    EXPECT(store.save(guard).error ==
           UpdateCheckpointStoreError::storage_failure);
}

void test_equal_generation_conflict_fails_closed() {
    FakeUpdateStorage storage{};
    UpdateCheckpointStore store{storage};
    const auto guard = pending_guard();
    auto first = checkpoint(guard, 5);
    auto second = first;
    second.state = UpdateState::rollback_required;
    second.rollback_reason = RollbackReason::boot_mismatch;
    storage.seed(0, encoded_checkpoint(first));
    storage.seed(1, encoded_checkpoint(second));

    UpdateBootGuard restored{};
    EXPECT(restored.start(policy()) == UpdateGuardError::none);
    const auto loaded = store.restore(restored);
    EXPECT(loaded.error ==
           UpdateCheckpointStoreError::generation_conflict);
    EXPECT(loaded.recovery_required);
    EXPECT(restored.status().state == UpdateState::idle);
    EXPECT(store.save(guard).error ==
           UpdateCheckpointStoreError::generation_conflict);
}

void test_policy_mismatch_rejects_without_live_mutation() {
    FakeUpdateStorage storage{};
    UpdateCheckpointStore store{storage};
    const auto guard = pending_guard();
    EXPECT(store.save(guard).saved());
    auto changed = policy();
    changed.minimum_stable_ms = 101;
    UpdateBootGuard restored{};
    EXPECT(restored.start(changed) == UpdateGuardError::none);
    const auto loaded = store.restore(restored);
    EXPECT(loaded.error == UpdateCheckpointStoreError::checkpoint_rejected);
    EXPECT(loaded.guard_error == UpdateGuardError::checkpoint_mismatch);
    EXPECT(loaded.recovery_required);
    EXPECT(restored.status().state == UpdateState::idle);
}

void test_generation_exhaustion_prevents_write() {
    FakeUpdateStorage storage{};
    UpdateCheckpointStore store{storage};
    const auto guard = pending_guard();
    storage.seed(
        0,
        encoded_checkpoint(checkpoint(
            guard, std::numeric_limits<std::uint64_t>::max())));
    const auto saved = store.save(guard);
    EXPECT(saved.error == UpdateCheckpointStoreError::generation_exhausted);
    EXPECT(saved.written_slot == UpdateCheckpointSource::none);
    EXPECT(!saved.commit_uncertain);
}

void test_reset_and_nonpersistent_guard_rejection() {
    FakeUpdateStorage storage{};
    UpdateCheckpointStore store{storage};
    UpdateBootGuard idle{};
    EXPECT(idle.start(policy()) == UpdateGuardError::none);
    const auto rejected = store.save(idle);
    EXPECT(rejected.error == UpdateCheckpointStoreError::checkpoint_rejected);
    EXPECT(rejected.guard_error == UpdateGuardError::invalid_state);

    const auto guard = pending_guard();
    EXPECT(store.save(guard).saved());
    EXPECT(store.reset() == UpdateCheckpointStoreError::none);
    UpdateBootGuard restored{};
    EXPECT(restored.start(policy()) == UpdateGuardError::none);
    EXPECT(store.restore(restored).error ==
           UpdateCheckpointStoreError::no_checkpoint);

    EXPECT(store.save(guard).saved());
    storage.fail_erase_slot = 1;
    EXPECT(store.reset() == UpdateCheckpointStoreError::storage_failure);
}

}  // namespace

int main() {
    test_empty_first_save_and_restore();
    test_rotation_selects_newest_unique_generation();
    test_partial_write_preserves_previous_generation();
    test_corrupt_readback_preserves_previous_generation();
    test_invalid_peer_is_repaired_without_overwriting_good_slot();
    test_unreadable_slot_fails_closed();
    test_equal_generation_conflict_fails_closed();
    test_policy_mismatch_rejects_without_live_mutation();
    test_generation_exhaustion_prevents_write();
    test_reset_and_nonpersistent_guard_rejection();

    if (failures != 0) {
        std::cerr << failures <<
            " update checkpoint store assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 update checkpoint store scenario groups\n";
    return EXIT_SUCCESS;
}
