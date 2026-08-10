#include "opentrail/ack_responder_session_store.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace opentrail::persistence {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'T', 'A', 'S'}};
constexpr std::size_t kHeaderBytes = 16;
constexpr std::size_t kPayloadBytes = 16;
constexpr std::size_t kCrcOffset = 56;

struct SessionRecord {
    std::uint64_t consumer_id{0};
    std::uint32_t authorization_epoch{0};
    std::uint32_t boot_session_id{0};
    std::uint32_t generation{0};
};

struct InspectedSlot {
    AckResponderSessionSlotState state{AckResponderSessionSlotState::blank};
    SessionRecord record{};
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
    const SessionRecord& record) {
    std::array<std::uint8_t, kPersistentSlotBytes> bytes{};
    std::copy(kMagic.begin(), kMagic.end(), bytes.begin());
    bytes[4] = kAckResponderSessionEnvelopeVersion;
    bytes[5] = kAckResponderSessionSchemaVersion;
    bytes[7] = static_cast<std::uint8_t>(kHeaderBytes);
    write_u32(bytes.data() + 8, record.generation);
    write_u16(bytes.data() + 12, static_cast<std::uint16_t>(kPayloadBytes));
    write_u64(bytes.data() + 16, record.consumer_id);
    write_u32(bytes.data() + 24, record.authorization_epoch);
    write_u32(bytes.data() + 28, record.boot_session_id);
    write_u32(bytes.data() + kCrcOffset, crc32(bytes.data(), kCrcOffset));
    write_u32(bytes.data() + kAckResponderSessionCommitOffset,
              kAckResponderSessionCommitMarker);
    return bytes;
}

InspectedSlot decode_slot(
    const std::array<std::uint8_t, kPersistentSlotBytes>& bytes) {
    InspectedSlot result{};
    result.bytes = bytes;
    if (all_value(bytes.data(), bytes.size(), 0xFFU)) {
        return result;
    }
    if (read_u32(bytes.data() + kAckResponderSessionCommitOffset) !=
        kAckResponderSessionCommitMarker) {
        result.state = AckResponderSessionSlotState::uncommitted;
        return result;
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin()) ||
        bytes[6] != 0 || bytes[7] != kHeaderBytes ||
        read_u16(bytes.data() + 12) != kPayloadBytes ||
        bytes[14] != 0 || bytes[15] != 0 ||
        !all_value(bytes.data() + 32, 24, 0)) {
        result.state = AckResponderSessionSlotState::malformed;
        return result;
    }
    if (bytes[4] != kAckResponderSessionEnvelopeVersion ||
        bytes[5] != kAckResponderSessionSchemaVersion) {
        result.state = AckResponderSessionSlotState::unsupported_version;
        return result;
    }
    if (read_u32(bytes.data() + kCrcOffset) !=
        crc32(bytes.data(), kCrcOffset)) {
        result.state = AckResponderSessionSlotState::integrity_failure;
        return result;
    }
    result.record.generation = read_u32(bytes.data() + 8);
    result.record.consumer_id = read_u64(bytes.data() + 16);
    result.record.authorization_epoch = read_u32(bytes.data() + 24);
    result.record.boot_session_id = read_u32(bytes.data() + 28);
    if (result.record.generation == 0 || result.record.consumer_id == 0 ||
        result.record.authorization_epoch == 0 ||
        result.record.boot_session_id == 0) {
        result.state = AckResponderSessionSlotState::malformed;
        return result;
    }
    result.state = AckResponderSessionSlotState::valid;
    return result;
}

InspectedSlot inspect_slot(PersistentStorage& storage, std::size_t slot) {
    std::array<std::uint8_t, kPersistentSlotBytes> bytes{};
    const auto read = storage.read_slot(
        StorageDomain::protocol_state,
        slot,
        {bytes.data(), bytes.size()});
    if (!read.read() || read.bytes_read != bytes.size()) {
        InspectedSlot result{};
        result.state = AckResponderSessionSlotState::storage_failure;
        return result;
    }
    return decode_slot(bytes);
}

AckResponderSessionError error_for_invalid_slots(
    const std::array<InspectedSlot, kPersistentSlotCount>& slots) {
    for (const auto& slot : slots) {
        if (slot.state == AckResponderSessionSlotState::storage_failure) {
            return AckResponderSessionError::storage_failure;
        }
        if (slot.state == AckResponderSessionSlotState::unsupported_version) {
            return AckResponderSessionError::unsupported_version;
        }
        if (slot.state == AckResponderSessionSlotState::integrity_failure ||
            slot.state == AckResponderSessionSlotState::malformed ||
            slot.state == AckResponderSessionSlotState::uncommitted) {
            return AckResponderSessionError::integrity_failure;
        }
    }
    return AckResponderSessionError::no_valid_state;
}

}  // namespace

AckResponderSessionStore::AckResponderSessionStore(PersistentStorage& storage)
    : storage_(storage) {}

AckResponderSessionAllocation AckResponderSessionStore::allocate(
    const AckResponderSessionRequest& request) {
    if (request.consumer_id == 0 || request.authorization_epoch == 0 ||
        request.initial_boot_session_id == 0) {
        return {AckResponderSessionError::invalid_request};
    }

    std::array<InspectedSlot, kPersistentSlotCount> slots{};
    for (std::size_t index = 0; index < slots.size(); ++index) {
        slots[index] = inspect_slot(storage_, index);
    }
    AckResponderSessionAllocation result{};
    for (std::size_t index = 0; index < slots.size(); ++index) {
        result.slot_states[index] = slots[index].state;
    }
    if (std::any_of(
            slots.begin(), slots.end(), [](const InspectedSlot& slot) {
                return slot.state ==
                       AckResponderSessionSlotState::storage_failure;
            })) {
        result.error = AckResponderSessionError::storage_failure;
        return result;
    }
    const auto invalid_slots = error_for_invalid_slots(slots);
    if (invalid_slots != AckResponderSessionError::no_valid_state) {
        result.error = invalid_slots;
        return result;
    }

    const InspectedSlot* current = nullptr;
    std::size_t current_slot = kPersistentSlotCount;
    for (std::size_t index = 0; index < slots.size(); ++index) {
        if (slots[index].state != AckResponderSessionSlotState::valid) {
            continue;
        }
        if (current != nullptr &&
            slots[index].record.generation == current->record.generation &&
            slots[index].bytes != current->bytes) {
            result.error = AckResponderSessionError::generation_conflict;
            return result;
        }
        if (current == nullptr ||
            slots[index].record.generation > current->record.generation) {
            current = &slots[index];
            current_slot = index;
        }
    }

    SessionRecord next{};
    if (current == nullptr) {
        next = {
            request.consumer_id,
            request.authorization_epoch,
            request.initial_boot_session_id,
            1};
        result.initialized = true;
    } else {
        if (current->record.consumer_id != request.consumer_id ||
            current->record.authorization_epoch !=
                request.authorization_epoch) {
            result.error = AckResponderSessionError::identity_mismatch;
            return result;
        }
        if (current->record.generation ==
            std::numeric_limits<std::uint32_t>::max()) {
            result.error = AckResponderSessionError::generation_exhausted;
            return result;
        }
        if (current->record.boot_session_id ==
            std::numeric_limits<std::uint32_t>::max()) {
            result.error = AckResponderSessionError::boot_session_exhausted;
            return result;
        }
        next = {
            request.consumer_id,
            request.authorization_epoch,
            current->record.boot_session_id + 1,
            current->record.generation + 1};
    }

    std::size_t target = 0;
    if (current != nullptr) {
        target = current_slot == 0 ? 1 : 0;
    } else if (slots[0].state != AckResponderSessionSlotState::blank &&
               slots[1].state == AckResponderSessionSlotState::blank) {
        target = 1;
    }
    const auto encoded = encode_record(next);
    const auto fail_storage = [&result]() {
        result.error = AckResponderSessionError::storage_failure;
        return result;
    };
    if (storage_.erase_slot(StorageDomain::protocol_state, target) !=
        StorageError::none) {
        return fail_storage();
    }
    if (storage_.write_slot(
            StorageDomain::protocol_state,
            target,
            0,
            {encoded.data(), kAckResponderSessionCommitOffset}) !=
            StorageError::none ||
        storage_.sync_slot(StorageDomain::protocol_state, target) !=
            StorageError::none ||
        storage_.write_slot(
            StorageDomain::protocol_state,
            target,
            kAckResponderSessionCommitOffset,
            {encoded.data() + kAckResponderSessionCommitOffset,
             kPersistentSlotBytes - kAckResponderSessionCommitOffset}) !=
            StorageError::none ||
        storage_.sync_slot(StorageDomain::protocol_state, target) !=
            StorageError::none) {
        return fail_storage();
    }

    const auto verified = inspect_slot(storage_, target);
    if (verified.state != AckResponderSessionSlotState::valid ||
        verified.bytes != encoded ||
        verified.record.consumer_id != next.consumer_id ||
        verified.record.authorization_epoch != next.authorization_epoch ||
        verified.record.boot_session_id != next.boot_session_id ||
        verified.record.generation != next.generation) {
        result.error = AckResponderSessionError::verification_failure;
        return result;
    }
    result.error = AckResponderSessionError::none;
    result.generation = next.generation;
    result.boot_session_id = next.boot_session_id;
    result.written_slot = target;
    return result;
}

AckResponderSessionError AckResponderSessionStore::reset() {
    const auto first = storage_.erase_slot(StorageDomain::protocol_state, 0);
    const auto second = storage_.erase_slot(StorageDomain::protocol_state, 1);
    return first == StorageError::none && second == StorageError::none
               ? AckResponderSessionError::none
               : AckResponderSessionError::storage_failure;
}

}  // namespace opentrail::persistence
