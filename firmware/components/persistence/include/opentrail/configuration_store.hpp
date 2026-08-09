#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/persistent_storage.hpp"

namespace opentrail::persistence {

inline constexpr std::uint8_t kConfigurationEnvelopeVersion = 1;
inline constexpr std::uint8_t kLegacyConfigurationSchemaVersion = 1;
inline constexpr std::uint8_t kCurrentConfigurationSchemaVersion = 2;
inline constexpr std::size_t kConfigurationHeaderBytes = 16;
inline constexpr std::size_t kConfigurationCommitOffset = 60;
inline constexpr std::uint32_t kConfigurationCommitMarker = 0xED174DC0U;
inline constexpr std::uint32_t kMinimumConfigurationWriteIntervalMs = 5000;

enum class OperatingRole : std::uint8_t {
    client = 0,
    client_repeater,
    fixed_relay,
    command_interface,
};

struct RuntimeConfiguration {
    OperatingRole role{OperatingRole::client};
    bool forwarding_enabled{false};
    bool location_broadcast_enabled{false};
    std::uint8_t display_brightness_percent{50};
    std::uint16_t moving_position_interval_seconds{60};
    std::uint16_t stationary_position_interval_seconds{300};
};

[[nodiscard]] constexpr bool operator==(
    const RuntimeConfiguration& left,
    const RuntimeConfiguration& right) {
    return left.role == right.role &&
           left.forwarding_enabled == right.forwarding_enabled &&
           left.location_broadcast_enabled ==
               right.location_broadcast_enabled &&
           left.display_brightness_percent ==
               right.display_brightness_percent &&
           left.moving_position_interval_seconds ==
               right.moving_position_interval_seconds &&
           left.stationary_position_interval_seconds ==
               right.stationary_position_interval_seconds;
}

[[nodiscard]] constexpr bool operator!=(
    const RuntimeConfiguration& left,
    const RuntimeConfiguration& right) {
    return !(left == right);
}

enum class ConfigurationError : std::uint8_t {
    none = 0,
    invalid_configuration,
    storage_failure,
    no_valid_record,
    integrity_failure,
    unsupported_envelope_version,
    unsupported_schema,
    generation_conflict,
    generation_exhausted,
    rate_limited,
    monotonic_time_rollback,
    verification_failure,
};

enum class ConfigurationSlotState : std::uint8_t {
    blank = 0,
    valid_current,
    valid_migrated,
    uncommitted,
    malformed,
    integrity_failure,
    unsupported_envelope_version,
    unsupported_schema,
    storage_failure,
};

enum class ConfigurationLoadDisposition : std::uint8_t {
    loaded_current = 0,
    migrated,
    safe_defaults,
};

enum class ConfigurationSaveDisposition : std::uint8_t {
    written = 0,
    unchanged,
    rejected,
};

struct ConfigurationLoadResult {
    RuntimeConfiguration configuration{};
    ConfigurationLoadDisposition disposition{
        ConfigurationLoadDisposition::safe_defaults};
    ConfigurationError error{ConfigurationError::no_valid_record};
    std::uint32_t generation{0};
    std::size_t selected_slot{kPersistentSlotCount};
    std::array<ConfigurationSlotState, kPersistentSlotCount> slot_states{};

    [[nodiscard]] constexpr bool from_persistence() const {
        return disposition != ConfigurationLoadDisposition::safe_defaults;
    }
};

struct ConfigurationSaveResult {
    ConfigurationSaveDisposition disposition{
        ConfigurationSaveDisposition::rejected};
    ConfigurationError error{ConfigurationError::none};
    std::uint32_t generation{0};
    std::size_t written_slot{kPersistentSlotCount};

    [[nodiscard]] constexpr bool accepted() const {
        return error == ConfigurationError::none;
    }
};

[[nodiscard]] constexpr RuntimeConfiguration safe_default_configuration() {
    return {};
}

[[nodiscard]] std::uint32_t configuration_crc32(StorageByteView bytes);

class ConfigurationStore {
public:
    explicit ConfigurationStore(
        PersistentStorage& storage,
        std::uint32_t minimum_write_interval_ms =
            kMinimumConfigurationWriteIntervalMs);

    [[nodiscard]] ConfigurationLoadResult load();
    [[nodiscard]] ConfigurationSaveResult save(
        const RuntimeConfiguration& configuration,
        std::uint64_t now_ms);

private:
    static bool valid_configuration(const RuntimeConfiguration& configuration);

    PersistentStorage& storage_;
    std::uint32_t minimum_write_interval_ms_{0};
    std::uint64_t last_successful_write_ms_{0};
    bool has_successful_write_{false};
};

}  // namespace opentrail::persistence
