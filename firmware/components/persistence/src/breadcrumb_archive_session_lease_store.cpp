#include "opentrail/breadcrumb_archive_session_lease_store.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace opentrail::persistence {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'T', 'B', 'L'}};
constexpr std::size_t kHeaderBytes = 16;
constexpr std::size_t kPayloadBytes = 16;
constexpr std::size_t kCrcOffset = 56;

struct LeaseRecord {
    std::uint32_t generation{0};
    std::uint64_t range_start{0};
    std::uint64_t range_end{0};
};

struct InspectedSlot {
    BreadcrumbArchiveSessionLeaseSlotState state{
        BreadcrumbArchiveSessionLeaseSlotState::blank};
    LeaseRecord record{};
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

std::array<std::uint8_t, kPersistentSlotBytes> encode_record(
    const LeaseRecord& record) {
    std::array<std::uint8_t, kPersistentSlotBytes> bytes{};
    std::copy(kMagic.begin(), kMagic.end(), bytes.begin());
    bytes[4] = kBreadcrumbArchiveSessionLeaseEnvelopeVersion;
    bytes[5] = kBreadcrumbArchiveSessionLeaseSchemaVersion;
    bytes[7] = static_cast<std::uint8_t>(kHeaderBytes);
    write_u32(bytes.data() + 8, record.generation);
    write_u16(bytes.data() + 12, static_cast<std::uint16_t>(kPayloadBytes));
    write_u64(bytes.data() + 16, record.range_start);
    write_u64(bytes.data() + 24, record.range_end);
    write_u32(bytes.data() + kCrcOffset, crc32(bytes.data(), kCrcOffset));
    write_u32(
        bytes.data() + kBreadcrumbArchiveSessionLeaseCommitOffset,
        kBreadcrumbArchiveSessionLeaseCommitMarker);
    return bytes;
}

InspectedSlot decode_slot(
    const std::array<std::uint8_t, kPersistentSlotBytes>& bytes) {
    InspectedSlot result{};
    result.bytes = bytes;
    if (all_value(bytes.data(), bytes.size(), 0xFFU)) {
        return result;
    }
    if (read_u32(
            bytes.data() + kBreadcrumbArchiveSessionLeaseCommitOffset) !=
        kBreadcrumbArchiveSessionLeaseCommitMarker) {
        result.state =
            BreadcrumbArchiveSessionLeaseSlotState::uncommitted;
        return result;
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin()) ||
        bytes[6] != 0 || bytes[7] != kHeaderBytes ||
        read_u16(bytes.data() + 12) != kPayloadBytes ||
        bytes[14] != 0 || bytes[15] != 0 ||
        !all_value(bytes.data() + 32, 24, 0)) {
        result.state = BreadcrumbArchiveSessionLeaseSlotState::malformed;
        return result;
    }
    if (bytes[4] != kBreadcrumbArchiveSessionLeaseEnvelopeVersion ||
        bytes[5] != kBreadcrumbArchiveSessionLeaseSchemaVersion) {
        result.state =
            BreadcrumbArchiveSessionLeaseSlotState::unsupported_version;
        return result;
    }
    if (read_u32(bytes.data() + kCrcOffset) !=
        crc32(bytes.data(), kCrcOffset)) {
        result.state =
            BreadcrumbArchiveSessionLeaseSlotState::integrity_failure;
        return result;
    }

    result.record.generation = read_u32(bytes.data() + 8);
    result.record.range_start = read_u64(bytes.data() + 16);
    result.record.range_end = read_u64(bytes.data() + 24);
    if (result.record.generation == 0 || result.record.range_start == 0 ||
        result.record.range_start > result.record.range_end) {
        result.state = BreadcrumbArchiveSessionLeaseSlotState::malformed;
        return result;
    }
    result.state = BreadcrumbArchiveSessionLeaseSlotState::valid;
    return result;
}

InspectedSlot inspect_slot(PersistentStorage& storage, std::size_t slot) {
    std::array<std::uint8_t, kPersistentSlotBytes> bytes{};
    const auto read = storage.read_slot(
        StorageDomain::breadcrumb_archive_state,
        slot,
        {bytes.data(), bytes.size()});
    if (!read.read() || read.bytes_read != bytes.size()) {
        InspectedSlot result{};
        result.state =
            BreadcrumbArchiveSessionLeaseSlotState::storage_failure;
        return result;
    }
    return decode_slot(bytes);
}

BreadcrumbArchiveSessionLeaseError error_for_invalid_slots(
    const std::array<InspectedSlot, kPersistentSlotCount>& slots) {
    for (const auto& slot : slots) {
        if (slot.state ==
            BreadcrumbArchiveSessionLeaseSlotState::storage_failure) {
            return BreadcrumbArchiveSessionLeaseError::storage_failure;
        }
        if (slot.state ==
            BreadcrumbArchiveSessionLeaseSlotState::unsupported_version) {
            return BreadcrumbArchiveSessionLeaseError::unsupported_version;
        }
        if (slot.state ==
                BreadcrumbArchiveSessionLeaseSlotState::integrity_failure ||
            slot.state == BreadcrumbArchiveSessionLeaseSlotState::malformed ||
            slot.state ==
                BreadcrumbArchiveSessionLeaseSlotState::uncommitted) {
            return BreadcrumbArchiveSessionLeaseError::integrity_failure;
        }
    }
    return BreadcrumbArchiveSessionLeaseError::no_valid_state;
}

bool make_range(
    std::uint64_t first,
    std::uint32_t lease_size,
    std::uint64_t& final) {
    if (first == 0 || lease_size == 0) {
        return false;
    }
    const auto span = static_cast<std::uint64_t>(lease_size - 1U);
    if (first > std::numeric_limits<std::uint64_t>::max() - span) {
        return false;
    }
    final = first + span;
    return true;
}

}  // namespace

BreadcrumbArchiveSessionLeaseStore::BreadcrumbArchiveSessionLeaseStore(
    PersistentStorage& storage)
    : storage_(storage) {}

BreadcrumbArchiveSessionLeaseAllocation
BreadcrumbArchiveSessionLeaseStore::allocate(
    const BreadcrumbArchiveSessionLeaseRequest& request) {
    if (request.initial_session_id == 0 || request.lease_size == 0) {
        return {BreadcrumbArchiveSessionLeaseError::invalid_request};
    }

    std::array<InspectedSlot, kPersistentSlotCount> slots{};
    for (std::size_t index = 0; index < slots.size(); ++index) {
        slots[index] = inspect_slot(storage_, index);
    }
    BreadcrumbArchiveSessionLeaseAllocation result{};
    for (std::size_t index = 0; index < slots.size(); ++index) {
        result.slot_states[index] = slots[index].state;
    }
    const auto invalid_slots = error_for_invalid_slots(slots);
    if (invalid_slots != BreadcrumbArchiveSessionLeaseError::no_valid_state) {
        result.error = invalid_slots;
        return result;
    }

    const InspectedSlot* current = nullptr;
    std::size_t current_slot = kPersistentSlotCount;
    for (std::size_t index = 0; index < slots.size(); ++index) {
        if (slots[index].state !=
            BreadcrumbArchiveSessionLeaseSlotState::valid) {
            continue;
        }
        if (current != nullptr &&
            slots[index].record.generation == current->record.generation &&
            slots[index].bytes != current->bytes) {
            result.error =
                BreadcrumbArchiveSessionLeaseError::generation_conflict;
            return result;
        }
        if (current == nullptr ||
            slots[index].record.generation > current->record.generation) {
            current = &slots[index];
            current_slot = index;
        }
    }

    LeaseRecord next{};
    bool initialized = false;
    if (current == nullptr) {
        std::uint64_t requested_final = 0;
        if (!make_range(
                request.initial_session_id,
                request.lease_size,
                requested_final)) {
            result.error =
                BreadcrumbArchiveSessionLeaseError::session_exhausted;
            return result;
        }
        next = {1, request.initial_session_id, requested_final};
        initialized = true;
    } else {
        if (current->record.generation ==
            std::numeric_limits<std::uint32_t>::max()) {
            result.error =
                BreadcrumbArchiveSessionLeaseError::generation_exhausted;
            return result;
        }
        if (current->record.range_end ==
            std::numeric_limits<std::uint64_t>::max()) {
            result.error =
                BreadcrumbArchiveSessionLeaseError::session_exhausted;
            return result;
        }
        next.generation = current->record.generation + 1;
        next.range_start = current->record.range_end + 1;
        if (!make_range(
                next.range_start, request.lease_size, next.range_end)) {
            result.error =
                BreadcrumbArchiveSessionLeaseError::session_exhausted;
            return result;
        }
    }

    std::size_t target = 0;
    if (current != nullptr) {
        target = current_slot == 0 ? 1 : 0;
    } else if (
        slots[0].state != BreadcrumbArchiveSessionLeaseSlotState::blank &&
        slots[1].state == BreadcrumbArchiveSessionLeaseSlotState::blank) {
        target = 1;
    }

    const auto encoded = encode_record(next);
    const auto fail_storage = [&result]() {
        result.error = BreadcrumbArchiveSessionLeaseError::storage_failure;
        return result;
    };
    if (storage_.erase_slot(
            StorageDomain::breadcrumb_archive_state, target) !=
        StorageError::none) {
        return fail_storage();
    }
    if (storage_.write_slot(
            StorageDomain::breadcrumb_archive_state,
            target,
            0,
            {encoded.data(), kBreadcrumbArchiveSessionLeaseCommitOffset}) !=
            StorageError::none ||
        storage_.sync_slot(
            StorageDomain::breadcrumb_archive_state, target) !=
            StorageError::none ||
        storage_.write_slot(
            StorageDomain::breadcrumb_archive_state,
            target,
            kBreadcrumbArchiveSessionLeaseCommitOffset,
            {encoded.data() + kBreadcrumbArchiveSessionLeaseCommitOffset,
             kPersistentSlotBytes -
                 kBreadcrumbArchiveSessionLeaseCommitOffset}) !=
            StorageError::none ||
        storage_.sync_slot(
            StorageDomain::breadcrumb_archive_state, target) !=
            StorageError::none) {
        return fail_storage();
    }

    const auto verified = inspect_slot(storage_, target);
    if (verified.state != BreadcrumbArchiveSessionLeaseSlotState::valid ||
        verified.bytes != encoded ||
        verified.record.generation != next.generation ||
        verified.record.range_start != next.range_start ||
        verified.record.range_end != next.range_end) {
        result.error =
            BreadcrumbArchiveSessionLeaseError::verification_failure;
        return result;
    }

    result.error = BreadcrumbArchiveSessionLeaseError::none;
    result.generation = next.generation;
    result.first_session_id = next.range_start;
    result.final_session_id = next.range_end;
    result.written_slot = target;
    result.initialized = initialized;
    return result;
}

}  // namespace opentrail::persistence
