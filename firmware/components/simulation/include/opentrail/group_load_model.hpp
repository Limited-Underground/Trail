#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/lora_airtime.hpp"

namespace opentrail::simulation {

inline constexpr std::uint64_t kMaximumScenarioDurationMs = 604800000;
inline constexpr std::uint8_t kMaximumSourceAttempts = 4;

enum class GroupLoadError : std::uint8_t {
    none = 0,
    invalid_member_count,
    invalid_relay_count,
    invalid_duration,
    invalid_source_attempts,
    invalid_traffic,
    invalid_radio_settings,
    arithmetic_overflow,
};

struct GroupTrafficProfile {
    std::size_t member_count{0};
    std::size_t forwarding_relays{0};
    std::uint64_t duration_ms{0};
    std::uint32_t position_interval_ms{0};
    std::size_t position_payload_bytes{0};
    std::uint32_t status_interval_ms{0};
    std::size_t status_payload_bytes{0};
    std::uint16_t alerts_per_member{0};
    std::size_t alert_payload_bytes{0};
    std::uint8_t source_attempts_per_message{1};
    radio::LoRaAirtimeSettings radio{};
};

struct TrafficClassLoad {
    std::uint64_t logical_messages{0};
    std::uint64_t source_transmissions{0};
    std::uint64_t relay_transmissions{0};
    std::uint64_t airtime_per_transmission_us{0};
    std::uint64_t total_airtime_us{0};
};

struct GroupLoadReport {
    GroupLoadError error{GroupLoadError::invalid_traffic};
    TrafficClassLoad positions{};
    TrafficClassLoad statuses{};
    TrafficClassLoad alerts{};
    std::uint64_t logical_messages{0};
    std::uint64_t source_transmissions{0};
    std::uint64_t relay_transmissions{0};
    std::uint64_t radio_transmissions{0};
    std::uint64_t total_airtime_us{0};
    std::uint64_t channel_utilization_ppm{0};

    [[nodiscard]] constexpr bool estimated() const {
        return error == GroupLoadError::none;
    }
};

[[nodiscard]] GroupLoadReport estimate_group_load(
    const GroupTrafficProfile& profile);

}  // namespace opentrail::simulation
