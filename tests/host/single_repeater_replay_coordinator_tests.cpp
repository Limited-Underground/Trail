#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/single_repeater_replay_coordinator.hpp"

namespace {

using namespace opentrail::delivery;

constexpr std::uint64_t kGroupContext = 0x1001;
constexpr std::uint64_t kSource = 0x2001;
constexpr std::uint64_t kRepeater = 0x3001;
constexpr std::uint64_t kDestination = 0x4001;
constexpr std::uint32_t kEpoch = 7;
constexpr std::uint32_t kRetentionMs = 10000;
const std::array<std::uint8_t, 6> kFrame{
    0x4F, 0x54, 0x01, 0xAA, 0x00, 0xFF};

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

class FakeStorage final : public DuplicateCheckpointStorage {
public:
    DuplicateCheckpointStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override {
        if (slot >= kDuplicateCheckpointSlotCount || output == nullptr ||
            size != kStoredDuplicateCheckpointBytes) {
            return DuplicateCheckpointStorageError::invalid_argument;
        }
        if (fail_next_read_[slot]) {
            fail_next_read_[slot] = false;
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
        if (slot >= kDuplicateCheckpointSlotCount || data == nullptr ||
            size != kStoredDuplicateCheckpointBytes) {
            return DuplicateCheckpointStorageError::invalid_argument;
        }
        if (fail_next_write_) {
            fail_next_write_ = false;
            return DuplicateCheckpointStorageError::io_failure;
        }
        std::copy(data, data + size, slots_[slot].begin());
        present_[slot] = true;
        ++writes_;
        return DuplicateCheckpointStorageError::none;
    }

    DuplicateCheckpointStorageError erase_slot(std::uint8_t slot) override {
        if (slot >= kDuplicateCheckpointSlotCount) {
            return DuplicateCheckpointStorageError::invalid_argument;
        }
        slots_[slot] = {};
        present_[slot] = false;
        return DuplicateCheckpointStorageError::none;
    }

    void fail_next_read(std::uint8_t slot) {
        fail_next_read_[slot] = true;
    }

    void fail_next_write() {
        fail_next_write_ = true;
    }

    [[nodiscard]] std::uint32_t writes() const {
        return writes_;
    }

    std::array<std::uint8_t, kStoredDuplicateCheckpointBytes>& slot(
        std::uint8_t slot) {
        return slots_[slot];
    }

private:
    std::array<
        std::array<std::uint8_t, kStoredDuplicateCheckpointBytes>,
        kDuplicateCheckpointSlotCount>
        slots_{};
    std::array<bool, kDuplicateCheckpointSlotCount> present_{};
    std::array<bool, kDuplicateCheckpointSlotCount> fail_next_read_{};
    bool fail_next_write_{false};
    std::uint32_t writes_{0};
};

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
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

void convert_slot_to_legacy_v0(FakeStorage& storage, std::uint8_t slot) {
    auto& bytes = storage.slot(slot);
    bytes[4] = 0;
    std::fill(bytes.begin() + 16, bytes.begin() + 24, 0);
    std::fill(bytes.begin() + 696, bytes.begin() + 700, 0);
    write_u32(bytes.data() + 700, crc32(bytes.data(), 700));
}

VerifiedForwardingMetadata metadata(std::uint32_t message_id = 1) {
    return {
        kGroupContext,
        kSource,
        kDestination,
        kEpoch,
        message_id,
        true,
        true,
        true,
    };
}

SingleRepeaterPolicy policy(
    std::size_t depth = 4,
    std::uint16_t rate = 8) {
    return {1, true, depth, rate, 1000, 100};
}

ReplayStorageBindingEvidence binding(bool allow_empty = false) {
    return {kGroupContext, kEpoch, true, allow_empty};
}

void expect_exact_frame(const RepeaterReplayTransmitResult& output) {
    EXPECT(output.error == RepeaterReplayError::none);
    EXPECT(output.forwarding.has_frame);
    EXPECT(output.forwarding.frame.size == kFrame.size());
    for (std::size_t index = 0; index < kFrame.size(); ++index) {
        EXPECT(output.forwarding.frame.bytes[index] == kFrame[index]);
    }
}

void test_authorized_empty_store_initializes_before_operation() {
    FakeStorage storage{};
    DuplicateCheckpointStore store{storage, kGroupContext, kEpoch};
    DuplicateWindow live{kRetentionMs};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), live};
    SingleRepeaterReplayCoordinator coordinator{
        kRetentionMs, kGroupContext, kEpoch, live, store, forwarder};

    const auto boot = coordinator.boot(binding(true), 0);
    EXPECT(boot.operational());
    EXPECT(boot.outcome == RepeaterReplayBootOutcome::first_boot_initialized);
    EXPECT(boot.generation == 1);
    EXPECT(storage.writes() == 1);
    const auto status = coordinator.status();
    EXPECT(status.boot_attempted && status.operational);
    EXPECT(status.persisted_generation == 1);
}

void test_unprovisioned_empty_store_requires_service() {
    FakeStorage storage{};
    DuplicateCheckpointStore store{storage, kGroupContext, kEpoch};
    DuplicateWindow live{kRetentionMs};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), live};
    SingleRepeaterReplayCoordinator coordinator{
        kRetentionMs, kGroupContext, kEpoch, live, store, forwarder};

    const auto boot = coordinator.boot(binding(false), 0);
    EXPECT(boot.error == RepeaterReplayError::missing_checkpoint);
    EXPECT(!coordinator.status().operational);
    EXPECT(storage.writes() == 0);
    EXPECT(coordinator.process_and_persist(
               metadata(), {kFrame.data(), kFrame.size()}, 1)
               .error == RepeaterReplayError::not_operational);
    EXPECT(coordinator.next_transmit(1).error ==
           RepeaterReplayError::not_operational);
}

void test_persisted_observation_survives_restart() {
    FakeStorage storage{};
    DuplicateCheckpointStore first_store{storage, kGroupContext, kEpoch};
    DuplicateWindow first_live{kRetentionMs};
    SingleRepeaterForwarder first_forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), first_live};
    SingleRepeaterReplayCoordinator first{
        kRetentionMs,
        kGroupContext,
        kEpoch,
        first_live,
        first_store,
        first_forwarder};
    EXPECT(first.boot(binding(true), 0).operational());

    const auto processed = first.process_and_persist(
        metadata(10), {kFrame.data(), kFrame.size()}, 10);
    EXPECT(processed.error == RepeaterReplayError::none);
    EXPECT(processed.forwarding.queued);
    EXPECT(processed.replay_state_persisted);
    EXPECT(processed.generation == 2);
    expect_exact_frame(first.next_transmit(11));

    DuplicateCheckpointStore second_store{storage, kGroupContext, kEpoch};
    DuplicateWindow second_live{kRetentionMs};
    SingleRepeaterForwarder second_forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), second_live};
    SingleRepeaterReplayCoordinator second{
        kRetentionMs,
        kGroupContext,
        kEpoch,
        second_live,
        second_store,
        second_forwarder};
    const auto reboot = second.boot(binding(false), 20);
    EXPECT(reboot.operational());
    EXPECT(reboot.outcome == RepeaterReplayBootOutcome::restored);
    EXPECT(reboot.generation == 2);
    const auto replay = second.process_and_persist(
        metadata(10), {kFrame.data(), kFrame.size()}, 21);
    EXPECT(replay.error == RepeaterReplayError::none);
    EXPECT(replay.forwarding.disposition ==
           SingleRepeaterDisposition::duplicate);
    EXPECT(!replay.replay_state_persisted);
    EXPECT(!second.next_transmit(22).forwarding.has_frame);
}

void test_failed_save_disables_transmission() {
    FakeStorage storage{};
    DuplicateCheckpointStore store{storage, kGroupContext, kEpoch};
    DuplicateWindow live{kRetentionMs};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), live};
    SingleRepeaterReplayCoordinator coordinator{
        kRetentionMs, kGroupContext, kEpoch, live, store, forwarder};
    EXPECT(coordinator.boot(binding(true), 0).operational());
    storage.fail_next_write();

    const auto processed = coordinator.process_and_persist(
        metadata(20), {kFrame.data(), kFrame.size()}, 10);
    EXPECT(processed.forwarding.queued);
    EXPECT(processed.error == RepeaterReplayError::persistence_failure);
    EXPECT(!processed.replay_state_persisted);
    EXPECT(coordinator.next_transmit(11).error ==
           RepeaterReplayError::not_operational);
    const auto status = coordinator.status();
    EXPECT(!status.operational && status.persistence_failures == 1);
}

void test_congestion_observation_is_persisted() {
    FakeStorage storage{};
    DuplicateCheckpointStore store{storage, kGroupContext, kEpoch};
    DuplicateWindow live{kRetentionMs};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(1), live};
    SingleRepeaterReplayCoordinator coordinator{
        kRetentionMs, kGroupContext, kEpoch, live, store, forwarder};
    EXPECT(coordinator.boot(binding(true), 0).operational());
    EXPECT(coordinator.process_and_persist(
               metadata(30), {kFrame.data(), kFrame.size()}, 10)
               .replay_state_persisted);
    const auto congested = coordinator.process_and_persist(
        metadata(31), {kFrame.data(), kFrame.size()}, 11);
    EXPECT(congested.forwarding.disposition ==
           SingleRepeaterDisposition::queue_full);
    EXPECT(congested.replay_state_persisted);
    EXPECT(congested.generation == 3);

    DuplicateCheckpointStore reboot_store{storage, kGroupContext, kEpoch};
    DuplicateWindow reboot_live{kRetentionMs};
    SingleRepeaterForwarder reboot_forwarder{
        kRepeater, kGroupContext, kEpoch, policy(1), reboot_live};
    SingleRepeaterReplayCoordinator reboot{
        kRetentionMs,
        kGroupContext,
        kEpoch,
        reboot_live,
        reboot_store,
        reboot_forwarder};
    EXPECT(reboot.boot(binding(false), 20).operational());
    EXPECT(reboot.process_and_persist(
               metadata(31), {kFrame.data(), kFrame.size()}, 21)
               .forwarding.disposition ==
           SingleRepeaterDisposition::duplicate);
}

void test_known_degraded_store_is_repaired_before_operation() {
    FakeStorage storage{};
    DuplicateCheckpointStore first_store{storage, kGroupContext, kEpoch};
    DuplicateWindow first_live{kRetentionMs};
    SingleRepeaterForwarder first_forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), first_live};
    SingleRepeaterReplayCoordinator first{
        kRetentionMs,
        kGroupContext,
        kEpoch,
        first_live,
        first_store,
        first_forwarder};
    EXPECT(first.boot(binding(true), 0).operational());
    EXPECT(first.process_and_persist(
               metadata(40), {kFrame.data(), kFrame.size()}, 10)
               .replay_state_persisted);
    EXPECT(storage.erase_slot(0) == DuplicateCheckpointStorageError::none);

    DuplicateCheckpointStore repair_store{
        storage, kGroupContext, kEpoch};
    DuplicateWindow repair_live{kRetentionMs};
    SingleRepeaterForwarder repair_forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), repair_live};
    SingleRepeaterReplayCoordinator repair{
        kRetentionMs,
        kGroupContext,
        kEpoch,
        repair_live,
        repair_store,
        repair_forwarder};
    const auto boot = repair.boot(binding(false), 20);
    EXPECT(boot.operational());
    EXPECT(boot.outcome ==
           RepeaterReplayBootOutcome::restored_and_repaired);
    EXPECT(boot.generation == 3);
    EXPECT(repair.process_and_persist(
               metadata(40), {kFrame.data(), kFrame.size()}, 21)
               .forwarding.disposition ==
           SingleRepeaterDisposition::duplicate);
}

void test_bound_media_mismatch_and_legacy_fail_closed() {
    FakeStorage storage{};
    DuplicateCheckpointStore seed_store{
        storage, kGroupContext, kEpoch + 1};
    DuplicateWindow seed{kRetentionMs};
    EXPECT(seed.observe({kSource, kEpoch + 1, 50}, 0).valid());
    EXPECT(seed_store.save(seed, 0).saved());

    DuplicateCheckpointStore store{storage, kGroupContext, kEpoch};
    DuplicateWindow live{kRetentionMs};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), live};
    SingleRepeaterReplayCoordinator coordinator{
        kRetentionMs, kGroupContext, kEpoch, live, store, forwarder};
    const auto boot = coordinator.boot(binding(false), 1);
    EXPECT(boot.error ==
           RepeaterReplayError::checkpoint_binding_mismatch);
    EXPECT(boot.store_error ==
           DuplicateCheckpointStoreError::binding_mismatch);
    EXPECT(!coordinator.status().operational);
    EXPECT(live.status(1).entries == 0);

    FakeStorage legacy_storage{};
    DuplicateCheckpointStore legacy_seed{
        legacy_storage, kGroupContext, kEpoch};
    DuplicateWindow legacy_seed_window{kRetentionMs};
    EXPECT(legacy_seed.save(legacy_seed_window, 0).saved());
    convert_slot_to_legacy_v0(legacy_storage, 0);
    DuplicateCheckpointStore legacy_store{
        legacy_storage, kGroupContext, kEpoch};
    DuplicateWindow legacy_live{kRetentionMs};
    SingleRepeaterForwarder legacy_forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), legacy_live};
    SingleRepeaterReplayCoordinator legacy{
        kRetentionMs,
        kGroupContext,
        kEpoch,
        legacy_live,
        legacy_store,
        legacy_forwarder};
    const auto legacy_boot = legacy.boot(binding(false), 1);
    EXPECT(legacy_boot.error == RepeaterReplayError::legacy_checkpoint);
    EXPECT(legacy_boot.store_error ==
           DuplicateCheckpointStoreError::legacy_unbound);
    EXPECT(!legacy.status().operational);
}

void test_unreadable_slot_fails_closed() {
    FakeStorage storage{};
    DuplicateCheckpointStore seed_store{storage, kGroupContext, kEpoch};
    DuplicateWindow seed{kRetentionMs};
    EXPECT(seed.observe({kSource, kEpoch, 60}, 0).valid());
    EXPECT(seed_store.save(seed, 0).saved());
    storage.fail_next_read(1);

    DuplicateCheckpointStore store{storage, kGroupContext, kEpoch};
    DuplicateWindow live{kRetentionMs};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), live};
    SingleRepeaterReplayCoordinator coordinator{
        kRetentionMs, kGroupContext, kEpoch, live, store, forwarder};
    const auto boot = coordinator.boot(binding(false), 1);
    EXPECT(boot.error == RepeaterReplayError::store_failure);
    EXPECT(boot.store_error == DuplicateCheckpointStoreError::storage_failure);
    EXPECT(!coordinator.status().operational);
    EXPECT(live.status(1).entries == 0);
}

void test_binding_lifecycle_and_clean_boot_are_enforced() {
    FakeStorage invalid_storage{};
    DuplicateCheckpointStore invalid_store{
        invalid_storage, kGroupContext, kEpoch};
    DuplicateWindow invalid_live{kRetentionMs};
    SingleRepeaterForwarder invalid_forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), invalid_live};
    SingleRepeaterReplayCoordinator invalid{
        kRetentionMs,
        kGroupContext,
        kEpoch,
        invalid_live,
        invalid_store,
        invalid_forwarder};
    auto unprotected = binding(true);
    unprotected.protected_namespace_verified = false;
    EXPECT(invalid.boot(unprotected, 0).error ==
           RepeaterReplayError::invalid_binding);
    EXPECT(invalid.boot(binding(true), 0).error ==
           RepeaterReplayError::already_booted);

    FakeStorage mismatch_storage{};
    DuplicateCheckpointStore mismatch_store{
        mismatch_storage, kGroupContext, kEpoch};
    DuplicateWindow mismatch_live{kRetentionMs};
    SingleRepeaterForwarder mismatch_forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), mismatch_live};
    SingleRepeaterReplayCoordinator mismatch{
        kRetentionMs,
        kGroupContext,
        kEpoch,
        mismatch_live,
        mismatch_store,
        mismatch_forwarder};
    auto wrong_group = binding(true);
    ++wrong_group.group_context_id;
    EXPECT(mismatch.boot(wrong_group, 0).error ==
           RepeaterReplayError::invalid_binding);

    FakeStorage store_mismatch_storage{};
    DuplicateCheckpointStore store_mismatch_store{
        store_mismatch_storage, kGroupContext + 1, kEpoch};
    DuplicateWindow store_mismatch_live{kRetentionMs};
    SingleRepeaterForwarder store_mismatch_forwarder{
        kRepeater,
        kGroupContext,
        kEpoch,
        policy(),
        store_mismatch_live};
    SingleRepeaterReplayCoordinator store_mismatch{
        kRetentionMs,
        kGroupContext,
        kEpoch,
        store_mismatch_live,
        store_mismatch_store,
        store_mismatch_forwarder};
    EXPECT(store_mismatch.boot(binding(true), 0).error ==
           RepeaterReplayError::invalid_binding);

    FakeStorage dirty_storage{};
    DuplicateCheckpointStore dirty_store{
        dirty_storage, kGroupContext, kEpoch};
    DuplicateWindow dirty_live{kRetentionMs};
    EXPECT(dirty_live.observe({kSource, kEpoch, 70}, 0).valid());
    SingleRepeaterForwarder dirty_forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), dirty_live};
    SingleRepeaterReplayCoordinator dirty{
        kRetentionMs,
        kGroupContext,
        kEpoch,
        dirty_live,
        dirty_store,
        dirty_forwarder};
    EXPECT(dirty.boot(binding(true), 0).error ==
           RepeaterReplayError::live_window_not_empty);
}

}  // namespace

int main() {
    test_authorized_empty_store_initializes_before_operation();
    test_unprovisioned_empty_store_requires_service();
    test_persisted_observation_survives_restart();
    test_failed_save_disables_transmission();
    test_congestion_observation_is_persisted();
    test_known_degraded_store_is_repaired_before_operation();
    test_bound_media_mismatch_and_legacy_fail_closed();
    test_unreadable_slot_fails_closed();
    test_binding_lifecycle_and_clean_boot_are_enforced();

    if (failures != 0) {
        std::cerr << failures
                  << " single-repeater replay coordinator assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 9 reboot-safe repeater replay scenario groups\n";
    return EXIT_SUCCESS;
}
