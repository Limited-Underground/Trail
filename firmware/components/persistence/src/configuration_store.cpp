#include "opentrail/configuration_store.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::persistence {
namespace {

constexpr std::array<std::uint8_t, 4> kConfigurationMagic{
    'O', 'T', 'C', 'F'};
constexpr std::size_t kLegacyPayloadBytes = 6;
constexpr std::size_t kCurrentPayloadBytes = 8;
constexpr std::uint8_t kForwardingEnabled = 0x01;
constexpr std::uint8_t kLocationBroadcastEnabled = 0x02;

struct DecodedSlot {
    ConfigurationSlotState state{ConfigurationSlotState::blank};
    RuntimeConfiguration configuration{};
    std::uint32_t generation{0};
    std::uint8_t schema_version{0};

    [[nodiscard]] bool supported() const {
        return state == ConfigurationSlotState::valid_current ||
               state == ConfigurationSlotState::valid_migrated;
    }
};

void write_u16_le(std::uint8_t* destination, std::uint16_t value) {
    destination[0] = static_cast<std::uint8_t>(value & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32_le(std::uint8_t* destination, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        destination[index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

std::uint16_t read_u16_le(const std::uint8_t* source) {
    return static_cast<std::uint16_t>(source[0]) |
           static_cast<std::uint16_t>(source[1] << 8U);
}

std::uint32_t read_u32_le(const std::uint8_t* source) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(source[index]) << (index * 8U);
    }
    return value;
}

bool known_role(OperatingRole role) {
    switch (role) {
        case OperatingRole::client:
        case OperatingRole::client_repeater:
        case OperatingRole::fixed_relay:
        case OperatingRole::command_interface:
            return true;
    }
    return false;
}

bool is_valid_configuration(const RuntimeConfiguration& configuration) {
    if (!known_role(configuration.role) ||
        configuration.display_brightness_percent > 100 ||
        configuration.moving_position_interval_seconds < 30 ||
        configuration.moving_position_interval_seconds > 3600 ||
        configuration.stationary_position_interval_seconds <
            configuration.moving_position_interval_seconds ||
        configuration.stationary_position_interval_seconds > 3600) {
        return false;
    }
    if (configuration.forwarding_enabled &&
        configuration.role != OperatingRole::client_repeater &&
        configuration.role != OperatingRole::fixed_relay) {
        return false;
    }
    return true;
}

bool decode_payload(
    std::uint8_t schema_version,
    const std::uint8_t* payload,
    std::size_t payload_size,
    RuntimeConfiguration& configuration) {
    if (schema_version == kLegacyConfigurationSchemaVersion) {
        if (payload_size != kLegacyPayloadBytes ||
            (payload[1] & static_cast<std::uint8_t>(~kForwardingEnabled)) != 0 ||
            payload[3] != 0) {
            return false;
        }
        configuration.role = static_cast<OperatingRole>(payload[0]);
        configuration.forwarding_enabled =
            (payload[1] & kForwardingEnabled) != 0;
        configuration.location_broadcast_enabled = false;
        configuration.display_brightness_percent = payload[2];
        configuration.moving_position_interval_seconds = read_u16_le(payload + 4);
        configuration.stationary_position_interval_seconds =
            std::max<std::uint16_t>(
                safe_default_configuration()
                    .stationary_position_interval_seconds,
                configuration.moving_position_interval_seconds);
        return is_valid_configuration(configuration);
    }
    if (schema_version == kCurrentConfigurationSchemaVersion) {
        constexpr std::uint8_t known_flags =
            kForwardingEnabled | kLocationBroadcastEnabled;
        if (payload_size != kCurrentPayloadBytes ||
            (payload[1] & static_cast<std::uint8_t>(~known_flags)) != 0 ||
            payload[3] != 0) {
            return false;
        }
        configuration.role = static_cast<OperatingRole>(payload[0]);
        configuration.forwarding_enabled =
            (payload[1] & kForwardingEnabled) != 0;
        configuration.location_broadcast_enabled =
            (payload[1] & kLocationBroadcastEnabled) != 0;
        configuration.display_brightness_percent = payload[2];
        configuration.moving_position_interval_seconds = read_u16_le(payload + 4);
        configuration.stationary_position_interval_seconds =
            read_u16_le(payload + 6);
        return is_valid_configuration(configuration);
    }
    return false;
}

DecodedSlot decode_slot(
    const std::array<std::uint8_t, kPersistentSlotBytes>& bytes) {
    const bool blank = std::all_of(
        bytes.begin(), bytes.end(), [](std::uint8_t byte) {
            return byte == 0xFFU;
        });
    if (blank) {
        return {};
    }
    if (read_u32_le(bytes.data() + kConfigurationCommitOffset) !=
        kConfigurationCommitMarker) {
        return {ConfigurationSlotState::uncommitted, {}, 0, 0};
    }
    if (!std::equal(
            kConfigurationMagic.begin(),
            kConfigurationMagic.end(),
            bytes.begin())) {
        return {ConfigurationSlotState::malformed, {}, 0, 0};
    }
    const auto generation = read_u32_le(bytes.data() + 8);
    if (generation == 0) {
        return {ConfigurationSlotState::malformed, {}, 0, bytes[5]};
    }
    if (bytes[4] != kConfigurationEnvelopeVersion) {
        return {
            ConfigurationSlotState::unsupported_envelope_version,
            {},
            generation,
            bytes[5],
        };
    }
    if (bytes[6] != 0 || bytes[7] != kConfigurationHeaderBytes ||
        bytes[14] != 0 || bytes[15] != 0) {
        return {ConfigurationSlotState::malformed, {}, 0, 0};
    }

    const auto payload_size = read_u16_le(bytes.data() + 12);
    const auto checksum_offset = kConfigurationHeaderBytes + payload_size;
    if (checksum_offset + 4 > kConfigurationCommitOffset) {
        return {ConfigurationSlotState::malformed, {}, generation, bytes[5]};
    }
    const auto stored_checksum = read_u32_le(bytes.data() + checksum_offset);
    const auto calculated_checksum = configuration_crc32({
        bytes.data(),
        kConfigurationHeaderBytes + payload_size,
    });
    if (stored_checksum != calculated_checksum) {
        return {
            ConfigurationSlotState::integrity_failure,
            {},
            generation,
            bytes[5],
        };
    }

    if (bytes[5] != kLegacyConfigurationSchemaVersion &&
        bytes[5] != kCurrentConfigurationSchemaVersion) {
        return {
            ConfigurationSlotState::unsupported_schema,
            {},
            generation,
            bytes[5],
        };
    }

    RuntimeConfiguration configuration{};
    if (!decode_payload(
            bytes[5],
            bytes.data() + kConfigurationHeaderBytes,
            payload_size,
            configuration)) {
        return {
            ConfigurationSlotState::malformed,
            {},
            generation,
            bytes[5],
        };
    }
    return {
        bytes[5] == kCurrentConfigurationSchemaVersion
            ? ConfigurationSlotState::valid_current
            : ConfigurationSlotState::valid_migrated,
        configuration,
        generation,
        bytes[5],
    };
}

std::array<std::uint8_t, kPersistentSlotBytes> encode_current_record(
    const RuntimeConfiguration& configuration,
    std::uint32_t generation,
    std::size_t& body_bytes) {
    std::array<std::uint8_t, kPersistentSlotBytes> output{};
    output.fill(0xFFU);
    std::copy(
        kConfigurationMagic.begin(),
        kConfigurationMagic.end(),
        output.begin());
    output[4] = kConfigurationEnvelopeVersion;
    output[5] = kCurrentConfigurationSchemaVersion;
    output[6] = 0;
    output[7] = static_cast<std::uint8_t>(kConfigurationHeaderBytes);
    write_u32_le(output.data() + 8, generation);
    write_u16_le(output.data() + 12, kCurrentPayloadBytes);
    output[14] = 0;
    output[15] = 0;

    auto* payload = output.data() + kConfigurationHeaderBytes;
    payload[0] = static_cast<std::uint8_t>(configuration.role);
    payload[1] = static_cast<std::uint8_t>(
        (configuration.forwarding_enabled ? kForwardingEnabled : 0U) |
        (configuration.location_broadcast_enabled
             ? kLocationBroadcastEnabled
             : 0U));
    payload[2] = configuration.display_brightness_percent;
    payload[3] = 0;
    write_u16_le(payload + 4, configuration.moving_position_interval_seconds);
    write_u16_le(
        payload + 6,
        configuration.stationary_position_interval_seconds);

    const auto checksum_offset =
        kConfigurationHeaderBytes + kCurrentPayloadBytes;
    write_u32_le(
        output.data() + checksum_offset,
        configuration_crc32({output.data(), checksum_offset}));
    body_bytes = checksum_offset + 4;
    write_u32_le(
        output.data() + kConfigurationCommitOffset,
        kConfigurationCommitMarker);
    return output;
}

}  // namespace

std::uint32_t configuration_crc32(StorageByteView bytes) {
    if (bytes.data == nullptr && bytes.size != 0) {
        return 0;
    }
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < bytes.size; ++index) {
        crc ^= bytes.data[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0
                ? (crc >> 1U) ^ 0xEDB88320U
                : crc >> 1U;
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

ConfigurationStore::ConfigurationStore(
    PersistentStorage& storage,
    std::uint32_t minimum_write_interval_ms)
    : storage_(storage),
      minimum_write_interval_ms_(minimum_write_interval_ms) {}

bool ConfigurationStore::valid_configuration(
    const RuntimeConfiguration& configuration) {
    return is_valid_configuration(configuration);
}

ConfigurationLoadResult ConfigurationStore::load() {
    std::array<DecodedSlot, kPersistentSlotCount> decoded{};
    ConfigurationLoadResult result{};
    result.configuration = safe_default_configuration();

    for (std::size_t slot = 0; slot < kPersistentSlotCount; ++slot) {
        std::array<std::uint8_t, kPersistentSlotBytes> bytes{};
        const auto read = storage_.read_slot(
            StorageDomain::configuration,
            slot,
            {bytes.data(), bytes.size()});
        if (!read.read() || read.bytes_read != bytes.size()) {
            decoded[slot].state = ConfigurationSlotState::storage_failure;
        } else {
            decoded[slot] = decode_slot(bytes);
        }
        result.slot_states[slot] = decoded[slot].state;
    }

    std::size_t selected = kPersistentSlotCount;
    for (std::size_t slot = 0; slot < kPersistentSlotCount; ++slot) {
        if (!decoded[slot].supported()) {
            continue;
        }
        if (selected == kPersistentSlotCount ||
            decoded[slot].generation > decoded[selected].generation) {
            selected = slot;
        } else if (decoded[slot].generation == decoded[selected].generation &&
                   (decoded[slot].schema_version !=
                        decoded[selected].schema_version ||
                    decoded[slot].configuration !=
                        decoded[selected].configuration)) {
            result.error = ConfigurationError::generation_conflict;
            return result;
        }
    }

    std::uint32_t newest_unsupported_envelope_generation = 0;
    std::uint32_t newest_unsupported_schema_generation = 0;
    for (const auto& slot : decoded) {
        if (slot.state ==
            ConfigurationSlotState::unsupported_envelope_version) {
            newest_unsupported_envelope_generation = std::max(
                newest_unsupported_envelope_generation,
                slot.generation);
        } else if (slot.state == ConfigurationSlotState::unsupported_schema) {
            newest_unsupported_schema_generation = std::max(
                newest_unsupported_schema_generation,
                slot.generation);
        }
    }
    if (newest_unsupported_envelope_generation != 0 &&
        (selected == kPersistentSlotCount ||
         newest_unsupported_envelope_generation >=
             decoded[selected].generation)) {
        result.error = ConfigurationError::unsupported_envelope_version;
        return result;
    }
    if (newest_unsupported_schema_generation != 0 &&
        (selected == kPersistentSlotCount ||
         newest_unsupported_schema_generation >=
             decoded[selected].generation)) {
        result.error = ConfigurationError::unsupported_schema;
        return result;
    }

    if (selected != kPersistentSlotCount) {
        result.configuration = decoded[selected].configuration;
        result.disposition =
            decoded[selected].state == ConfigurationSlotState::valid_current
            ? ConfigurationLoadDisposition::loaded_current
            : ConfigurationLoadDisposition::migrated;
        result.error = ConfigurationError::none;
        result.generation = decoded[selected].generation;
        result.selected_slot = selected;
        return result;
    }

    const bool storage_failure = std::any_of(
        decoded.begin(), decoded.end(), [](const DecodedSlot& slot) {
            return slot.state == ConfigurationSlotState::storage_failure;
        });
    const bool corrupt = std::any_of(
        decoded.begin(), decoded.end(), [](const DecodedSlot& slot) {
            return slot.state == ConfigurationSlotState::integrity_failure ||
                   slot.state == ConfigurationSlotState::malformed;
        });
    result.error = storage_failure
        ? ConfigurationError::storage_failure
        : (corrupt ? ConfigurationError::integrity_failure
                   : ConfigurationError::no_valid_record);
    return result;
}

ConfigurationSaveResult ConfigurationStore::save(
    const RuntimeConfiguration& configuration,
    std::uint64_t now_ms) {
    if (!valid_configuration(configuration)) {
        return {
            ConfigurationSaveDisposition::rejected,
            ConfigurationError::invalid_configuration,
            0,
            kPersistentSlotCount,
        };
    }

    const auto current = load();
    if (current.error == ConfigurationError::unsupported_envelope_version ||
        current.error == ConfigurationError::unsupported_schema ||
        current.error == ConfigurationError::generation_conflict ||
        current.error == ConfigurationError::storage_failure) {
        return {
            ConfigurationSaveDisposition::rejected,
            current.error,
            current.generation,
            kPersistentSlotCount,
        };
    }
    if (current.disposition == ConfigurationLoadDisposition::loaded_current &&
        current.configuration == configuration) {
        return {
            ConfigurationSaveDisposition::unchanged,
            ConfigurationError::none,
            current.generation,
            current.selected_slot,
        };
    }
    if (has_successful_write_ && now_ms < last_successful_write_ms_) {
        return {
            ConfigurationSaveDisposition::rejected,
            ConfigurationError::monotonic_time_rollback,
            current.generation,
            kPersistentSlotCount,
        };
    }
    if (has_successful_write_ &&
        now_ms - last_successful_write_ms_ < minimum_write_interval_ms_) {
        return {
            ConfigurationSaveDisposition::rejected,
            ConfigurationError::rate_limited,
            current.generation,
            kPersistentSlotCount,
        };
    }
    if (current.generation == std::numeric_limits<std::uint32_t>::max()) {
        return {
            ConfigurationSaveDisposition::rejected,
            ConfigurationError::generation_exhausted,
            current.generation,
            kPersistentSlotCount,
        };
    }

    const auto next_generation = current.generation + 1U;
    const auto target_slot = current.selected_slot == 0 ? 1U : 0U;
    std::size_t body_bytes = 0;
    const auto encoded =
        encode_current_record(configuration, next_generation, body_bytes);

    auto storage_error = storage_.erase_slot(
        StorageDomain::configuration,
        target_slot);
    if (storage_error == StorageError::none) {
        storage_error = storage_.write_slot(
            StorageDomain::configuration,
            target_slot,
            0,
            {encoded.data(), body_bytes});
    }
    if (storage_error == StorageError::none) {
        storage_error = storage_.sync_slot(
            StorageDomain::configuration,
            target_slot);
    }
    if (storage_error == StorageError::none) {
        storage_error = storage_.write_slot(
            StorageDomain::configuration,
            target_slot,
            kConfigurationCommitOffset,
            {encoded.data() + kConfigurationCommitOffset, 4});
    }
    if (storage_error == StorageError::none) {
        storage_error = storage_.sync_slot(
            StorageDomain::configuration,
            target_slot);
    }
    if (storage_error != StorageError::none) {
        return {
            ConfigurationSaveDisposition::rejected,
            ConfigurationError::storage_failure,
            current.generation,
            target_slot,
        };
    }

    const auto verification = load();
    if (verification.error != ConfigurationError::none ||
        verification.generation != next_generation ||
        verification.selected_slot != target_slot ||
        verification.configuration != configuration) {
        return {
            ConfigurationSaveDisposition::rejected,
            ConfigurationError::verification_failure,
            current.generation,
            target_slot,
        };
    }

    last_successful_write_ms_ = now_ms;
    has_successful_write_ = true;
    return {
        ConfigurationSaveDisposition::written,
        ConfigurationError::none,
        next_generation,
        target_slot,
    };
}

}  // namespace opentrail::persistence
