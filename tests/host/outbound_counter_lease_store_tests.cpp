#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "memory_persistent_storage.hpp"
#include "opentrail/outbound_counter_lease_store.hpp"

namespace {

using namespace opentrail::persistence;
using namespace opentrail::persistence::test_support;

constexpr std::uint32_t kGroupEpoch = 7;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

CounterDomainId domain(std::uint8_t seed = 1) {
    CounterDomainId result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::uint8_t>(seed + index);
    }
    return result;
}

OutboundCounterLeaseRequest request(
    CounterDomainId domain_id = domain(),
    std::uint32_t group_epoch = kGroupEpoch,
    std::uint32_t lease_size = 4) {
    return {domain_id, group_epoch, lease_size};
}

void write_u16(std::uint8_t* output, std::uint16_t value) {
    output[0] = static_cast<std::uint8_t>(value & 0xFFU);
    output[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

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

std::array<std::uint8_t, kPersistentSlotBytes> record(
    const CounterDomainId& domain_id,
    std::uint32_t group_epoch,
    std::uint64_t reserved_through,
    std::uint32_t generation,
    std::uint8_t envelope_version = kOutboundCounterEnvelopeVersion,
    std::uint8_t schema_version = kOutboundCounterSchemaVersion) {
    std::array<std::uint8_t, kPersistentSlotBytes> bytes{};
    bytes[0] = 'O';
    bytes[1] = 'T';
    bytes[2] = 'C';
    bytes[3] = 'N';
    bytes[4] = envelope_version;
    bytes[5] = schema_version;
    bytes[7] = 16;
    write_u32(bytes.data() + 8, generation);
    write_u16(bytes.data() + 12, 28);
    for (std::size_t index = 0; index < domain_id.size(); ++index) {
        bytes[16 + index] = domain_id[index];
    }
    write_u32(bytes.data() + 32, group_epoch);
    write_u64(bytes.data() + 36, reserved_through);
    write_u32(bytes.data() + 56, crc32(bytes.data(), 56));
    write_u32(bytes.data() + kOutboundCounterCommitOffset,
              kOutboundCounterCommitMarker);
    return bytes;
}

void test_first_reservation_is_durable_and_domain_isolated() {
    MemoryPersistentStorage storage{};
    OutboundCounterLeaseStore store{storage};
    const auto lease = store.reserve(request());
    EXPECT(lease.reserved());
    EXPECT(lease.initialized);
    EXPECT(lease.first_counter == 1);
    EXPECT(lease.last_counter == 4);
    EXPECT(lease.generation == 1);
    EXPECT(lease.written_slot == 0);

    const auto& bytes = storage.slot_bytes(
        StorageDomain::outbound_counter_state, 0);
    EXPECT(bytes[0] == 'O' && bytes[1] == 'T' &&
           bytes[2] == 'C' && bytes[3] == 'N');
    EXPECT(bytes[4] == 1 && bytes[5] == 1 && bytes[7] == 16);
    EXPECT(bytes[8] == 1 && bytes[12] == 28);
    EXPECT(bytes[16] == 1 && bytes[31] == 16);
    EXPECT(bytes[32] == kGroupEpoch);
    EXPECT(bytes[36] == 4);

    const auto own = storage.counters(StorageDomain::outbound_counter_state);
    EXPECT(own.erases == 1 && own.writes == 2 && own.syncs == 2);
    EXPECT(storage.counters(StorageDomain::protocol_state).writes == 0);
    EXPECT(storage.counters(StorageDomain::secret_material).writes == 0);
}

void test_subsequent_leases_are_nonoverlapping_and_alternate_slots() {
    MemoryPersistentStorage storage{};
    OutboundCounterLeaseStore store{storage};
    const auto first = store.reserve(request());
    const auto second = store.reserve(request());
    const auto third = store.reserve(request(domain(), kGroupEpoch, 2));
    EXPECT(first.reserved() && second.reserved() && third.reserved());
    EXPECT(first.first_counter == 1 && first.last_counter == 4);
    EXPECT(second.first_counter == 5 && second.last_counter == 8);
    EXPECT(second.generation == 2 && second.written_slot == 1);
    EXPECT(third.first_counter == 9 && third.last_counter == 10);
    EXPECT(third.generation == 3 && third.written_slot == 0);
}

void test_allocator_reserves_before_serving_and_extends() {
    MemoryPersistentStorage storage{};
    OutboundCounterLeaseStore store{storage};
    OutboundCounterAllocator allocator{store};
    EXPECT(allocator.next().error == OutboundCounterError::not_started);
    EXPECT(allocator.start(request(domain(), kGroupEpoch, 2)) ==
           OutboundCounterError::none);
    EXPECT(allocator.start(request()) == OutboundCounterError::already_started);
    const auto first = allocator.next();
    const auto second = allocator.next();
    const auto third = allocator.next();
    EXPECT(first.allocated() && first.counter == 1);
    EXPECT(second.allocated() && second.counter == 2);
    EXPECT(third.allocated() && third.counter == 3);
    EXPECT(storage.counters(StorageDomain::outbound_counter_state).erases == 2);
}

void test_restart_discards_unused_lease_without_reuse() {
    MemoryPersistentStorage storage{};
    OutboundCounterLeaseStore store{storage};
    OutboundCounterAllocator first_boot{store};
    EXPECT(first_boot.start(request(domain(), kGroupEpoch, 8)) ==
           OutboundCounterError::none);
    EXPECT(first_boot.next().counter == 1);

    OutboundCounterAllocator second_boot{store};
    EXPECT(second_boot.start(request(domain(), kGroupEpoch, 8)) ==
           OutboundCounterError::none);
    EXPECT(second_boot.next().counter == 9);
}

void test_domain_or_epoch_change_fails_without_writes() {
    MemoryPersistentStorage storage{};
    OutboundCounterLeaseStore store{storage};
    EXPECT(store.reserve(request()).reserved());
    const auto before = storage.counters(StorageDomain::outbound_counter_state);
    EXPECT(store.reserve(request(domain(2))).error ==
           OutboundCounterError::domain_mismatch);
    EXPECT(store.reserve(request(domain(), kGroupEpoch + 1)).error ==
           OutboundCounterError::domain_mismatch);
    const auto after = storage.counters(StorageDomain::outbound_counter_state);
    EXPECT(after.erases == before.erases && after.writes == before.writes);
}

void test_corruption_conflict_and_future_version_fail_closed() {
    MemoryPersistentStorage corrupted_storage{};
    OutboundCounterLeaseStore corrupted{corrupted_storage};
    EXPECT(corrupted.reserve(request()).reserved());
    EXPECT(corrupted.reserve(request()).reserved());
    corrupted_storage.corrupt_byte(
        StorageDomain::outbound_counter_state, 1, 36, 0x01);
    EXPECT(corrupted.reserve(request()).error ==
           OutboundCounterError::integrity_failure);

    MemoryPersistentStorage conflict_storage{};
    conflict_storage.seed_slot(
        StorageDomain::outbound_counter_state,
        0,
        record(domain(), kGroupEpoch, 8, 4));
    conflict_storage.seed_slot(
        StorageDomain::outbound_counter_state,
        1,
        record(domain(), kGroupEpoch, 12, 4));
    OutboundCounterLeaseStore conflict{conflict_storage};
    EXPECT(conflict.reserve(request()).error ==
           OutboundCounterError::generation_conflict);

    MemoryPersistentStorage future_storage{};
    future_storage.seed_slot(
        StorageDomain::outbound_counter_state,
        0,
        record(domain(), kGroupEpoch, 4, 1, 2, 1));
    OutboundCounterLeaseStore future{future_storage};
    EXPECT(future.reserve(request()).error ==
           OutboundCounterError::unsupported_version);
}

void test_invalid_request_and_exhaustion_are_explicit() {
    MemoryPersistentStorage invalid_storage{};
    OutboundCounterLeaseStore invalid{invalid_storage};
    CounterDomainId zero{};
    EXPECT(invalid.reserve(request(zero)).error ==
           OutboundCounterError::invalid_request);
    EXPECT(invalid.reserve(request(domain(), 0)).error ==
           OutboundCounterError::invalid_request);
    EXPECT(invalid.reserve(request(domain(), kGroupEpoch, 0)).error ==
           OutboundCounterError::invalid_request);
    EXPECT(invalid.reserve(request(
               domain(), kGroupEpoch, kMaximumOutboundCounterLeaseSize + 1))
               .error == OutboundCounterError::invalid_request);
    EXPECT(invalid_storage.counters(StorageDomain::outbound_counter_state).writes == 0);

    MemoryPersistentStorage generation_storage{};
    generation_storage.seed_slot(
        StorageDomain::outbound_counter_state,
        0,
        record(
            domain(),
            kGroupEpoch,
            4,
            std::numeric_limits<std::uint32_t>::max()));
    OutboundCounterLeaseStore generation{generation_storage};
    EXPECT(generation.reserve(request()).error ==
           OutboundCounterError::generation_exhausted);

    MemoryPersistentStorage counter_storage{};
    counter_storage.seed_slot(
        StorageDomain::outbound_counter_state,
        0,
        record(
            domain(),
            kGroupEpoch,
            std::numeric_limits<std::uint64_t>::max() - 1,
            1));
    OutboundCounterLeaseStore counter{counter_storage};
    EXPECT(counter.reserve(request(domain(), kGroupEpoch, 2)).error ==
           OutboundCounterError::counter_exhausted);
}

void test_read_failure_returns_no_lease_and_makes_no_writes() {
    MemoryPersistentStorage storage{};
    storage.fail_next_read();
    OutboundCounterLeaseStore store{storage};
    const auto lease = store.reserve(request());
    EXPECT(lease.error == OutboundCounterError::storage_failure);
    EXPECT(lease.first_counter == 0 && lease.last_counter == 0);
    EXPECT(!lease.initialized && lease.generation == 0);
    EXPECT(lease.written_slot == kPersistentSlotCount);
    EXPECT(storage.counters(StorageDomain::outbound_counter_state).writes == 0);

    MemoryPersistentStorage write_storage{};
    write_storage.arm_power_loss_after(0);
    OutboundCounterLeaseStore write_store{write_storage};
    const auto write_failure = write_store.reserve(request());
    EXPECT(write_failure.error == OutboundCounterError::storage_failure);
    EXPECT(write_failure.first_counter == 0 &&
           write_failure.last_counter == 0);
    EXPECT(!write_failure.initialized && write_failure.generation == 0);
    EXPECT(write_failure.written_slot == kPersistentSlotCount);
}

void test_power_loss_never_returns_an_uncertain_lease() {
    for (std::size_t successful_mutations = 0;
         successful_mutations <= 5;
         ++successful_mutations) {
        MemoryPersistentStorage storage{};
        OutboundCounterLeaseStore store{storage};
        EXPECT(store.reserve(request()).reserved());
        storage.arm_power_loss_after(successful_mutations);
        const auto interrupted = store.reserve(request());
        storage.clear_fault();
        if (successful_mutations == 5) {
            EXPECT(interrupted.reserved());
            EXPECT(interrupted.first_counter == 5);
            continue;
        }
        EXPECT(interrupted.error == OutboundCounterError::storage_failure);
        EXPECT(interrupted.first_counter == 0 && interrupted.last_counter == 0);
        const auto retry = store.reserve(request());
        if (successful_mutations <= 1) {
            EXPECT(retry.reserved() && retry.first_counter == 5);
        } else if (successful_mutations <= 3) {
            EXPECT(retry.error == OutboundCounterError::integrity_failure);
        } else {
            EXPECT(retry.reserved() && retry.first_counter == 9);
        }
    }
}

void test_uncommitted_and_malformed_records_require_recovery() {
    MemoryPersistentStorage uncommitted_storage{};
    auto uncommitted = record(domain(), kGroupEpoch, 4, 1);
    write_u32(uncommitted.data() + kOutboundCounterCommitOffset, 0xFFFFFFFFU);
    uncommitted_storage.seed_slot(
        StorageDomain::outbound_counter_state, 0, uncommitted);
    OutboundCounterLeaseStore uncommitted_store{uncommitted_storage};
    EXPECT(uncommitted_store.reserve(request()).error ==
           OutboundCounterError::integrity_failure);

    MemoryPersistentStorage malformed_storage{};
    auto malformed = record(domain(), kGroupEpoch, 4, 1);
    malformed[7] = 17;
    write_u32(malformed.data() + 56, crc32(malformed.data(), 56));
    malformed_storage.seed_slot(
        StorageDomain::outbound_counter_state, 0, malformed);
    OutboundCounterLeaseStore malformed_store{malformed_storage};
    EXPECT(malformed_store.reserve(request()).error ==
           OutboundCounterError::integrity_failure);
}

}  // namespace

int main() {
    test_first_reservation_is_durable_and_domain_isolated();
    test_subsequent_leases_are_nonoverlapping_and_alternate_slots();
    test_allocator_reserves_before_serving_and_extends();
    test_restart_discards_unused_lease_without_reuse();
    test_domain_or_epoch_change_fails_without_writes();
    test_corruption_conflict_and_future_version_fail_closed();
    test_invalid_request_and_exhaustion_are_explicit();
    test_read_failure_returns_no_lease_and_makes_no_writes();
    test_power_loss_never_returns_an_uncertain_lease();
    test_uncommitted_and_malformed_records_require_recovery();

    if (failures != 0) {
        std::cerr << failures
                  << " outbound counter lease assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 outbound counter lease scenario groups\n";
    return EXIT_SUCCESS;
}
