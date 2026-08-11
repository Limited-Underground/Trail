#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/map_selector_domain_record.hpp"
#include "opentrail/map_selector_domain_store.hpp"

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

class FakeMapSelectorDomainStorage final
    : public MapSelectorDomainStorage {
public:
    MapSelectorDomainStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override {
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
        present[slot] = true;
        ++write_calls;
        if (partial_write_bytes < size) {
            slots[slot].fill(0);
            std::copy(
                data, data + partial_write_bytes, slots[slot].begin());
            partial_write_bytes = size;
            return MapSelectorDomainStorageError::io_failure;
        }
        std::copy(data, data + size, slots[slot].begin());
        if (corrupt_after_write) {
            slots[slot][24] ^= 0x01U;
            corrupt_after_write = false;
        }
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
        if (fail_commit_slot == static_cast<int>(slot)) {
            if (commit_then_fail) {
                slots[slot][offset] = value;
            }
            return MapSelectorDomainStorageError::io_failure;
        }
        slots[slot][offset] = value;
        if (corrupt_after_commit) {
            slots[slot][40] ^= 0x01U;
            corrupt_after_commit = false;
        }
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
    int fail_read_slot{-1};
    int fail_commit_slot{-1};
    std::size_t partial_write_bytes{kMapSelectorDomainRecordBytes};
    bool corrupt_after_write{false};
    bool corrupt_after_commit{false};
    bool commit_then_fail{false};
    std::uint32_t write_calls{0};
    std::uint32_t commit_calls{0};
};

MapSelectorDomainId domain(std::uint8_t seed = 1) {
    MapSelectorDomainId value{};
    for (std::size_t index = 0; index < value.size(); ++index) {
        value[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

MapSelectorDomainRecord pending_first(std::uint64_t record_generation = 1) {
    return {
        kMapSelectorDomainRecordVersion,
        MapSelectorDomainRecordState::pending_first_baseline,
        MapSelectorDomainRecordOrigin::fresh_device_commissioning,
        domain(),
        {},
        0,
        0,
        1,
        record_generation};
}

MapSelectorDomainRecord active_first(
    std::uint64_t record_generation = 2,
    std::uint64_t accepted_selector_generation = 5) {
    auto record = pending_first(record_generation);
    record.state = MapSelectorDomainRecordState::active;
    record.accepted_selector_generation = accepted_selector_generation;
    return record;
}

MapSelectorDomainRecord pending_replacement(
    const MapSelectorDomainRecord& current,
    std::uint8_t new_domain_seed,
    std::uint64_t record_generation,
    std::uint64_t retired_selector_generation) {
    return {
        kMapSelectorDomainRecordVersion,
        MapSelectorDomainRecordState::pending_selector_reseed,
        MapSelectorDomainRecordOrigin::same_device_replacement,
        domain(new_domain_seed),
        current.current_domain,
        retired_selector_generation,
        0,
        current.domain_epoch + 1,
        record_generation};
}

std::array<std::uint8_t, kMapSelectorDomainRecordBytes> encoded_record(
    const MapSelectorDomainRecord& record) {
    std::array<std::uint8_t, kMapSelectorDomainRecordBytes> bytes{};
    EXPECT(encode_map_selector_domain_record(
               record, bytes.data(), bytes.size())
               .succeeded());
    return bytes;
}

void test_empty_media_accepts_only_first_pending_generation() {
    FakeMapSelectorDomainStorage storage{};
    MapSelectorDomainStore store{storage};
    EXPECT(store.inspect().error == MapSelectorDomainStoreError::no_record);

    EXPECT(store.save(active_first(1)).error ==
           MapSelectorDomainStoreError::transition_rejected);
    EXPECT(store.save(pending_first(2)).error ==
           MapSelectorDomainStoreError::generation_mismatch);
    EXPECT(storage.write_calls == 0);

    const auto saved = store.save(pending_first());
    EXPECT(saved.saved());
    EXPECT(saved.generation == 1);
    EXPECT(saved.written_slot == MapSelectorDomainSource::slot_a);
    EXPECT(!saved.commit_uncertain);
    EXPECT(storage.write_calls == 1);
    EXPECT(storage.commit_calls == 1);
    EXPECT(storage.slots[0][kMapSelectorDomainRecordCommitOffset] ==
           kMapSelectorDomainRecordCommitMarker);

    const auto inspected = store.inspect();
    EXPECT(inspected.error == MapSelectorDomainStoreError::none);
    EXPECT(inspected.record_available);
    EXPECT(inspected.recovery_required);
    EXPECT(inspected.source == MapSelectorDomainSource::slot_a);
    EXPECT(inspected.record.state ==
           MapSelectorDomainRecordState::pending_first_baseline);
    EXPECT(inspected.record.record_generation == 1);
}

void test_activation_maintenance_and_selector_advance_rotate() {
    FakeMapSelectorDomainStorage storage{};
    MapSelectorDomainStore store{storage};
    EXPECT(store.save(pending_first()).saved());

    const auto active = active_first();
    const auto activated = store.save(active);
    EXPECT(activated.saved());
    EXPECT(activated.written_slot == MapSelectorDomainSource::slot_b);
    EXPECT(!store.inspect().recovery_required);

    auto maintenance = active;
    maintenance.record_generation = 3;
    EXPECT(store.save(maintenance).saved());

    auto advanced = maintenance;
    advanced.accepted_selector_generation = 6;
    advanced.record_generation = 4;
    const auto saved = store.save(advanced);
    EXPECT(saved.saved());
    EXPECT(saved.written_slot == MapSelectorDomainSource::slot_b);
    const auto inspected = store.inspect();
    EXPECT(inspected.record.record_generation == 4);
    EXPECT(inspected.record.accepted_selector_generation == 6);
}

void test_replacement_chain_preserves_exact_domain_linkage() {
    FakeMapSelectorDomainStorage storage{};
    MapSelectorDomainStore store{storage};
    EXPECT(store.save(pending_first()).saved());
    const auto first = active_first();
    EXPECT(store.save(first).saved());

    const auto pending = pending_replacement(first, 41, 3, 8);
    EXPECT(store.save(pending).saved());
    auto active = pending;
    active.state = MapSelectorDomainRecordState::active;
    active.accepted_selector_generation = 9;
    active.record_generation = 4;
    EXPECT(store.save(active).saved());

    const auto next = pending_replacement(active, 81, 5, 11);
    EXPECT(store.save(next).saved());
    const auto inspected = store.inspect();
    EXPECT(inspected.record.current_domain == next.current_domain);
    EXPECT(inspected.record.retired_domain == active.current_domain);
    EXPECT(inspected.record.retired_selector_generation == 11);
    EXPECT(inspected.record.domain_epoch == 3);
}

void test_stale_backward_and_reused_domain_transitions_do_not_write() {
    FakeMapSelectorDomainStorage storage{};
    MapSelectorDomainStore store{storage};
    storage.seed(0, encoded_record(active_first()));
    const auto writes_before = storage.write_calls;

    auto stale = active_first(2);
    EXPECT(store.save(stale).error ==
           MapSelectorDomainStoreError::generation_mismatch);

    auto backwards = pending_first(3);
    EXPECT(store.save(backwards).error ==
           MapSelectorDomainStoreError::transition_rejected);

    auto changed_binding = active_first(3, 6);
    changed_binding.current_domain = domain(55);
    EXPECT(store.save(changed_binding).error ==
           MapSelectorDomainStoreError::transition_rejected);

    auto low_floor = pending_replacement(active_first(), 41, 3, 4);
    EXPECT(store.save(low_floor).error ==
           MapSelectorDomainStoreError::transition_rejected);

    auto skipped_epoch = pending_replacement(active_first(), 41, 3, 5);
    ++skipped_epoch.domain_epoch;
    EXPECT(store.save(skipped_epoch).error ==
           MapSelectorDomainStoreError::transition_rejected);

    auto wrong_retired = pending_replacement(active_first(), 41, 3, 5);
    wrong_retired.retired_domain = domain(81);
    EXPECT(store.save(wrong_retired).error ==
           MapSelectorDomainStoreError::transition_rejected);

    EXPECT(storage.write_calls == writes_before);

    FakeMapSelectorDomainStorage reused_storage{};
    MapSelectorDomainStore reused_store{reused_storage};
    MapSelectorDomainRecord current_replacement{
        kMapSelectorDomainRecordVersion,
        MapSelectorDomainRecordState::active,
        MapSelectorDomainRecordOrigin::same_device_replacement,
        domain(41),
        domain(),
        5,
        6,
        2,
        7};
    reused_storage.seed(0, encoded_record(current_replacement));
    auto reused = pending_replacement(
        current_replacement, 81, 8, 6);
    reused.current_domain = current_replacement.retired_domain;
    EXPECT(validate_map_selector_domain_record(reused) ==
           MapSelectorDomainRecordError::none);
    EXPECT(reused_store.save(reused).error ==
           MapSelectorDomainStoreError::transition_rejected);
    EXPECT(reused_storage.write_calls == 0);
}

void test_every_interrupted_prepared_write_preserves_prior_record() {
    constexpr std::array<std::size_t, 12> boundaries{
        0, 1, 4, 8, 24, 40, 56, 64, 72, 75, 76, 79};
    for (const auto boundary : boundaries) {
        FakeMapSelectorDomainStorage storage{};
        MapSelectorDomainStore store{storage};
        const auto current = active_first();
        storage.seed(0, encoded_record(current));
        auto next = current;
        next.record_generation = 3;
        storage.partial_write_bytes = boundary;

        const auto failed = store.save(next);
        EXPECT(failed.error == MapSelectorDomainStoreError::storage_failure);
        EXPECT(!failed.commit_uncertain);
        const auto inspected = store.inspect();
        EXPECT(inspected.error == MapSelectorDomainStoreError::none);
        EXPECT(inspected.record.record_generation == 2);
        EXPECT(inspected.source == MapSelectorDomainSource::slot_a);
        EXPECT(inspected.recovery_required);
    }
}

void test_commit_error_is_uncertain_and_boot_reconciles_media() {
    FakeMapSelectorDomainStorage storage{};
    MapSelectorDomainStore store{storage};
    const auto current = active_first();
    storage.seed(0, encoded_record(current));
    auto next = current;
    next.record_generation = 3;
    storage.fail_commit_slot = 1;

    const auto uncommitted = store.save(next);
    EXPECT(uncommitted.error ==
           MapSelectorDomainStoreError::storage_failure);
    EXPECT(uncommitted.commit_uncertain);
    EXPECT(store.inspect().record.record_generation == 2);

    FakeMapSelectorDomainStorage committed_storage{};
    MapSelectorDomainStore committed_store{committed_storage};
    committed_storage.seed(0, encoded_record(current));
    committed_storage.fail_commit_slot = 1;
    committed_storage.commit_then_fail = true;
    const auto uncertain = committed_store.save(next);
    EXPECT(uncertain.commit_uncertain);
    const auto reconciled = committed_store.inspect();
    EXPECT(reconciled.error == MapSelectorDomainStoreError::none);
    EXPECT(reconciled.record.record_generation == 3);
    EXPECT(reconciled.source == MapSelectorDomainSource::slot_b);
}

void test_corrupt_readback_preserves_prior_and_peer_can_be_repaired() {
    FakeMapSelectorDomainStorage storage{};
    MapSelectorDomainStore store{storage};
    const auto current = active_first();
    const auto prior_bytes = encoded_record(current);
    storage.seed(0, prior_bytes);
    auto next = current;
    next.record_generation = 3;
    storage.corrupt_after_commit = true;

    const auto failed = store.save(next);
    EXPECT(failed.error ==
           MapSelectorDomainStoreError::verification_failure);
    EXPECT(failed.commit_uncertain);
    EXPECT(store.inspect().record.record_generation == 2);

    const auto repaired = store.save(next);
    EXPECT(repaired.saved());
    EXPECT(repaired.repaired_peer);
    EXPECT(repaired.written_slot == MapSelectorDomainSource::slot_b);
    EXPECT(storage.slots[0] == prior_bytes);
    EXPECT(store.inspect().record.record_generation == 3);
}

void test_unreadable_or_invalid_only_media_fails_closed() {
    FakeMapSelectorDomainStorage storage{};
    MapSelectorDomainStore store{storage};
    storage.seed(0, encoded_record(active_first()));
    storage.fail_read_slot = 1;
    const auto unreadable = store.inspect();
    EXPECT(unreadable.error ==
           MapSelectorDomainStoreError::storage_failure);
    EXPECT(!unreadable.record_available);
    auto next = active_first(3);
    EXPECT(store.save(next).error ==
           MapSelectorDomainStoreError::storage_failure);

    FakeMapSelectorDomainStorage invalid_storage{};
    MapSelectorDomainStore invalid_store{invalid_storage};
    auto corrupt = encoded_record(active_first());
    corrupt[40] ^= 0x01U;
    invalid_storage.seed(0, corrupt);
    const auto invalid = invalid_store.inspect();
    EXPECT(invalid.error == MapSelectorDomainStoreError::invalid_state);
    EXPECT(!invalid.record_available);
    EXPECT(invalid.recovery_required);
    EXPECT(invalid_store.save(pending_first()).error ==
           MapSelectorDomainStoreError::invalid_state);
}

void test_equal_generation_conflict_fails_but_identical_copies_work() {
    FakeMapSelectorDomainStorage storage{};
    MapSelectorDomainStore store{storage};
    const auto first = active_first();
    auto different = first;
    different.accepted_selector_generation = 6;
    storage.seed(0, encoded_record(first));
    storage.seed(1, encoded_record(different));
    const auto conflicted = store.inspect();
    EXPECT(conflicted.error ==
           MapSelectorDomainStoreError::generation_conflict);
    EXPECT(!conflicted.record_available);
    auto next = first;
    next.record_generation = 3;
    EXPECT(store.save(next).error ==
           MapSelectorDomainStoreError::generation_conflict);

    FakeMapSelectorDomainStorage duplicate_storage{};
    MapSelectorDomainStore duplicate_store{duplicate_storage};
    const auto bytes = encoded_record(first);
    duplicate_storage.seed(0, bytes);
    duplicate_storage.seed(1, bytes);
    const auto duplicate = duplicate_store.inspect();
    EXPECT(duplicate.error == MapSelectorDomainStoreError::none);
    EXPECT(!duplicate.recovery_required);
    EXPECT(duplicate_store.save(next).saved());
}

void test_generation_exhaustion_and_invalid_record_are_rejected() {
    FakeMapSelectorDomainStorage storage{};
    MapSelectorDomainStore store{storage};
    auto exhausted = active_first(
        std::numeric_limits<std::uint64_t>::max());
    storage.seed(0, encoded_record(exhausted));
    EXPECT(store.save(exhausted).error ==
           MapSelectorDomainStoreError::generation_exhausted);

    FakeMapSelectorDomainStorage invalid_storage{};
    MapSelectorDomainStore invalid_store{invalid_storage};
    auto invalid = pending_first();
    invalid.current_domain = {};
    EXPECT(invalid_store.save(invalid).error ==
           MapSelectorDomainStoreError::record_rejected);
    EXPECT(invalid_storage.write_calls == 0);
}

}  // namespace

int main() {
    test_empty_media_accepts_only_first_pending_generation();
    test_activation_maintenance_and_selector_advance_rotate();
    test_replacement_chain_preserves_exact_domain_linkage();
    test_stale_backward_and_reused_domain_transitions_do_not_write();
    test_every_interrupted_prepared_write_preserves_prior_record();
    test_commit_error_is_uncertain_and_boot_reconciles_media();
    test_corrupt_readback_preserves_prior_and_peer_can_be_repaired();
    test_unreadable_or_invalid_only_media_fails_closed();
    test_equal_generation_conflict_fails_but_identical_copies_work();
    test_generation_exhaustion_and_invalid_record_are_rejected();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector domain-store assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 map selector domain-store scenario groups\n";
    return EXIT_SUCCESS;
}
