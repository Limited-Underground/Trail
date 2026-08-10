#include "opentrail/outbound_counter_lease_store.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace opentrail::persistence {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'T', 'C', 'N'}};
constexpr std::size_t kHeaderBytes = 16;
constexpr std::size_t kPayloadBytes = 28;
constexpr std::size_t kCrcOffset = 56;

struct CounterRecord {
    CounterDomainId domain_id{};
    std::uint32_t group_epoch{0};
    std::uint64_t reserved_through{0};
    std::uint32_t generation{0};
};

struct InspectedSlot {
    OutboundCounterSlotState state{OutboundCounterSlotState::blank};
    CounterRecord record{};
    std::array<std::uint8_t, kPersistentSlotBytes> bytes{};
};

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

std::uint16_t read_u16(const std::uint8_t* input) {
    return static_cast<std::uint16_t>(input[0]) |
           (static_cast<std::uint16_t>(input[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* input) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(input[index]) << (index * 8U);
    }
    return value;
}

std::uint64_t read_u64(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(input[index]) << (index * 8U);
    }
    return value;
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

bool all_value(
    const std::uint8_t* data,
    std::size_t size,
    std::uint8_t expected) {
    return std::all_of(data, data + size, [expected](std::uint8_t byte) {
        return byte == expected;
    });
}

bool all_zero(const CounterDomainId& domain_id) {
    return std::all_of(domain_id.begin(), domain_id.end(), [](std::uint8_t byte) {
        return byte == 0;
    });
}

std::array<std::uint8_t, kPersistentSlotBytes> encode_record(
    const CounterRecord& record) {
    std::array<std::uint8_t, kPersistentSlotBytes> bytes{};
    std::copy(kMagic.begin(), kMagic.end(), bytes.begin());
    bytes[4] = kOutboundCounterEnvelopeVersion;
    bytes[5] = kOutboundCounterSchemaVersion;
    bytes[7] = static_cast<std::uint8_t>(kHeaderBytes);
    write_u32(bytes.data() + 8, record.generation);
    write_u16(bytes.data() + 12, static_cast<std::uint16_t>(kPayloadBytes));
    std::copy(record.domain_id.begin(), record.domain_id.end(), bytes.begin() + 16);
    write_u32(bytes.data() + 32, record.group_epoch);
    write_u64(bytes.data() + 36, record.reserved_through);
    write_u32(bytes.data() + kCrcOffset, crc32(bytes.data(), kCrcOffset));
    write_u32(bytes.data() + kOutboundCounterCommitOffset,
              kOutboundCounterCommitMarker);
    return bytes;
}

InspectedSlot decode_slot(
    const std::array<std::uint8_t, kPersistentSlotBytes>& bytes) {
    InspectedSlot result{};
    result.bytes = bytes;
    if (all_value(bytes.data(), bytes.size(), 0xFFU)) {
        return result;
    }
    if (read_u32(bytes.data() + kOutboundCounterCommitOffset) !=
        kOutboundCounterCommitMarker) {
        result.state = OutboundCounterSlotState::uncommitted;
        return result;
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin()) ||
        bytes[6] != 0 || bytes[7] != kHeaderBytes ||
        read_u16(bytes.data() + 12) != kPayloadBytes ||
        bytes[14] != 0 || bytes[15] != 0 ||
        !all_value(bytes.data() + 44, 12, 0)) {
        result.state = OutboundCounterSlotState::malformed;
        return result;
    }
    if (bytes[4] != kOutboundCounterEnvelopeVersion ||
        bytes[5] != kOutboundCounterSchemaVersion) {
        result.state = OutboundCounterSlotState::unsupported_version;
        return result;
    }
    if (read_u32(bytes.data() + kCrcOffset) !=
        crc32(bytes.data(), kCrcOffset)) {
        result.state = OutboundCounterSlotState::integrity_failure;
        return result;
    }
    std::copy(bytes.begin() + 16, bytes.begin() + 32,
              result.record.domain_id.begin());
    result.record.generation = read_u32(bytes.data() + 8);
    result.record.group_epoch = read_u32(bytes.data() + 32);
    result.record.reserved_through = read_u64(bytes.data() + 36);
    if (result.record.generation == 0 || all_zero(result.record.domain_id) ||
        result.record.group_epoch == 0 || result.record.reserved_through == 0) {
        result.state = OutboundCounterSlotState::malformed;
        return result;
    }
    result.state = OutboundCounterSlotState::valid;
    return result;
}

InspectedSlot inspect_slot(PersistentStorage& storage, std::size_t slot) {
    std::array<std::uint8_t, kPersistentSlotBytes> bytes{};
    const auto read = storage.read_slot(
        StorageDomain::outbound_counter_state,
        slot,
        {bytes.data(), bytes.size()});
    if (!read.read() || read.bytes_read != bytes.size()) {
        InspectedSlot result{};
        result.state = OutboundCounterSlotState::storage_failure;
        return result;
    }
    return decode_slot(bytes);
}

OutboundCounterError error_for_invalid_slots(
    const std::array<InspectedSlot, kPersistentSlotCount>& slots) {
    for (const auto& slot : slots) {
        if (slot.state == OutboundCounterSlotState::storage_failure) {
            return OutboundCounterError::storage_failure;
        }
        if (slot.state == OutboundCounterSlotState::unsupported_version) {
            return OutboundCounterError::unsupported_version;
        }
        if (slot.state == OutboundCounterSlotState::integrity_failure ||
            slot.state == OutboundCounterSlotState::malformed ||
            slot.state == OutboundCounterSlotState::uncommitted) {
            return OutboundCounterError::integrity_failure;
        }
    }
    return OutboundCounterError::no_valid_state;
}

}  // namespace

OutboundCounterLeaseStore::OutboundCounterLeaseStore(PersistentStorage& storage)
    : storage_(storage) {}

OutboundCounterLease OutboundCounterLeaseStore::reserve(
    const OutboundCounterLeaseRequest& request) {
    if (all_zero(request.domain_id) || request.group_epoch == 0 ||
        request.lease_size == 0 ||
        request.lease_size > kMaximumOutboundCounterLeaseSize) {
        return {OutboundCounterError::invalid_request};
    }

    std::array<InspectedSlot, kPersistentSlotCount> slots{};
    for (std::size_t index = 0; index < slots.size(); ++index) {
        slots[index] = inspect_slot(storage_, index);
    }
    OutboundCounterLease result{};
    for (std::size_t index = 0; index < slots.size(); ++index) {
        result.slot_states[index] = slots[index].state;
    }
    const auto invalid_slots = error_for_invalid_slots(slots);
    if (invalid_slots != OutboundCounterError::no_valid_state) {
        result.error = invalid_slots;
        return result;
    }

    const InspectedSlot* current = nullptr;
    std::size_t current_slot = kPersistentSlotCount;
    for (std::size_t index = 0; index < slots.size(); ++index) {
        if (slots[index].state != OutboundCounterSlotState::valid) {
            continue;
        }
        if (current != nullptr &&
            slots[index].record.generation == current->record.generation &&
            slots[index].bytes != current->bytes) {
            result.error = OutboundCounterError::generation_conflict;
            return result;
        }
        if (current == nullptr ||
            slots[index].record.generation > current->record.generation) {
            current = &slots[index];
            current_slot = index;
        }
    }

    CounterRecord next{};
    if (current == nullptr) {
        next = {request.domain_id, request.group_epoch, request.lease_size, 1};
        result.first_counter = 1;
        result.initialized = true;
    } else {
        if (current->record.domain_id != request.domain_id ||
            current->record.group_epoch != request.group_epoch) {
            result.error = OutboundCounterError::domain_mismatch;
            return result;
        }
        if (current->record.generation ==
            std::numeric_limits<std::uint32_t>::max()) {
            result.error = OutboundCounterError::generation_exhausted;
            return result;
        }
        if (std::numeric_limits<std::uint64_t>::max() -
                current->record.reserved_through <
            request.lease_size) {
            result.error = OutboundCounterError::counter_exhausted;
            return result;
        }
        result.first_counter = current->record.reserved_through + 1;
        next = {
            request.domain_id,
            request.group_epoch,
            current->record.reserved_through + request.lease_size,
            current->record.generation + 1};
    }
    result.last_counter = next.reserved_through;

    std::size_t target = 0;
    if (current != nullptr) {
        target = current_slot == 0 ? 1 : 0;
    } else if (slots[0].state != OutboundCounterSlotState::blank &&
               slots[1].state == OutboundCounterSlotState::blank) {
        target = 1;
    }
    const auto encoded = encode_record(next);
    const auto fail_commit = [&result](const OutboundCounterError error) {
        result.error = error;
        result.first_counter = 0;
        result.last_counter = 0;
        result.generation = 0;
        result.written_slot = kPersistentSlotCount;
        result.initialized = false;
        return result;
    };
    if (storage_.erase_slot(StorageDomain::outbound_counter_state, target) !=
        StorageError::none) {
        return fail_commit(OutboundCounterError::storage_failure);
    }
    if (storage_.write_slot(
            StorageDomain::outbound_counter_state,
            target,
            0,
            {encoded.data(), kOutboundCounterCommitOffset}) !=
            StorageError::none ||
        storage_.sync_slot(StorageDomain::outbound_counter_state, target) !=
            StorageError::none ||
        storage_.write_slot(
            StorageDomain::outbound_counter_state,
            target,
            kOutboundCounterCommitOffset,
            {encoded.data() + kOutboundCounterCommitOffset,
             kPersistentSlotBytes - kOutboundCounterCommitOffset}) !=
            StorageError::none ||
        storage_.sync_slot(StorageDomain::outbound_counter_state, target) !=
            StorageError::none) {
        return fail_commit(OutboundCounterError::storage_failure);
    }

    const auto verified = inspect_slot(storage_, target);
    if (verified.state != OutboundCounterSlotState::valid ||
        verified.bytes != encoded ||
        verified.record.domain_id != next.domain_id ||
        verified.record.group_epoch != next.group_epoch ||
        verified.record.reserved_through != next.reserved_through ||
        verified.record.generation != next.generation) {
        return fail_commit(OutboundCounterError::verification_failure);
    }
    result.error = OutboundCounterError::none;
    result.generation = next.generation;
    result.written_slot = target;
    return result;
}

OutboundCounterAllocator::OutboundCounterAllocator(
    OutboundCounterLeaseStore& store)
    : store_(store) {}

OutboundCounterError OutboundCounterAllocator::start(
    const OutboundCounterLeaseRequest& request) {
    if (started_) {
        return OutboundCounterError::already_started;
    }
    request_ = request;
    const auto error = reserve_next();
    if (error == OutboundCounterError::none) {
        started_ = true;
    }
    return error;
}

OutboundCounterError OutboundCounterAllocator::reserve_next() {
    const auto lease = store_.reserve(request_);
    if (!lease.reserved()) {
        has_remaining_ = false;
        return lease.error;
    }
    next_counter_ = lease.first_counter;
    lease_last_ = lease.last_counter;
    has_remaining_ = true;
    return OutboundCounterError::none;
}

OutboundCounterAllocation OutboundCounterAllocator::next() {
    if (!started_) {
        return {OutboundCounterError::not_started, 0};
    }
    if (!has_remaining_) {
        const auto error = reserve_next();
        if (error != OutboundCounterError::none) {
            return {error, 0};
        }
    }
    const auto allocated = next_counter_;
    if (allocated == lease_last_) {
        has_remaining_ = false;
    } else {
        ++next_counter_;
    }
    return {OutboundCounterError::none, allocated};
}

}  // namespace opentrail::persistence
