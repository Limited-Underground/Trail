#pragma once

// Bench LoRa link test (V1 checklist F5, step R1), built only with
// OPENTRAIL_LORA_LINK_TEST. Each board broadcasts a small unauthenticated beacon
// and logs the beacons it hears, proving the product firmware sends and receives
// over LoRa alongside Bluetooth. Not a messaging protocol: no identity, no
// encryption, no acknowledgement. Header-only so the host tests can use it.

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::target::heltec_v4_bench::lora_link_test {

inline constexpr std::array<std::uint8_t, 4> kMagic{'O', 'T', 'L', 'T'};
inline constexpr std::uint8_t kVersion = 1;
// magic(4) version(1) sender(4) sequence(4) heard_from(4) heard_sequence(4)
inline constexpr std::size_t kBeaconBytes = 21;
inline constexpr std::uint64_t kBasePeriodMs = 5000;

struct Beacon {
    std::uint32_t sender{0};
    std::uint32_t sequence{0};
    // The last beacon this sender heard from another board (0 = none yet),
    // so one log line shows both directions.
    std::uint32_t heard_from{0};
    std::uint32_t heard_sequence{0};
};

namespace detail {
inline void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}
inline std::uint32_t get32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}
}  // namespace detail

inline std::array<std::uint8_t, kBeaconBytes> encode(const Beacon& b) {
    std::array<std::uint8_t, kBeaconBytes> out{};
    for (std::size_t i = 0; i < kMagic.size(); ++i) out[i] = kMagic[i];
    out[4] = kVersion;
    detail::put32(&out[5], b.sender);
    detail::put32(&out[9], b.sequence);
    detail::put32(&out[13], b.heard_from);
    detail::put32(&out[17], b.heard_sequence);
    return out;
}

// Exact size, magic and version; sender and sequence must be nonzero.
inline bool decode(const std::uint8_t* data, std::size_t size, Beacon& out) {
    if (data == nullptr || size != kBeaconBytes) return false;
    for (std::size_t i = 0; i < kMagic.size(); ++i) {
        if (data[i] != kMagic[i]) return false;
    }
    if (data[4] != kVersion) return false;
    Beacon b{detail::get32(&data[5]), detail::get32(&data[9]),
             detail::get32(&data[13]), detail::get32(&data[17])};
    if (b.sender == 0 || b.sequence == 0) return false;
    out = b;
    return true;
}

// Per-board period so two boards drift apart instead of colliding every time.
inline std::uint64_t period_ms(std::uint32_t sender) {
    return kBasePeriodMs + (sender % 997U);
}

class Beaconer {
public:
    explicit Beaconer(std::uint32_t self) : self_(self == 0 ? 1 : self) {}

    // Returns true when a beacon is due; fills `out` and advances the schedule.
    bool due(std::uint64_t now_ms, Beacon& out) {
        if (now_ms < next_ms_) return false;
        next_ms_ = now_ms + period_ms(self_);
        out = {self_, ++sequence_, heard_from_, heard_sequence_};
        return true;
    }
    // Records a decoded beacon; own echoes (same sender) are ignored.
    bool heard(const Beacon& b) {
        if (b.sender == self_) return false;
        heard_from_ = b.sender;
        heard_sequence_ = b.sequence;
        ++heard_count_;
        return true;
    }
    [[nodiscard]] std::uint32_t self() const { return self_; }
    [[nodiscard]] std::uint32_t heard_count() const { return heard_count_; }

private:
    std::uint32_t self_;
    std::uint32_t sequence_{0};
    std::uint32_t heard_from_{0};
    std::uint32_t heard_sequence_{0};
    std::uint32_t heard_count_{0};
    std::uint64_t next_ms_{0};
};

}  // namespace opentrail::target::heltec_v4_bench::lora_link_test
