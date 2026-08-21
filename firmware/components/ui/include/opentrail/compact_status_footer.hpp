#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::ui::compact_status_footer {

constexpr std::size_t kWidth = 128;
constexpr std::size_t kHeight = 8;
constexpr std::size_t kGlyphWidth = 5;

enum class ObservationState : std::uint8_t {
    unavailable = 0,
    valid,
    invalid,
};

struct Metric {
    ObservationState state{ObservationState::unavailable};
    std::uint16_t value{0};
    std::uint64_t sampled_at_ms{0};
};

enum class BleCode : std::uint8_t {
    unavailable = 0,
    starting,
    advertising,
    connected,
    retrying,
    error,
};

enum class Direction : std::uint8_t {
    none = 0,
    tx,
    rx,
};

struct Freshness {
    std::uint64_t battery_fresh_for_ms{0};
    std::uint64_t gps_fresh_for_ms{0};
};

struct Snapshot {
    Metric battery_percent{};
    Metric gps_satellites{};
    BleCode ble{BleCode::unavailable};
};

struct Fields {
    std::array<char, 8> battery{};
    std::array<char, 6> gps{};
    std::array<char, 5> ble{};
};

struct Page {
    std::array<std::uint8_t, kWidth> columns{};
};

[[nodiscard]] Fields format(
    const Snapshot& snapshot,
    Freshness freshness,
    std::uint64_t now_ms);

class ActivityOwner {
public:
    ActivityOwner(bool transport_supported, std::uint64_t visible_for_ms);

    // A future target adapter may call this only after its real LoRa
    // transport definitively accepts an event. That adapter trust seam is
    // not implemented or accepted by this host-only component.
    [[nodiscard]] bool observe_accepted_transport_event(
        Direction direction,
        std::uint64_t at_ms);
    [[nodiscard]] Direction visible_direction(std::uint64_t now_ms) const;
    void clear();

private:
    bool transport_supported_{false};
    std::uint64_t visible_for_ms_{0};
    bool event_valid_{false};
    Direction direction_{Direction::none};
    std::uint64_t event_at_ms_{0};
};

[[nodiscard]] Page render(
    const Fields& fields,
    const ActivityOwner& activity,
    std::uint64_t now_ms);

}  // namespace opentrail::ui::compact_status_footer
