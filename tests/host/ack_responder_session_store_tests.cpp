#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "memory_persistent_storage.hpp"
#include "opentrail/ack_responder_session_store.hpp"
#include "opentrail/critical_alert_ack_responder.hpp"

namespace {

using namespace opentrail::integration;
using namespace opentrail::persistence;
using namespace opentrail::persistence::test_support;

constexpr std::uint64_t kConsumerId = 0x0102030405060708ULL;
constexpr std::uint32_t kAuthorizationEpoch = 7;
constexpr std::uint32_t kInitialBootSession = 0x11223344U;

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
    std::uint64_t consumer_id,
    std::uint32_t authorization_epoch,
    std::uint32_t boot_session_id,
    std::uint32_t generation,
    std::uint8_t envelope_version = kAckResponderSessionEnvelopeVersion,
    std::uint8_t schema_version = kAckResponderSessionSchemaVersion) {
    std::array<std::uint8_t, kPersistentSlotBytes> bytes{};
    bytes[0] = 'O';
    bytes[1] = 'T';
    bytes[2] = 'A';
    bytes[3] = 'S';
    bytes[4] = envelope_version;
    bytes[5] = schema_version;
    bytes[7] = 16;
    write_u32(bytes.data() + 8, generation);
    write_u16(bytes.data() + 12, 16);
    write_u64(bytes.data() + 16, consumer_id);
    write_u32(bytes.data() + 24, authorization_epoch);
    write_u32(bytes.data() + 28, boot_session_id);
    write_u32(bytes.data() + 56, crc32(bytes.data(), 56));
    write_u32(bytes.data() + kAckResponderSessionCommitOffset,
              kAckResponderSessionCommitMarker);
    return bytes;
}

AckResponderSessionRequest request(
    std::uint64_t consumer_id = kConsumerId,
    std::uint32_t authorization_epoch = kAuthorizationEpoch,
    std::uint32_t initial_boot_session_id = kInitialBootSession) {
    return {consumer_id, authorization_epoch, initial_boot_session_id};
}

void test_first_allocation_is_commit_last_and_composes_with_responder() {
    MemoryPersistentStorage storage{};
    AckResponderSessionStore store{storage};
    const auto allocated = store.allocate(request());
    EXPECT(allocated.allocated());
    EXPECT(allocated.initialized);
    EXPECT(allocated.generation == 1);
    EXPECT(allocated.boot_session_id == kInitialBootSession);
    EXPECT(allocated.written_slot == 0);
    const auto& bytes = storage.slot_bytes(StorageDomain::protocol_state, 0);
    EXPECT(bytes[0] == 'O' && bytes[1] == 'T' &&
           bytes[2] == 'A' && bytes[3] == 'S');
    EXPECT(bytes[4] == 1 && bytes[5] == 1 && bytes[7] == 16);
    EXPECT(bytes[8] == 1 && bytes[12] == 16);
    EXPECT(bytes[16] == 0x08 && bytes[23] == 0x01);
    EXPECT(storage.counters(StorageDomain::protocol_state).erases == 1);
    EXPECT(storage.counters(StorageDomain::configuration).writes == 0);
    EXPECT(storage.counters(StorageDomain::secret_material).writes == 0);

    CriticalAlertAckResponder responder{};
    EXPECT(responder.start(
               {kConsumerId, allocated.boot_session_id, 0}) ==
           CriticalAlertAckResponseError::none);
    EXPECT(responder.status().next_ack_sequence == 0);
}

void test_subsequent_boots_increment_and_alternate_slots() {
    MemoryPersistentStorage storage{};
    AckResponderSessionStore store{storage};
    const auto first = store.allocate(request());
    const auto second = store.allocate(request(kConsumerId,
                                                kAuthorizationEpoch,
                                                99));
    const auto third = store.allocate(request());
    EXPECT(first.allocated() && second.allocated() && third.allocated());
    EXPECT(second.generation == 2);
    EXPECT(second.boot_session_id == kInitialBootSession + 1);
    EXPECT(second.written_slot == 1);
    EXPECT(!second.initialized);
    EXPECT(third.generation == 3);
    EXPECT(third.boot_session_id == kInitialBootSession + 2);
    EXPECT(third.written_slot == 0);
}

void test_identity_epoch_change_requires_explicit_reset() {
    MemoryPersistentStorage storage{};
    AckResponderSessionStore store{storage};
    EXPECT(store.allocate(request()).allocated());
    const auto before = storage.counters(StorageDomain::protocol_state);
    EXPECT(store.allocate(request(kConsumerId + 1)).error ==
           AckResponderSessionError::identity_mismatch);
    EXPECT(store.allocate(request(kConsumerId, kAuthorizationEpoch + 1)).error ==
           AckResponderSessionError::identity_mismatch);
    const auto after = storage.counters(StorageDomain::protocol_state);
    EXPECT(after.erases == before.erases && after.writes == before.writes);
    EXPECT(store.reset() == AckResponderSessionError::none);
    const auto rebound = store.allocate(request(
        kConsumerId, kAuthorizationEpoch + 1, 0x55667788U));
    EXPECT(rebound.allocated());
    EXPECT(rebound.initialized);
    EXPECT(rebound.boot_session_id == 0x55667788U);
}

void test_corrupt_peer_slot_fails_closed_without_reuse() {
    MemoryPersistentStorage storage{};
    AckResponderSessionStore store{storage};
    EXPECT(store.allocate(request()).allocated());
    EXPECT(store.allocate(request()).allocated());
    storage.corrupt_byte(StorageDomain::protocol_state, 1, 20, 0x01);
    const auto before = storage.counters(StorageDomain::protocol_state);
    const auto result = store.allocate(request());
    EXPECT(result.error == AckResponderSessionError::integrity_failure);
    EXPECT(result.slot_states[1] ==
           AckResponderSessionSlotState::integrity_failure);
    const auto after = storage.counters(StorageDomain::protocol_state);
    EXPECT(after.erases == before.erases && after.writes == before.writes);
}

void test_equal_generation_conflict_and_future_version_fail_closed() {
    MemoryPersistentStorage conflict_storage{};
    conflict_storage.seed_slot(
        StorageDomain::protocol_state,
        0,
        record(kConsumerId, kAuthorizationEpoch, 10, 9));
    conflict_storage.seed_slot(
        StorageDomain::protocol_state,
        1,
        record(kConsumerId, kAuthorizationEpoch, 11, 9));
    AckResponderSessionStore conflict{conflict_storage};
    EXPECT(conflict.allocate(request()).error ==
           AckResponderSessionError::generation_conflict);

    MemoryPersistentStorage future_storage{};
    future_storage.seed_slot(
        StorageDomain::protocol_state,
        0,
        record(kConsumerId, kAuthorizationEpoch, 10, 9, 2, 1));
    AckResponderSessionStore future{future_storage};
    EXPECT(future.allocate(request()).error ==
           AckResponderSessionError::unsupported_version);
}

void test_generation_and_boot_session_exhaustion_are_explicit() {
    MemoryPersistentStorage generation_storage{};
    generation_storage.seed_slot(
        StorageDomain::protocol_state,
        0,
        record(
            kConsumerId,
            kAuthorizationEpoch,
            10,
            std::numeric_limits<std::uint32_t>::max()));
    AckResponderSessionStore generation{generation_storage};
    EXPECT(generation.allocate(request()).error ==
           AckResponderSessionError::generation_exhausted);

    MemoryPersistentStorage session_storage{};
    session_storage.seed_slot(
        StorageDomain::protocol_state,
        0,
        record(
            kConsumerId,
            kAuthorizationEpoch,
            std::numeric_limits<std::uint32_t>::max(),
            1));
    AckResponderSessionStore session{session_storage};
    EXPECT(session.allocate(request()).error ==
           AckResponderSessionError::boot_session_exhausted);
}

void test_read_failure_and_invalid_requests_make_no_writes() {
    MemoryPersistentStorage storage{};
    AckResponderSessionStore store{storage};
    EXPECT(store.allocate(request(0)).error ==
           AckResponderSessionError::invalid_request);
    EXPECT(store.allocate(request(kConsumerId, 0)).error ==
           AckResponderSessionError::invalid_request);
    EXPECT(store.allocate(request(kConsumerId, kAuthorizationEpoch, 0)).error ==
           AckResponderSessionError::invalid_request);
    storage.fail_next_read();
    EXPECT(store.allocate(request()).error ==
           AckResponderSessionError::storage_failure);
    EXPECT(storage.counters(StorageDomain::protocol_state).writes == 0);
}

void test_power_loss_never_returns_or_silently_reuses_uncertain_state() {
    for (std::size_t successful_mutations = 0;
         successful_mutations <= 5;
         ++successful_mutations) {
        MemoryPersistentStorage storage{};
        AckResponderSessionStore store{storage};
        EXPECT(store.allocate(request()).allocated());
        storage.arm_power_loss_after(successful_mutations);
        const auto interrupted = store.allocate(request());
        storage.clear_fault();
        if (successful_mutations == 5) {
            EXPECT(interrupted.allocated());
            EXPECT(interrupted.boot_session_id == kInitialBootSession + 1);
            continue;
        }
        EXPECT(interrupted.error == AckResponderSessionError::storage_failure);
        const auto retry = store.allocate(request());
        if (successful_mutations <= 1) {
            EXPECT(retry.allocated());
            EXPECT(retry.boot_session_id == kInitialBootSession + 1);
        } else if (successful_mutations <= 3) {
            EXPECT(retry.error == AckResponderSessionError::integrity_failure);
        } else {
            EXPECT(retry.allocated());
            EXPECT(retry.boot_session_id == kInitialBootSession + 2);
        }
    }
}

void test_uncommitted_and_malformed_records_require_recovery() {
    MemoryPersistentStorage uncommitted_storage{};
    auto uncommitted = record(kConsumerId, kAuthorizationEpoch, 10, 1);
    write_u32(uncommitted.data() + kAckResponderSessionCommitOffset, 0xFFFFFFFFU);
    uncommitted_storage.seed_slot(
        StorageDomain::protocol_state, 0, uncommitted);
    AckResponderSessionStore uncommitted_store{uncommitted_storage};
    EXPECT(uncommitted_store.allocate(request()).error ==
           AckResponderSessionError::integrity_failure);

    MemoryPersistentStorage malformed_storage{};
    auto malformed = record(kConsumerId, kAuthorizationEpoch, 10, 1);
    malformed[7] = 17;
    write_u32(malformed.data() + 56, crc32(malformed.data(), 56));
    malformed_storage.seed_slot(StorageDomain::protocol_state, 0, malformed);
    AckResponderSessionStore malformed_store{malformed_storage};
    EXPECT(malformed_store.allocate(request()).error ==
           AckResponderSessionError::integrity_failure);
}

void test_reset_attempts_both_slots_on_failure() {
    MemoryPersistentStorage storage{};
    AckResponderSessionStore store{storage};
    EXPECT(store.allocate(request()).allocated());
    EXPECT(store.allocate(request()).allocated());
    storage.arm_power_loss_after(0);
    EXPECT(store.reset() == AckResponderSessionError::storage_failure);
    storage.clear_fault();
    EXPECT(storage.counters(StorageDomain::protocol_state).erases == 4);
}

}  // namespace

int main() {
    test_first_allocation_is_commit_last_and_composes_with_responder();
    test_subsequent_boots_increment_and_alternate_slots();
    test_identity_epoch_change_requires_explicit_reset();
    test_corrupt_peer_slot_fails_closed_without_reuse();
    test_equal_generation_conflict_and_future_version_fail_closed();
    test_generation_and_boot_session_exhaustion_are_explicit();
    test_read_failure_and_invalid_requests_make_no_writes();
    test_power_loss_never_returns_or_silently_reuses_uncertain_state();
    test_uncommitted_and_malformed_records_require_recovery();
    test_reset_attempts_both_slots_on_failure();

    if (failures != 0) {
        std::cerr << failures
                  << " ACK responder session store assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 ACK responder session store scenario groups\n";
    return EXIT_SUCCESS;
}
