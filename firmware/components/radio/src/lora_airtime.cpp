#include "opentrail/lora_airtime.hpp"

namespace opentrail::radio {
namespace {

std::uint64_t divide_round_up(std::uint64_t numerator, std::uint64_t divisor) {
    return numerator / divisor + (numerator % divisor == 0 ? 0U : 1U);
}

}  // namespace

LoRaAirtimeResult calculate_lora_airtime(
    const LoRaAirtimeSettings& settings,
    std::size_t payload_bytes) {
    if (settings.bandwidth_hz == 0 || settings.preamble_symbols == 0 ||
        settings.spreading_factor < 5 || settings.spreading_factor > 12 ||
        settings.coding_rate_denominator < 5 ||
        settings.coding_rate_denominator > 8) {
        return {LoRaAirtimeError::invalid_settings, 0, 0};
    }
    if (payload_bytes > 255) {
        return {LoRaAirtimeError::payload_too_large, 0, 0};
    }

    const auto spreading_factor =
        static_cast<std::int32_t>(settings.spreading_factor);
    const auto denominator = 4 *
        (spreading_factor -
         (settings.low_data_rate_optimization ? 2 : 0));
    if (denominator <= 0) {
        return {LoRaAirtimeError::invalid_settings, 0, 0};
    }

    const auto numerator =
        8 * static_cast<std::int32_t>(payload_bytes) -
        4 * spreading_factor + 28 +
        (settings.payload_crc ? 16 : 0) -
        (settings.explicit_header ? 0 : 20);
    std::uint16_t payload_symbols = 8;
    if (numerator > 0) {
        const auto symbol_blocks =
            (numerator + denominator - 1) / denominator;
        payload_symbols = static_cast<std::uint16_t>(
            payload_symbols +
            symbol_blocks * settings.coding_rate_denominator);
    }

    // Preamble includes 4.25 symbols. Quarter-symbol arithmetic keeps the
    // result deterministic without floating point.
    const auto total_quarter_symbols =
        static_cast<std::uint64_t>(settings.preamble_symbols) * 4U + 17U +
        static_cast<std::uint64_t>(payload_symbols) * 4U;
    const auto symbol_scale =
        static_cast<std::uint64_t>(1) << settings.spreading_factor;
    const auto airtime_numerator =
        total_quarter_symbols * symbol_scale * 1000000U;
    const auto airtime_denominator =
        static_cast<std::uint64_t>(settings.bandwidth_hz) * 4U;
    return {
        LoRaAirtimeError::none,
        divide_round_up(airtime_numerator, airtime_denominator),
        payload_symbols,
    };
}

}  // namespace opentrail::radio
