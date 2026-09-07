#pragma once
#include "opentrail/companion_configuration_dispatcher.hpp"
#include "opentrail/companion_semantics.hpp"

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
// Render-only proof of an acknowledged protected Snapshot for this exact
// authority. It grants no command or radio permission.
class ConfigurationPhoneStatus {
public:
    void clear() { ready_ = false; context_ = {}; }
    void observe(const companion::DeviceNameAuthority& authority) {
        if (authority.phase != companion::DeviceNamePhase::connected ||
            (ready_ && authority.context != context_)) clear();
    }
    void complete(const ConfigurationLane& lane,
                  const companion::DeviceNameAuthority& authority,
                  bool confirmed, std::uint64_t now_ms) {
        observe(authority);
        if (!confirmed || !lane.indicated || !lane.current(authority) || lane.expired(now_ms)) {
            clear();
            return;
        }
        const auto frame = companion::decode_configuration_frame(
            lane.record.data(), lane.bytes, 3);
        if (!frame.decoded() || frame.value.kind != 0x81 ||
            frame.value.session_nonce != lane.context.session_nonce ||
            frame.value.exchange_id != lane.exchange) return;
        const auto snapshot = companion::decode_companion_status_snapshot(
            {frame.value.payload.data(), frame.value.payload_bytes});
        if (!snapshot.decoded()) return;
        context_ = lane.context;
        ready_ = true;
    }
    [[nodiscard]] bool ready(const companion::DeviceNameAuthority& authority,
                             std::uint64_t generation) const {
        return ready_ && generation != 0 &&
            authority.phase == companion::DeviceNamePhase::connected &&
            authority.context == context_ && context_.transport_generation == generation;
    }
private:
    bool ready_{false};
    companion::DeviceNameContext context_{};
};
} // namespace opentrail::target::heltec_v4_bench
