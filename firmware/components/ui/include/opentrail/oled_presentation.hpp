#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "opentrail/oled_clock.hpp"

namespace opentrail::ui::oled_presentation {

constexpr std::size_t kWidth = 128;
constexpr std::size_t kHeight = 64;
constexpr std::size_t kRows = 8;
constexpr std::size_t kCharacters = 21;
constexpr std::uint64_t kMetricFreshnessMs = 30'000;
constexpr std::uint64_t kActivityPulseMs = 1'000;
constexpr std::uint64_t kPairingWindowMaxMs = 60'000;

enum class Surface : std::uint8_t {
    failure, reset_progress, reset_confirmation, pairing, region_required, normal,
};
enum class PhoneState : std::uint8_t { unknown, disconnected, reconnecting, ready };
enum class GroupState : std::uint8_t { unknown, none, member, administrator };
enum class GpsFix : std::uint8_t { unknown, no_fix, fix };

struct Metric {
    bool valid{false};
    std::uint16_t value{0};
    std::uint64_t sampled_at_ms{0};
};
struct Pulse {
    bool valid{false};
    std::uint64_t at_ms{0};
};
struct PairingWindow {
    bool active{false};
    std::uint64_t start_ms{0};
    std::uint64_t deadline_ms{0};
    std::array<char, 6> digits{};
    std::string_view setup_label{};
};

/** Borrowed labels are replaceable display metadata, never identifiers or authority. */
struct Snapshot {
    bool failure{false};
    bool reset_in_progress{false};
    bool reset_confirmation{false};
    PairingWindow pairing{};
    std::string_view device_name{};
    PhoneState phone{PhoneState::unknown};
    GroupState group{GroupState::unknown};
    bool location_available{false};
    bool location_on{false};
    bool region_configured{false};
    std::string_view region_label{};
    bool tx_available{false};
    Metric battery{};
    Metric satellites{};
    GpsFix gps_fix{GpsFix::unknown};
    std::uint64_t gps_sampled_at_ms{0};
    Pulse tx{};
    Pulse rx{};
    // Caller obtains clock.observe(now_ms) for this coherent snapshot. The renderer
    // checks its numeric/text structure but cannot establish age from a reading.
    opentrail::time::OledClockReading clock{};
};

struct Frame {
    Surface surface{Surface::region_required};
    std::array<std::array<char, kCharacters + 1>, kRows> rows{};
    // SSD1306 page order: byte=(y/8)*128+x, bit=y%8. Final 3 columns stay blank.
    std::array<std::uint8_t, kWidth * kHeight / 8> pixels{};
    bool clipped{false};
};

// Normal rows: name, phone, group/location, region/TX, battery/satellites,
// GPS fix, radio pulses, clock. Exclusive surfaces leave unused rows blank.
// Each call creates an independent frame; caller owns its lifetime and display I/O.
// Pure renderer requires an independently checked nondecreasing time source;
// normal consumers use PresentationOwner to contain rollback across calls.
[[nodiscard]] Frame render(const Snapshot& snapshot, std::uint64_t now_ms);

/** Retains only monotonic-time admission, never snapshots, labels or pairing digits. */
class PresentationOwner {
public:
    [[nodiscard]] Frame present(const Snapshot& snapshot, std::uint64_t now_ms);

private:
    bool has_observed_{false};
    std::uint64_t last_ms_{0};
    bool contained_{false};
};

}  // namespace opentrail::ui::oled_presentation
