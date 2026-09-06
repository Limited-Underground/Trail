#pragma once
#include "opentrail/companion_configuration_dispatcher.hpp"

namespace opentrail::target::heltec_v4_bench {
// Pure fixed-memory lane used by the real GATT/app handoff. External short
// serialization protects it; no dispatcher or persistence call occurs here.
struct ConfigurationLane {
    bool occupied{false}, executing{false}, indicated{false}, response_ready{false};
    companion::DeviceNameContext context{};
    std::uint16_t connection{0xffff};
    std::uint64_t token{0}, admitted_ms{0};
    std::uint32_t exchange{0};
    std::size_t bytes{0};
    std::array<std::uint8_t, companion::kConfigurationRecordBytes> record{};
    [[nodiscard]] bool current(const companion::DeviceNameAuthority& authority) const {
        return occupied && authority.phase == companion::DeviceNamePhase::connected &&
            authority.context == context;
    }
    [[nodiscard]] bool expired(std::uint64_t now) const {
        return occupied && (now < admitted_ms || now - admitted_ms >= 5000);
    }
    [[nodiscard]] bool can_execute(const companion::DeviceNameAuthority& authority) const {
        return current(authority) && !executing && !indicated && !response_ready &&
            !expired(authority.now_ms);
    }
    [[nodiscard]] bool matches(std::uint16_t handle, std::uint64_t generation,
        std::uint32_t nonce, std::uint32_t id, std::uint64_t delivery) const {
        return occupied && connection == handle && context.transport_generation == generation &&
            context.session_nonce == nonce && exchange == id && token == delivery;
    }
};
} // namespace opentrail::target::heltec_v4_bench
