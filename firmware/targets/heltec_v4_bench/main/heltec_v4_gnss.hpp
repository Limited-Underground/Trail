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

enum class GnssFixState : std::uint8_t {
    unavailable = 0,
    valid,
    no_fix,
    stale,
    invalid,
};

struct GnssFixObservation {
    GnssFixState state{GnssFixState::unavailable};
    std::uint64_t sampled_at_ms{0};
};

enum class NmeaIngestResult : std::uint8_t {
    none = 0,
    observation_accepted,
    sentence_rejected,
};

// Streaming, allocation-free GGA/GNS observer. It retains coordinate digits
// only while parsing a bounded sentence, then clears them on completion,
// rejection or a one-second partial-sentence timeout. It retains no completed
// position, time, altitude, accuracy, or raw sentence. Only a
// checksum-valid satellite count in the range 0..99 and a separate fix state
// can become observable. A satellite count alone never establishes a fix.
class NmeaSatelliteObserver {
public:
    [[nodiscard]] NmeaIngestResult ingest(
        std::uint8_t byte,
        std::uint64_t received_at_ms);

    [[nodiscard]] GnssSatelliteObservation snapshot(
        std::uint64_t now_ms,
        std::uint64_t fresh_for_ms) const;

    [[nodiscard]] GnssFixObservation fix(
        std::uint64_t now_ms,
        std::uint64_t fresh_for_ms) const;

    void expire_partial(std::uint64_t now_ms);
    void reset();

private:
    struct CoordinateProbe {
        std::uint16_t degrees{0};
        std::uint8_t minutes{0};
        std::uint8_t whole_digits{0};
        std::uint8_t fractional_digits{0};
        bool decimal{false};
        bool fractional_nonzero{false};
        bool invalid{false};

        void consume(char value, std::uint8_t degree_digits);
        [[nodiscard]] bool valid(std::uint8_t degree_digits,
                                 std::uint16_t max_degrees) const;
    };

    enum class ParseState : std::uint8_t {
        waiting_for_start = 0,
        body,
        checksum_high,
        checksum_low,
        line_end,
        discard,
    };

    void begin_sentence();
    void clear_candidate_position();
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
    std::uint8_t fix_field_bytes_{0};
    bool candidate_fix_{false};
    bool candidate_non_gnss_mode_{false};
    CoordinateProbe latitude_{};
    CoordinateProbe longitude_{};
    std::uint8_t north_south_bytes_{0};
    std::uint8_t east_west_bytes_{0};
    bool north_south_valid_{false};
    bool east_west_valid_{false};
    bool candidate_supported_{false};
    bool candidate_invalid_{false};
    bool saw_carriage_return_{false};
    bool has_observation_{false};
    bool fix_valid_{false};
    std::uint8_t satellites_{0};
    std::uint64_t sampled_at_ms_{0};
    std::uint64_t last_byte_at_ms_{0};
};

// Thin target adapter for the received Heltec WiFi LoRa 32 V4.2 wiring. Its
// public surface exposes only the privacy-safe satellite observation above.
class HeltecV4Gnss {
public:
    struct Diagnostics {
        std::uint64_t bytes_received{0};
        std::uint64_t accepted_sentences{0};
        std::uint64_t rejected_sentences{0};
        std::uint64_t uart_read_errors{0};
    };

    [[nodiscard]] bool initialize();
    void service(std::uint64_t now_ms);

    [[nodiscard]] bool initialized() const { return initialized_; }
    [[nodiscard]] Diagnostics diagnostics() const { return diagnostics_; }
    [[nodiscard]] GnssSatelliteObservation satellites(
        std::uint64_t now_ms,
        std::uint64_t fresh_for_ms) const {
        return observer_.snapshot(now_ms, fresh_for_ms);
    }

    [[nodiscard]] GnssFixObservation fix(
        std::uint64_t now_ms,
        std::uint64_t fresh_for_ms) const {
        return observer_.fix(now_ms, fresh_for_ms);
    }

private:
    NmeaSatelliteObserver observer_{};
    Diagnostics diagnostics_{};
    bool attempted_{false};
    bool initialized_{false};
};

}  // namespace opentrail::target::heltec_v4_bench
