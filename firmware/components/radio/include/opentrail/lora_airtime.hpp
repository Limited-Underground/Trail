#pragma once

#include <cstddef>
#include <cstdint>

namespace opentrail::radio {

struct LoRaAirtimeSettings {
    std::uint32_t bandwidth_hz{0};
    std::uint16_t preamble_symbols{8};
    std::uint8_t spreading_factor{0};
    std::uint8_t coding_rate_denominator{5};
    bool explicit_header{true};
    bool payload_crc{true};
    bool low_data_rate_optimization{false};
};

enum class LoRaAirtimeError : std::uint8_t {
    none = 0,
    invalid_settings,
    payload_too_large,
};

struct LoRaAirtimeResult {
    LoRaAirtimeError error{LoRaAirtimeError::invalid_settings};
    std::uint64_t airtime_us{0};
    std::uint16_t payload_symbols{0};

    [[nodiscard]] constexpr bool calculated() const {
        return error == LoRaAirtimeError::none;
    }
};

[[nodiscard]] LoRaAirtimeResult calculate_lora_airtime(
    const LoRaAirtimeSettings& settings,
    std::size_t payload_bytes);

}  // namespace opentrail::radio
