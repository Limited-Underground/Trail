#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::integration {

inline constexpr std::uint8_t kCriticalAlertSchemaVersion = 0;
inline constexpr std::size_t kCriticalAlertFrameBytes = 64;
inline constexpr std::uint32_t kMaximumAlertAgeMs = 120000;
inline constexpr std::uint32_t kMaximumUtcAgeSeconds = 120;
inline constexpr std::uint32_t kMaximumFutureSkewSeconds = 30;
inline constexpr std::uint64_t kDuplicateRetentionMs = 600000;
inline constexpr std::uint64_t kRateWindowMs = 10000;
inline constexpr std::uint8_t kGeneralRateAllowance = 4;
inline constexpr std::uint8_t kEmergencyRateReserve = 2;
inline constexpr std::size_t kDuplicateCapacity = 16;
inline constexpr std::size_t kRateProducerCapacity = 8;

enum class CriticalAlertType : std::uint8_t {
    engine_over_temperature = 1,
    oil_pressure_low = 2,
    charging_failure = 3,
    rollover_detected = 4,
    vehicle_immobilized = 5,
    fuel_critical = 6,
    generic_critical = 7,
};

enum class AlertSeverity : std::uint8_t {
    warning = 1,
    critical = 2,
    emergency = 3,
};

enum class AlertState : std::uint8_t {
    asserted = 1,
    cleared = 2,
};

enum class AlertQuality : std::uint8_t {
    valid = 1,
    suspect = 2,
    unavailable = 3,
    error = 4,
};

enum class AlertUnit : std::uint8_t {
    none = 0,
    celsius = 1,
    kilopascal = 2,
    volt = 3,
    percent = 4,
    revolutions_per_minute = 5,
    boolean = 6,
};

struct CriticalAlert {
    CriticalAlertType type{CriticalAlertType::generic_critical};
    AlertSeverity severity{AlertSeverity::critical};
    AlertState state{AlertState::asserted};
    AlertQuality quality{AlertQuality::valid};
    AlertUnit unit{AlertUnit::none};
    std::uint64_t producer_id{0};
    std::uint64_t vehicle_id{0};
    std::uint64_t event_id{0};
    std::uint64_t condition_id{0};
    std::uint32_t event_time_utc_s{0};
    std::uint32_t age_ms{0};
    std::int32_t value_milli{0};
    std::uint32_t diagnostic_code{0};
    bool utc_present{false};
    bool value_present{false};
};

enum class AlertCodecError : std::uint8_t {
    none = 0,
    invalid_argument,
    malformed,
    unsupported_version,
    reserved_flags_set,
    integrity_failure,
    unknown_enum,
    invalid_identity,
    inconsistent_time,
    inconsistent_value,
    inconsistent_quality,
    value_out_of_range,
    invalid_type_unit,
    invalid_severity,
};

struct AlertEncodeResult {
    AlertCodecError error{AlertCodecError::invalid_argument};
    std::size_t encoded_bytes{0};

    [[nodiscard]] constexpr bool encoded() const {
        return error == AlertCodecError::none;
    }
};

struct AlertDecodeResult {
    AlertCodecError error{AlertCodecError::malformed};
    CriticalAlert alert{};

    [[nodiscard]] constexpr bool decoded() const {
        return error == AlertCodecError::none;
    }
};

[[nodiscard]] std::uint32_t alert_crc32(
    const std::uint8_t* data,
    std::size_t size);
[[nodiscard]] AlertCodecError validate_critical_alert(
    const CriticalAlert& alert);
[[nodiscard]] AlertEncodeResult encode_critical_alert(
    const CriticalAlert& alert,
    std::array<std::uint8_t, kCriticalAlertFrameBytes>& output);
[[nodiscard]] AlertDecodeResult decode_critical_alert(
    const std::uint8_t* encoded,
    std::size_t size);

struct AlertIngressContext {
    bool authenticated{false};
    bool authorized_to_publish{false};
    std::uint64_t authenticated_producer_id{0};
    std::uint64_t receive_monotonic_ms{0};
    bool utc_available{false};
    std::uint32_t receive_utc_s{0};
};

enum class AlertIngressDisposition : std::uint8_t {
    accepted = 0,
    duplicate,
    rejected,
};

enum class AlertIngressError : std::uint8_t {
    none = 0,
    codec_rejected,
    unauthenticated,
    unauthorized,
    producer_mismatch,
    stale,
    future_timestamp,
    duplicate_conflict,
    rate_limited,
    producer_capacity_exhausted,
    monotonic_time_rollback,
};

struct AlertIngressResult {
    AlertIngressDisposition disposition{AlertIngressDisposition::rejected};
    AlertIngressError error{AlertIngressError::codec_rejected};
    AlertCodecError codec_error{AlertCodecError::none};
    CriticalAlert alert{};

    [[nodiscard]] constexpr bool accepted() const {
        return disposition == AlertIngressDisposition::accepted;
    }
};

class CriticalAlertIngress {
public:
    [[nodiscard]] AlertIngressResult process(
        const std::uint8_t* encoded,
        std::size_t size,
        const AlertIngressContext& context);

private:
    struct DuplicateEntry {
        bool used{false};
        std::uint64_t producer_id{0};
        std::uint64_t vehicle_id{0};
        std::uint64_t event_id{0};
        std::uint32_t fingerprint{0};
        std::uint64_t seen_ms{0};
    };

    struct RateEntry {
        bool used{false};
        std::uint64_t producer_id{0};
        std::uint64_t window_start_ms{0};
        std::uint64_t last_seen_ms{0};
        std::uint8_t general_count{0};
        std::uint8_t emergency_reserve_count{0};
    };

    [[nodiscard]] RateEntry* rate_entry(
        std::uint64_t producer_id,
        std::uint64_t now_ms);
    void remember(
        const CriticalAlert& alert,
        std::uint32_t fingerprint,
        std::uint64_t now_ms);

    std::array<DuplicateEntry, kDuplicateCapacity> duplicates_{};
    std::array<RateEntry, kRateProducerCapacity> rates_{};
};

}  // namespace opentrail::integration
