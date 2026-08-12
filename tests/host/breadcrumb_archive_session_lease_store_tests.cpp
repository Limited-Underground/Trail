#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

#include "memory_persistent_storage.hpp"
#include "opentrail/breadcrumb_archive_session_lease_store.hpp"

namespace {

using namespace opentrail::persistence;
using namespace opentrail::persistence::test_support;

constexpr std::uint64_t kInitialSession = 0x0102030405060708ULL;
constexpr std::uint32_t kLeaseSize = 4;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

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
    std::uint64_t range_start,
    std::uint64_t range_end,
    std::uint32_t generation,
    std::uint8_t envelope_version =
        kBreadcrumbArchiveSessionLeaseEnvelopeVersion,
    std::uint8_t schema_version =
        kBreadcrumbArchiveSessionLeaseSchemaVersion) {
    std::array<std::uint8_t, kPersistentSlotBytes> bytes{};
    bytes[0] = 'O';
    bytes[1] = 'T';
    bytes[2] = 'B';
    bytes[3] = 'L';
    bytes[4] = envelope_version;
    bytes[5] = schema_version;
    bytes[7] = 16;
    write_u32(bytes.data() + 8, generation);
    write_u16(bytes.data() + 12, 16);
    write_u64(bytes.data() + 16, range_start);
    write_u64(bytes.data() + 24, range_end);
    write_u32(bytes.data() + 56, crc32(bytes.data(), 56));
    write_u32(
        bytes.data() + kBreadcrumbArchiveSessionLeaseCommitOffset,
        kBreadcrumbArchiveSessionLeaseCommitMarker);
    return bytes;
}

BreadcrumbArchiveSessionLeaseRequest request(
    std::uint64_t initial_session_id = kInitialSession,
    std::uint32_t lease_size = kLeaseSize) {
    return {initial_session_id, lease_size};
}

void test_first_lease_is_commit_last_and_domain_isolated() {
    MemoryPersistentStorage storage{};
    BreadcrumbArchiveSessionLeaseStore store{storage};
    const auto allocated = store.allocate(request());
    EXPECT(allocated.allocated());
    EXPECT(allocated.initialized);
    EXPECT(allocated.generation == 1);
    EXPECT(allocated.first_session_id == kInitialSession);
    EXPECT(allocated.final_session_id == kInitialSession + 3);
    EXPECT(allocated.written_slot == 0);
    const auto& bytes = storage.slot_bytes(
        StorageDomain::breadcrumb_archive_state, 0);
    EXPECT(bytes[0] == 'O' && bytes[1] == 'T' &&
           bytes[2] == 'B' && bytes[3] == 'L');
    EXPECT(storage.counters(
               StorageDomain::breadcrumb_archive_state).erases == 1);
    EXPECT(storage.counters(StorageDomain::protocol_state).writes == 0);
    EXPECT(storage.counters(
               StorageDomain::outbound_counter_state).writes == 0);
}

void test_restarts_abandon_unused_ids_and_rotate_slots() {
    MemoryPersistentStorage storage{};
    BreadcrumbArchiveSessionLeaseStore first_boot{storage};
    const auto first = first_boot.allocate(request());
    BreadcrumbArchiveSessionLeaseStore second_boot{storage};
    const auto second = second_boot.allocate(request(
        std::numeric_limits<std::uint64_t>::max(), 2));
    BreadcrumbArchiveSessionLeaseStore third_boot{storage};
    const auto third = third_boot.allocate(request());
    EXPECT(first.allocated() && second.allocated() && third.allocated());
    EXPECT(second.first_session_id == first.final_session_id + 1);
    EXPECT(second.final_session_id == second.first_session_id + 1);
    EXPECT(second.generation == 2 && second.written_slot == 1);
    EXPECT(third.first_session_id == second.final_session_id + 1);
    EXPECT(third.final_session_id == third.first_session_id + 3);
    EXPECT(third.generation == 3 && third.written_slot == 0);
}

void test_failed_first_commit_does_not_claim_initialization() {
    MemoryPersistentStorage storage{};
    BreadcrumbArchiveSessionLeaseStore store{storage};
    storage.arm_power_loss_after(0);
    const auto failed = store.allocate(request());
    EXPECT(!failed.allocated());
    EXPECT(!failed.initialized);
    EXPECT(failed.first_session_id == 0 && failed.final_session_id == 0);
}

void test_corrupt_peer_fails_closed_without_allocation() {
    MemoryPersistentStorage storage{};
    BreadcrumbArchiveSessionLeaseStore store{storage};
    EXPECT(store.allocate(request()).allocated());
    EXPECT(store.allocate(request()).allocated());
    storage.corrupt_byte(
        StorageDomain::breadcrumb_archive_state, 1, 20, 0x01);
    const auto before = storage.counters(
        StorageDomain::breadcrumb_archive_state);
    const auto failed = store.allocate(request());
    EXPECT(failed.error ==
           BreadcrumbArchiveSessionLeaseError::integrity_failure);
    EXPECT(failed.slot_states[1] ==
           BreadcrumbArchiveSessionLeaseSlotState::integrity_failure);
    const auto after = storage.counters(
        StorageDomain::breadcrumb_archive_state);
    EXPECT(after.writes == before.writes && after.erases == before.erases);
}

void test_conflict_and_future_version_fail_closed() {
    MemoryPersistentStorage conflict_storage{};
    conflict_storage.seed_slot(
        StorageDomain::breadcrumb_archive_state,
        0,
        record(10, 19, 4));
    conflict_storage.seed_slot(
        StorageDomain::breadcrumb_archive_state,
        1,
        record(20, 29, 4));
    BreadcrumbArchiveSessionLeaseStore conflict{conflict_storage};
    EXPECT(conflict.allocate(request()).error ==
           BreadcrumbArchiveSessionLeaseError::generation_conflict);

    MemoryPersistentStorage future_storage{};
    future_storage.seed_slot(
        StorageDomain::breadcrumb_archive_state,
        0,
        record(10, 19, 4, 2, 1));
    BreadcrumbArchiveSessionLeaseStore future{future_storage};
    EXPECT(future.allocate(request()).error ==
           BreadcrumbArchiveSessionLeaseError::unsupported_version);
}

void test_generation_and_session_exhaustion_are_explicit() {
    MemoryPersistentStorage generation_storage{};
    generation_storage.seed_slot(
        StorageDomain::breadcrumb_archive_state,
        0,
        record(
            10,
            19,
            std::numeric_limits<std::uint32_t>::max()));
    BreadcrumbArchiveSessionLeaseStore generation{generation_storage};
    EXPECT(generation.allocate(request()).error ==
           BreadcrumbArchiveSessionLeaseError::generation_exhausted);

    MemoryPersistentStorage session_storage{};
    session_storage.seed_slot(
        StorageDomain::breadcrumb_archive_state,
        0,
        record(
            std::numeric_limits<std::uint64_t>::max() - 1,
            std::numeric_limits<std::uint64_t>::max(),
            1));
    BreadcrumbArchiveSessionLeaseStore session{session_storage};
    EXPECT(session.allocate(request()).error ==
           BreadcrumbArchiveSessionLeaseError::session_exhausted);
    EXPECT(session.allocate(request(
               std::numeric_limits<std::uint64_t>::max(), 2)).error ==
           BreadcrumbArchiveSessionLeaseError::session_exhausted);
}

void test_invalid_request_and_read_failure_make_no_writes() {
    MemoryPersistentStorage storage{};
    BreadcrumbArchiveSessionLeaseStore store{storage};
    EXPECT(store.allocate(request(0)).error ==
           BreadcrumbArchiveSessionLeaseError::invalid_request);
    EXPECT(store.allocate(request(kInitialSession, 0)).error ==
           BreadcrumbArchiveSessionLeaseError::invalid_request);
    storage.fail_next_read();
    EXPECT(store.allocate(request()).error ==
           BreadcrumbArchiveSessionLeaseError::storage_failure);
    EXPECT(storage.counters(
               StorageDomain::breadcrumb_archive_state).writes == 0);
}

void test_power_loss_never_returns_or_reuses_uncertain_range() {
    for (std::size_t successful_mutations = 0;
         successful_mutations <= 5;
         ++successful_mutations) {
        MemoryPersistentStorage storage{};
        BreadcrumbArchiveSessionLeaseStore store{storage};
        const auto first = store.allocate(request());
        EXPECT(first.allocated());
        storage.arm_power_loss_after(successful_mutations);
        const auto interrupted = store.allocate(request());
        storage.clear_fault();
        if (successful_mutations == 5) {
            EXPECT(interrupted.allocated());
            EXPECT(interrupted.first_session_id == first.final_session_id + 1);
            continue;
        }
        EXPECT(interrupted.error ==
               BreadcrumbArchiveSessionLeaseError::storage_failure);
        const auto retry = store.allocate(request());
        if (successful_mutations <= 1) {
            EXPECT(retry.allocated());
            EXPECT(retry.first_session_id == first.final_session_id + 1);
        } else if (successful_mutations <= 3) {
            EXPECT(retry.error ==
                   BreadcrumbArchiveSessionLeaseError::integrity_failure);
        } else {
            EXPECT(retry.allocated());
            EXPECT(retry.first_session_id ==
                   first.final_session_id + kLeaseSize + 1);
        }
    }
}

void test_uncommitted_and_malformed_records_require_recovery() {
    MemoryPersistentStorage uncommitted_storage{};
    auto uncommitted = record(10, 19, 1);
    write_u32(
        uncommitted.data() + kBreadcrumbArchiveSessionLeaseCommitOffset,
        0xFFFFFFFFU);
    uncommitted_storage.seed_slot(
        StorageDomain::breadcrumb_archive_state, 0, uncommitted);
    BreadcrumbArchiveSessionLeaseStore uncommitted_store{
        uncommitted_storage};
    EXPECT(uncommitted_store.allocate(request()).error ==
           BreadcrumbArchiveSessionLeaseError::integrity_failure);

    MemoryPersistentStorage malformed_storage{};
    auto malformed = record(20, 10, 1);
    malformed_storage.seed_slot(
        StorageDomain::breadcrumb_archive_state, 0, malformed);
    BreadcrumbArchiveSessionLeaseStore malformed_store{malformed_storage};
    EXPECT(malformed_store.allocate(request()).error ==
           BreadcrumbArchiveSessionLeaseError::integrity_failure);
}

static_assert(std::is_trivially_copyable_v<
              BreadcrumbArchiveSessionLeaseAllocation>);
static_assert(sizeof(BreadcrumbArchiveSessionLeaseAllocation) <= 64);

}  // namespace

int main() {
    test_first_lease_is_commit_last_and_domain_isolated();
    test_restarts_abandon_unused_ids_and_rotate_slots();
    test_failed_first_commit_does_not_claim_initialization();
    test_corrupt_peer_fails_closed_without_allocation();
    test_conflict_and_future_version_fail_closed();
    test_generation_and_session_exhaustion_are_explicit();
    test_invalid_request_and_read_failure_make_no_writes();
    test_power_loss_never_returns_or_reuses_uncertain_range();
    test_uncommitted_and_malformed_records_require_recovery();

    if (failures != 0) {
        std::cerr << failures
                  << " breadcrumb archive session lease assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 9 archive session lease scenario groups\n";
    return EXIT_SUCCESS;
}
