#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/duplicate_checkpoint_store.hpp"

namespace {

using namespace opentrail::delivery;

int failures = 0;
constexpr std::uint64_t kGroupContext = 0x0102030405060708ULL;
constexpr std::uint32_t kGroupEpoch = 0x11223344U;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

enum class WriteBehavior : std::uint8_t {
    normal = 0,
    fail_before_write,
    fail_after_partial_write,
    corrupt_after_success,
};

class FakeStorage final : public DuplicateCheckpointStorage {
public:
    DuplicateCheckpointStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override {
        if (slot >= 2 || output == nullptr ||
            size != kStoredDuplicateCheckpointBytes) {
            return DuplicateCheckpointStorageError::invalid_argument;
        }
        ++reads_[slot];
        if (fail_read_[slot]) {
            fail_read_[slot] = false;
            return DuplicateCheckpointStorageError::io_failure;
        }
        if (!present_[slot]) {
            return DuplicateCheckpointStorageError::not_found;
        }
        std::copy(slots_[slot].begin(), slots_[slot].end(), output);
        return DuplicateCheckpointStorageError::none;
    }

    DuplicateCheckpointStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) override {
        if (slot >= 2 || data == nullptr ||
            size != kStoredDuplicateCheckpointBytes) {
            return DuplicateCheckpointStorageError::invalid_argument;
        }
        ++writes_[slot];
        const auto behavior = next_write_[slot];
        next_write_[slot] = WriteBehavior::normal;
        if (behavior == WriteBehavior::fail_before_write) {
            return DuplicateCheckpointStorageError::io_failure;
        }
        present_[slot] = true;
        if (behavior == WriteBehavior::fail_after_partial_write) {
            std::copy(data, data + size / 2, slots_[slot].begin());
            return DuplicateCheckpointStorageError::io_failure;
        }
        std::copy(data, data + size, slots_[slot].begin());
        if (behavior == WriteBehavior::corrupt_after_success) {
            slots_[slot][100] ^= 0x5AU;
        }
        return DuplicateCheckpointStorageError::none;
    }

    DuplicateCheckpointStorageError erase_slot(std::uint8_t slot) override {
        if (slot >= 2) {
            return DuplicateCheckpointStorageError::invalid_argument;
        }
        ++erases_[slot];
        if (fail_erase_[slot]) {
            fail_erase_[slot] = false;
            return DuplicateCheckpointStorageError::io_failure;
        }
        slots_[slot] = {};
        present_[slot] = false;
        return DuplicateCheckpointStorageError::none;
    }

    void fail_next_read(std::uint8_t slot) {
        fail_read_[slot] = true;
    }

    void set_next_write(std::uint8_t slot, WriteBehavior behavior) {
        next_write_[slot] = behavior;
    }

    void fail_next_erase(std::uint8_t slot) {
        fail_erase_[slot] = true;
    }

    std::array<std::uint8_t, kStoredDuplicateCheckpointBytes>& slot(
        std::uint8_t slot) {
        return slots_[slot];
    }

    bool present(std::uint8_t slot) const {
        return present_[slot];
    }

    std::uint32_t writes(std::uint8_t slot) const {
        return writes_[slot];
    }

    std::uint32_t erases(std::uint8_t slot) const {
        return erases_[slot];
    }

private:
    std::array<
        std::array<std::uint8_t, kStoredDuplicateCheckpointBytes>,
        2> slots_{};
    std::array<bool, 2> present_{};
    std::array<bool, 2> fail_read_{};
    std::array<WriteBehavior, 2> next_write_{};
    std::array<bool, 2> fail_erase_{};
    std::array<std::uint32_t, 2> reads_{};
    std::array<std::uint32_t, 2> writes_{};
    std::array<std::uint32_t, 2> erases_{};
};

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

void write_u64(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                -(static_cast<std::int32_t>(crc & 1U)));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

void repair_outer_crc(
    std::array<std::uint8_t, kStoredDuplicateCheckpointBytes>& bytes) {
    constexpr std::size_t offset = kStoredDuplicateCheckpointBytes - 4;
    write_u32(bytes.data() + offset, crc32(bytes.data(), offset));
}

void repair_inner_and_outer_crc(
    std::array<std::uint8_t, kStoredDuplicateCheckpointBytes>& bytes) {
    constexpr std::size_t inner_offset = 24;
    constexpr std::size_t inner_crc =
        inner_offset + kDuplicateCheckpointRecordBytes - 4;
    write_u32(bytes.data() + inner_crc,
              crc32(bytes.data() + inner_offset,
                    kDuplicateCheckpointRecordBytes - 4));
    repair_outer_crc(bytes);
}

DuplicateWindow window_with(
    const DuplicateKey& key,
    std::uint64_t observed_at = 100) {
    DuplicateWindow window{1000};
    EXPECT(window.observe(key, observed_at).observation ==
           DuplicateObservation::accepted);
    return window;
}

void test_blank_restore_and_first_save_round_trip() {
    FakeStorage storage{};
    DuplicateCheckpointStore store{storage, kGroupContext, kGroupEpoch};
    DuplicateWindow unchanged{1000};
    const DuplicateKey existing{91, kGroupEpoch, 93};
    EXPECT(unchanged.observe(existing, 0).valid());
    const auto empty = store.restore(unchanged, 50);
    EXPECT(empty.error == DuplicateCheckpointStoreError::no_checkpoint);
    EXPECT(!empty.restored && !empty.recovery_required);
    EXPECT(unchanged.observe(existing, 50).observation ==
           DuplicateObservation::duplicate);

    const DuplicateKey key{1, kGroupEpoch, 3};
    const auto original = window_with(key);
    const auto saved = store.save(original, 400);
    EXPECT(saved.saved());
    EXPECT(saved.generation == 1);
    EXPECT(saved.written_slot == DuplicateCheckpointSource::slot_a);
    EXPECT(storage.slot(0)[0] == 'O' && storage.slot(0)[1] == 'D' &&
           storage.slot(0)[2] == 'S' && storage.slot(0)[3] == '0');
    EXPECT(storage.slot(0)[4] == 1 && storage.slot(0)[5] == 24);
    EXPECT(storage.slot(0)[6] == 0xA0 && storage.slot(0)[7] == 0x02);
    EXPECT(storage.slot(0)[8] == 1 && storage.slot(0)[24] == 'O');
    EXPECT(storage.slot(0)[16] == 0x08 && storage.slot(0)[23] == 0x01);
    EXPECT(storage.slot(0)[696] == 0x44 && storage.slot(0)[699] == 0x11);

    DuplicateWindow restored{1000};
    const auto loaded = store.restore(restored, 50);
    EXPECT(loaded.error == DuplicateCheckpointStoreError::none);
    EXPECT(loaded.restored && loaded.recovery_required);
    EXPECT(loaded.generation == 1);
    EXPECT(loaded.source == DuplicateCheckpointSource::slot_a);
    EXPECT(restored.observe(key, 749).observation ==
           DuplicateObservation::duplicate);
    EXPECT(restored.observe(key, 750).observation ==
           DuplicateObservation::accepted);
}

void test_generations_rotate_and_newest_restores() {
    FakeStorage storage{};
    DuplicateCheckpointStore store{storage, kGroupContext, kGroupEpoch};
    const DuplicateKey first_key{10, kGroupEpoch, 12};
    const DuplicateKey second_key{20, kGroupEpoch, 22};
    auto first = window_with(first_key);
    EXPECT(store.save(first, 100).generation == 1);
    auto second = window_with(second_key);
    const auto saved_second = store.save(second, 100);
    EXPECT(saved_second.generation == 2);
    EXPECT(saved_second.written_slot == DuplicateCheckpointSource::slot_b);
    const auto saved_third = store.save(first, 100);
    EXPECT(saved_third.generation == 3);
    EXPECT(saved_third.written_slot == DuplicateCheckpointSource::slot_a);

    DuplicateWindow restored{1000};
    const auto loaded = store.restore(restored, 200);
    EXPECT(loaded.restored && !loaded.recovery_required);
    EXPECT(loaded.generation == 3);
    EXPECT(loaded.source == DuplicateCheckpointSource::slot_a);
    EXPECT(restored.observe(first_key, 200).observation ==
           DuplicateObservation::duplicate);
    EXPECT(restored.observe(second_key, 200).observation ==
           DuplicateObservation::accepted);
}

void test_partial_write_recovers_old_and_next_save_repairs_peer() {
    FakeStorage storage{};
    DuplicateCheckpointStore store{storage, kGroupContext, kGroupEpoch};
    const DuplicateKey old_key{30, kGroupEpoch, 32};
    const DuplicateKey new_key{40, kGroupEpoch, 42};
    auto old_window = window_with(old_key);
    auto new_window = window_with(new_key);
    EXPECT(store.save(old_window, 100).saved());
    storage.set_next_write(1, WriteBehavior::fail_after_partial_write);
    EXPECT(store.save(new_window, 100).error ==
           DuplicateCheckpointStoreError::storage_failure);

    DuplicateWindow recovered{1000};
    const auto loaded = store.restore(recovered, 200);
    EXPECT(loaded.restored && loaded.recovery_required);
    EXPECT(loaded.generation == 1);
    EXPECT(loaded.slot_b == DuplicateCheckpointSlotState::invalid);
    EXPECT(recovered.observe(old_key, 200).observation ==
           DuplicateObservation::duplicate);

    const auto repaired = store.save(new_window, 100);
    EXPECT(repaired.saved() && repaired.repaired_peer);
    EXPECT(repaired.generation == 2);
    EXPECT(repaired.written_slot == DuplicateCheckpointSource::slot_b);
}

void test_corrupt_newer_recovers_older_with_degradation_visible() {
    FakeStorage storage{};
    DuplicateCheckpointStore store{storage, kGroupContext, kGroupEpoch};
    const DuplicateKey first_key{50, kGroupEpoch, 52};
    const DuplicateKey second_key{60, kGroupEpoch, 62};
    auto first = window_with(first_key);
    auto second = window_with(second_key);
    EXPECT(store.save(first, 100).saved());
    EXPECT(store.save(second, 100).saved());
    storage.slot(1)[100] ^= 0x80U;

    DuplicateWindow restored{1000};
    const auto loaded = store.restore(restored, 500);
    EXPECT(loaded.restored && loaded.recovery_required);
    EXPECT(loaded.generation == 1);
    EXPECT(loaded.slot_b == DuplicateCheckpointSlotState::invalid);
    EXPECT(restored.observe(first_key, 500).observation ==
           DuplicateObservation::duplicate);
}

void test_inner_semantic_tamper_is_rejected_despite_repaired_crcs() {
    FakeStorage storage{};
    DuplicateCheckpointStore store{storage, kGroupContext, kGroupEpoch};
    auto first = window_with({70, kGroupEpoch, 72});
    auto second = window_with({80, kGroupEpoch, 82});
    EXPECT(store.save(first, 100).saved());
    EXPECT(store.save(second, 100).saved());
    auto& newer = storage.slot(1);
    newer[48] = 0;
    newer[49] = 0;
    newer[50] = 0;
    newer[51] = 0;
    repair_inner_and_outer_crc(newer);

    DuplicateWindow restored{1000};
    const auto loaded = store.restore(restored, 200);
    EXPECT(loaded.restored && loaded.recovery_required);
    EXPECT(loaded.slot_b == DuplicateCheckpointSlotState::invalid);
    EXPECT(loaded.generation == 1);
}

void test_read_and_verification_failures_do_not_mutate_window() {
    FakeStorage storage{};
    DuplicateCheckpointStore store{storage, kGroupContext, kGroupEpoch};
    const DuplicateKey stored_key{90, kGroupEpoch, 92};
    auto stored = window_with(stored_key);
    EXPECT(store.save(stored, 100).saved());

    DuplicateWindow unchanged{1000};
    const DuplicateKey existing{100, kGroupEpoch, 102};
    EXPECT(unchanged.observe(existing, 0).valid());
    storage.fail_next_read(1);
    const auto failed_load = store.restore(unchanged, 100);
    EXPECT(failed_load.error ==
           DuplicateCheckpointStoreError::storage_failure);
    EXPECT(!failed_load.restored);
    EXPECT(unchanged.observe(existing, 100).observation ==
           DuplicateObservation::duplicate);

    storage.set_next_write(1, WriteBehavior::corrupt_after_success);
    EXPECT(store.save(stored, 100).error ==
           DuplicateCheckpointStoreError::verification_failure);
}

void test_equal_generation_conflict_fails_without_restore_or_write() {
    FakeStorage storage{};
    DuplicateCheckpointStore store{storage, kGroupContext, kGroupEpoch};
    auto first = window_with({110, kGroupEpoch, 112});
    auto second = window_with({120, kGroupEpoch, 122});
    EXPECT(store.save(first, 100).saved());
    EXPECT(store.save(second, 100).saved());
    write_u64(storage.slot(1).data() + 8, 1);
    repair_outer_crc(storage.slot(1));
    const auto writes_before = storage.writes(0) + storage.writes(1);

    DuplicateWindow unchanged{1000};
    const DuplicateKey existing{130, kGroupEpoch, 132};
    EXPECT(unchanged.observe(existing, 0).valid());
    const auto loaded = store.restore(unchanged, 100);
    EXPECT(loaded.error ==
           DuplicateCheckpointStoreError::generation_conflict);
    EXPECT(!loaded.restored && loaded.recovery_required);
    EXPECT(unchanged.observe(existing, 100).observation ==
           DuplicateObservation::duplicate);
    EXPECT(store.save(first, 100).error ==
           DuplicateCheckpointStoreError::generation_conflict);
    EXPECT(storage.writes(0) + storage.writes(1) == writes_before);
}

void test_invalid_only_state_and_generation_exhaustion_fail_closed() {
    FakeStorage invalid_storage{};
    DuplicateCheckpointStore invalid_store{
        invalid_storage, kGroupContext, kGroupEpoch};
    auto window = window_with({140, kGroupEpoch, 142});
    EXPECT(invalid_store.save(window, 100).saved());
    invalid_storage.slot(0)[0] = 'X';
    DuplicateWindow restored{1000};
    const auto invalid = invalid_store.restore(restored, 100);
    EXPECT(invalid.error == DuplicateCheckpointStoreError::invalid_state);
    EXPECT(!invalid.restored && invalid.recovery_required);
    EXPECT(invalid_store.save(window, 100).error ==
           DuplicateCheckpointStoreError::invalid_state);
    EXPECT(invalid_storage.writes(0) == 1);

    FakeStorage exhausted_storage{};
    DuplicateCheckpointStore exhausted_store{
        exhausted_storage, kGroupContext, kGroupEpoch};
    EXPECT(exhausted_store.save(window, 100).saved());
    write_u64(exhausted_storage.slot(0).data() + 8,
              std::numeric_limits<std::uint64_t>::max());
    repair_outer_crc(exhausted_storage.slot(0));
    EXPECT(exhausted_store.save(window, 100).error ==
           DuplicateCheckpointStoreError::generation_exhausted);
}

void test_context_epoch_binding_and_legacy_media_fail_closed() {
    FakeStorage bound_storage{};
    DuplicateCheckpointStore bound_store{
        bound_storage, kGroupContext, kGroupEpoch};
    auto bound_window = window_with({160, kGroupEpoch, 162});
    EXPECT(bound_store.save(bound_window, 100).saved());

    DuplicateWindow unchanged{1000};
    const DuplicateKey existing{170, kGroupEpoch, 172};
    EXPECT(unchanged.observe(existing, 0).valid());
    DuplicateCheckpointStore wrong_context{
        bound_storage, kGroupContext + 1, kGroupEpoch};
    const auto mismatched_context = wrong_context.restore(unchanged, 100);
    EXPECT(mismatched_context.error ==
           DuplicateCheckpointStoreError::binding_mismatch);
    EXPECT(mismatched_context.slot_a ==
           DuplicateCheckpointSlotState::binding_mismatch);
    EXPECT(unchanged.observe(existing, 100).observation ==
           DuplicateObservation::duplicate);
    EXPECT(wrong_context.save(bound_window, 100).error ==
           DuplicateCheckpointStoreError::binding_mismatch);

    DuplicateCheckpointStore wrong_epoch{
        bound_storage, kGroupContext, kGroupEpoch + 1};
    EXPECT(wrong_epoch.restore(unchanged, 100).error ==
           DuplicateCheckpointStoreError::binding_mismatch);

    FakeStorage legacy_storage{};
    DuplicateCheckpointStore current_store{
        legacy_storage, kGroupContext, kGroupEpoch};
    EXPECT(current_store.save(bound_window, 100).saved());
    auto& legacy = legacy_storage.slot(0);
    legacy[4] = 0;
    std::fill(legacy.begin() + 16, legacy.begin() + 24, 0);
    std::fill(legacy.begin() + 696, legacy.begin() + 700, 0);
    repair_outer_crc(legacy);
    DuplicateCheckpointStore legacy_store{
        legacy_storage, kGroupContext, kGroupEpoch};
    DuplicateWindow legacy_target{1000};
    const auto legacy_result = legacy_store.restore(legacy_target, 100);
    EXPECT(legacy_result.error ==
           DuplicateCheckpointStoreError::legacy_unbound);
    EXPECT(legacy_result.slot_a ==
           DuplicateCheckpointSlotState::legacy_unbound);
    EXPECT(legacy_target.status(100).entries == 0);
    EXPECT(legacy_store.save(bound_window, 100).error ==
           DuplicateCheckpointStoreError::legacy_unbound);

    FakeStorage invalid_binding_storage{};
    DuplicateCheckpointStore invalid_binding{
        invalid_binding_storage, 0, kGroupEpoch};
    DuplicateWindow empty{1000};
    EXPECT(invalid_binding.restore(empty, 0).error ==
           DuplicateCheckpointStoreError::invalid_binding);
    EXPECT(invalid_binding.save(empty, 0).error ==
           DuplicateCheckpointStoreError::invalid_binding);

    FakeStorage wrong_key_storage{};
    DuplicateCheckpointStore wrong_key_store{
        wrong_key_storage, kGroupContext, kGroupEpoch};
    auto wrong_key_window = window_with({180, kGroupEpoch + 1, 182});
    EXPECT(wrong_key_store.save(wrong_key_window, 100).error ==
           DuplicateCheckpointStoreError::binding_mismatch);
}

void test_reset_attempts_both_slots_and_allows_fresh_generation() {
    FakeStorage storage{};
    DuplicateCheckpointStore store{storage, kGroupContext, kGroupEpoch};
    auto window = window_with({150, kGroupEpoch, 152});
    EXPECT(store.save(window, 100).saved());
    EXPECT(store.save(window, 100).saved());
    storage.fail_next_erase(0);
    EXPECT(store.reset() == DuplicateCheckpointStoreError::storage_failure);
    EXPECT(storage.erases(0) == 1 && storage.erases(1) == 1);
    EXPECT(!storage.present(1));
    EXPECT(store.reset() == DuplicateCheckpointStoreError::none);
    const auto fresh = store.save(window, 100);
    EXPECT(fresh.saved() && fresh.generation == 1);
}

}  // namespace

int main() {
    test_blank_restore_and_first_save_round_trip();
    test_generations_rotate_and_newest_restores();
    test_partial_write_recovers_old_and_next_save_repairs_peer();
    test_corrupt_newer_recovers_older_with_degradation_visible();
    test_inner_semantic_tamper_is_rejected_despite_repaired_crcs();
    test_read_and_verification_failures_do_not_mutate_window();
    test_equal_generation_conflict_fails_without_restore_or_write();
    test_invalid_only_state_and_generation_exhaustion_fail_closed();
    test_context_epoch_binding_and_legacy_media_fail_closed();
    test_reset_attempts_both_slots_and_allows_fresh_generation();

    if (failures != 0) {
        std::cerr << failures
                  << " duplicate checkpoint store assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 duplicate checkpoint store scenario groups\n";
    return EXIT_SUCCESS;
}
