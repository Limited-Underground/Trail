#pragma once

#include <cstdint>

namespace opentrail::target::heltec_v4_bench::companion_gatt_layout {

// ESP-IDF 6.0.2 registers GAP and standard GATT before the Companion service.
inline constexpr std::uint16_t kServiceChangedValue = 0x0008;
inline constexpr std::uint16_t kService = 0x000e;
inline constexpr std::uint16_t kLastHandle = 0x0018;
inline constexpr std::uint16_t kLegacyStreamValue = 0x0014;  // b4c843b
inline constexpr std::uint16_t kCurrentStreamValue = 0x0015; // 0615e22
inline constexpr std::uint16_t kNoConnection = 0xffff;

constexpr bool matches_current(std::uint16_t service_changed, std::uint16_t service,
                               std::uint16_t protocol, std::uint16_t pairing_feature,
                               std::uint16_t command, std::uint16_t stream,
                               std::uint16_t stream_cccd, std::uint16_t public_info) {
    return service_changed == kServiceChangedValue && service == kService &&
           protocol == 0x0010 && pairing_feature == 0x0011 && command == 0x0013 &&
           stream == kCurrentStreamValue && stream_cccd == 0x0016 &&
           public_info == kLastHandle;
}

// The Stream CCCD is stored against its characteristic value handle. A saved
// subscription for the sole retained bond identifies the layout it used.
class Migration final {
public:
    bool begin(std::uint16_t connection, bool secure_returning_owner,
               bool service_changed_subscribed, bool legacy_stream_subscribed,
               bool current_stream_subscribed) {
        if (connection == kNoConnection || pending_ != kNoConnection ||
            confirmed_ || !secure_returning_owner || !service_changed_subscribed ||
            !legacy_stream_subscribed || current_stream_subscribed) return false;
        pending_ = connection;
        return true;
    }

    bool confirm(std::uint16_t connection, std::uint16_t attribute,
                 bool indication, bool acknowledged) {
        if (pending_ == kNoConnection || connection != pending_ ||
            attribute != kServiceChangedValue || !indication || !acknowledged) return false;
        confirmed_ = true;
        pending_ = kNoConnection;
        return true;
    }

    void disconnect(std::uint16_t connection) {
        if (pending_ == connection) pending_ = kNoConnection;
    }

private:
    std::uint16_t pending_{kNoConnection};
    // For this boot, acknowledgements prevent a rediscovery reconnect loop.
    // The current Stream subscription is the persistent evidence on later boots.
    bool confirmed_{false};
};

}  // namespace opentrail::target::heltec_v4_bench::companion_gatt_layout
