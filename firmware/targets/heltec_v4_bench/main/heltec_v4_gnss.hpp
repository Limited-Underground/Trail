#pragma once

#include <cstddef>
#include <cstdint>

namespace opentrail::target::heltec_v4_bench {

inline constexpr int kHeltecV4GnssEnableGpio = 34;
inline constexpr int kHeltecV4GnssResetGpio = 42;
inline constexpr int kHeltecV4GnssUartNumber = 1;
inline constexpr int kHeltecV4GnssRxGpio = 39;
inline constexpr int kHeltecV4GnssTxGpio = 38;
inline constexpr std::uint32_t kHeltecV4GnssBaud = 9'600;
inline constexpr int kHeltecV4GnssEnableLevel = 0;
inline constexpr int kHeltecV4GnssInactiveLevel = 1;
inline constexpr int kHeltecV4GnssResetAssertedLevel = 0;
inline constexpr int kHeltecV4GnssResetReleasedLevel = 1;
inline constexpr std::size_t kHeltecV4MaxNmeaSentenceBytes = 82;

enum class GnssSatelliteState : std::uint8_t {
    unavailable = 0,
    valid,
    stale,
    invalid,
};

struct GnssSatelliteObservation {
    GnssSatelliteState state{GnssSatelliteState::unavailable};
    std::uint8_t satellites{0};
    std::uint64_t sampled_at_ms{0};

    [[nodiscard]] constexpr bool fresh() const {
        return state == GnssSatelliteState::valid;
    }
};

enum class NmeaIngestResult : std::uint8_t {
    none = 0,
    observation_accepted,
    sentence_rejected,
};

// Streaming, allocation-free GGA/GNS observer. It deliberately retains no
// position, time, altitude, accuracy, talker identity, or raw sentence. Only a
// checksum-valid satellite count in the range 0..99 can become observable.
class NmeaSatelliteObserver {
public:
    [[nodiscard]] NmeaIngestResult ingest(
        std::uint8_t byte,
        std::uint64_t received_at_ms);

    [[nodiscard]] GnssSatelliteObservation snapshot(
        std::uint64_t now_ms,
        std::uint64_t fresh_for_ms) const;

    void reset();

private:
    enum class ParseState : std::uint8_t {
        waiting_for_start = 0,
        body,
        checksum_high,
        checksum_low,
        line_end,
        discard,
    };

    void begin_sentence();
    void consume_body_character(char value);
    void finish_field();
    [[nodiscard]] bool candidate_complete() const;
    [[nodiscard]] NmeaIngestResult reject_sentence();

    ParseState parse_state_{ParseState::waiting_for_start};
    std::size_t sentence_bytes_{0};
    std::uint8_t checksum_{0};
    std::uint8_t expected_checksum_{0};
    std::uint8_t field_index_{0};
    char sentence_kind_[5]{};
    std::uint8_t sentence_kind_bytes_{0};
    std::uint8_t candidate_satellites_{0};
    std::uint8_t satellite_digits_{0};
    bool candidate_supported_{false};
    bool candidate_invalid_{false};
    bool saw_carriage_return_{false};
    bool has_observation_{false};
    std::uint8_t satellites_{0};
    std::uint64_t sampled_at_ms_{0};
};

// Thin target adapter for the received Heltec WiFi LoRa 32 V4.2 wiring. Its
// public surface exposes only the privacy-safe satellite observation above.
class HeltecV4Gnss {
public:
    [[nodiscard]] bool initialize();
    void service(std::uint64_t now_ms);

    [[nodiscard]] bool initialized() const { return initialized_; }
    [[nodiscard]] GnssSatelliteObservation satellites(
        std::uint64_t now_ms,
        std::uint64_t fresh_for_ms) const {
        return observer_.snapshot(now_ms, fresh_for_ms);
    }

private:
    NmeaSatelliteObserver observer_{};
    bool attempted_{false};
    bool initialized_{false};
};

}  // namespace opentrail::target::heltec_v4_bench
