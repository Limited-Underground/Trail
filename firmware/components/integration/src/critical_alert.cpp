#include "opentrail/critical_alert.hpp"

#include <limits>

namespace opentrail::integration {
namespace {

constexpr std::uint8_t kUtcPresentFlag = 0x01;
constexpr std::uint8_t kValuePresentFlag = 0x02;
constexpr std::uint8_t kKnownFlags = kUtcPresentFlag | kValuePresentFlag;
constexpr std::size_t kChecksumOffset = 60;

void write_u32_le(std::uint8_t* destination, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        destination[index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

void write_u64_le(std::uint8_t* destination, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        destination[index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

std::uint32_t read_u32_le(const std::uint8_t* source) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(source[index]) << (index * 8U);
    }
    return value;
}

std::uint64_t read_u64_le(const std::uint8_t* source) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(source[index]) << (index * 8U);
    }
    return value;
}

std::int32_t read_i32_le(const std::uint8_t* source) {
    const auto raw = read_u32_le(source);
    if (raw <= static_cast<std::uint32_t>(
                   std::numeric_limits<std::int32_t>::max())) {
        return static_cast<std::int32_t>(raw);
    }
    return static_cast<std::int32_t>(
        static_cast<std::int64_t>(raw) -
        (static_cast<std::int64_t>(1) << 32U));
}

bool known_type(CriticalAlertType type) {
    switch (type) {
        case CriticalAlertType::engine_over_temperature:
        case CriticalAlertType::oil_pressure_low:
        case CriticalAlertType::charging_failure:
        case CriticalAlertType::rollover_detected:
        case CriticalAlertType::vehicle_immobilized:
        case CriticalAlertType::fuel_critical:
        case CriticalAlertType::generic_critical:
            return true;
    }
    return false;
}

bool known_severity(AlertSeverity severity) {
    switch (severity) {
        case AlertSeverity::warning:
        case AlertSeverity::critical:
        case AlertSeverity::emergency:
            return true;
    }
    return false;
}

bool known_state(AlertState state) {
    switch (state) {
        case AlertState::asserted:
        case AlertState::cleared:
            return true;
    }
    return false;
}

bool known_quality(AlertQuality quality) {
    switch (quality) {
        case AlertQuality::valid:
        case AlertQuality::suspect:
        case AlertQuality::unavailable:
        case AlertQuality::error:
            return true;
    }
    return false;
}

bool known_unit(AlertUnit unit) {
    switch (unit) {
        case AlertUnit::none:
        case AlertUnit::celsius:
        case AlertUnit::kilopascal:
        case AlertUnit::volt:
        case AlertUnit::percent:
        case AlertUnit::revolutions_per_minute:
        case AlertUnit::boolean:
            return true;
    }
    return false;
}

bool quality_allows_value(AlertQuality quality) {
    return quality == AlertQuality::valid || quality == AlertQuality::suspect;
}

bool value_in_range(AlertUnit unit, std::int32_t value) {
    switch (unit) {
        case AlertUnit::none:
            return value == 0;
        case AlertUnit::celsius:
            return value >= -100000 && value <= 250000;
        case AlertUnit::kilopascal:
            return value >= 0 && value <= 2000000;
        case AlertUnit::volt:
            return value >= 0 && value <= 100000;
        case AlertUnit::percent:
            return value >= 0 && value <= 100000;
        case AlertUnit::revolutions_per_minute:
            return value >= 0 && value <= 20000000;
        case AlertUnit::boolean:
            return value == 0 || value == 1000;
    }
    return false;
}

bool type_allows_unit(CriticalAlertType type, AlertUnit unit) {
    switch (type) {
        case CriticalAlertType::engine_over_temperature:
            return unit == AlertUnit::celsius;
        case CriticalAlertType::oil_pressure_low:
            return unit == AlertUnit::kilopascal;
        case CriticalAlertType::charging_failure:
            return unit == AlertUnit::volt || unit == AlertUnit::boolean;
        case CriticalAlertType::rollover_detected:
        case CriticalAlertType::vehicle_immobilized:
            return unit == AlertUnit::boolean;
        case CriticalAlertType::fuel_critical:
            return unit == AlertUnit::percent;
        case CriticalAlertType::generic_critical:
            return unit != AlertUnit::none;
    }
    return false;
}

AlertIngressResult rejected(
    AlertIngressError error,
    const CriticalAlert& alert = {},
    AlertCodecError codec_error = AlertCodecError::none) {
    return {
        AlertIngressDisposition::rejected,
        error,
        codec_error,
        alert,
    };
}

}  // namespace

std::uint32_t alert_crc32(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr && size != 0) {
        return 0;
    }
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const auto mask =
                static_cast<std::uint32_t>(-(static_cast<std::int32_t>(
                    crc & 1U)));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

AlertCodecError validate_critical_alert(const CriticalAlert& alert) {
    if (!known_type(alert.type) || !known_severity(alert.severity) ||
        !known_state(alert.state) || !known_quality(alert.quality) ||
        !known_unit(alert.unit)) {
        return AlertCodecError::unknown_enum;
    }
    if (alert.producer_id == 0 || alert.vehicle_id == 0 ||
        alert.event_id == 0 || alert.condition_id == 0) {
        return AlertCodecError::invalid_identity;
    }
    if (alert.utc_present != (alert.event_time_utc_s != 0)) {
        return AlertCodecError::inconsistent_time;
    }
    if (alert.type == CriticalAlertType::rollover_detected) {
        if (alert.severity != AlertSeverity::emergency) {
            return AlertCodecError::invalid_severity;
        }
    } else if (alert.severity == AlertSeverity::warning) {
        return AlertCodecError::invalid_severity;
    }
    if (alert.state == AlertState::asserted &&
        !quality_allows_value(alert.quality)) {
        return AlertCodecError::inconsistent_quality;
    }
    if (!alert.value_present) {
        if (alert.unit != AlertUnit::none || alert.value_milli != 0) {
            return AlertCodecError::inconsistent_value;
        }
        return AlertCodecError::none;
    }
    if (alert.unit == AlertUnit::none) {
        return AlertCodecError::inconsistent_value;
    }
    if (!quality_allows_value(alert.quality)) {
        return AlertCodecError::inconsistent_quality;
    }
    if (!value_in_range(alert.unit, alert.value_milli)) {
        return AlertCodecError::value_out_of_range;
    }
    if (!type_allows_unit(alert.type, alert.unit)) {
        return AlertCodecError::invalid_type_unit;
    }
    return AlertCodecError::none;
}

AlertEncodeResult encode_critical_alert(
    const CriticalAlert& alert,
    std::array<std::uint8_t, kCriticalAlertFrameBytes>& output) {
    const auto validation = validate_critical_alert(alert);
    if (validation != AlertCodecError::none) {
        return {validation, 0};
    }

    output.fill(0);
    output[0] = 'O';
    output[1] = 'G';
    output[2] = 'A';
    output[3] = '0';
    output[4] = kCriticalAlertSchemaVersion;
    output[5] = static_cast<std::uint8_t>(kCriticalAlertFrameBytes);
    output[6] = static_cast<std::uint8_t>(
        (alert.utc_present ? kUtcPresentFlag : 0U) |
        (alert.value_present ? kValuePresentFlag : 0U));
    output[7] = static_cast<std::uint8_t>(alert.type);
    output[8] = static_cast<std::uint8_t>(alert.severity);
    output[9] = static_cast<std::uint8_t>(alert.state);
    output[10] = static_cast<std::uint8_t>(alert.quality);
    output[11] = static_cast<std::uint8_t>(alert.unit);
    write_u64_le(output.data() + 12, alert.producer_id);
    write_u64_le(output.data() + 20, alert.vehicle_id);
    write_u64_le(output.data() + 28, alert.event_id);
    write_u64_le(output.data() + 36, alert.condition_id);
    write_u32_le(output.data() + 44, alert.event_time_utc_s);
    write_u32_le(output.data() + 48, alert.age_ms);
    write_u32_le(
        output.data() + 52,
        static_cast<std::uint32_t>(alert.value_milli));
    write_u32_le(output.data() + 56, alert.diagnostic_code);
    write_u32_le(
        output.data() + kChecksumOffset,
        alert_crc32(output.data(), kChecksumOffset));
    return {AlertCodecError::none, kCriticalAlertFrameBytes};
}

AlertDecodeResult decode_critical_alert(
    const std::uint8_t* encoded,
    std::size_t size) {
    if (encoded == nullptr) {
        return {AlertCodecError::invalid_argument, {}};
    }
    if (size != kCriticalAlertFrameBytes || encoded[5] != size) {
        return {AlertCodecError::malformed, {}};
    }
    if (encoded[0] != 'O' || encoded[1] != 'G' ||
        encoded[2] != 'A' || encoded[3] != '0') {
        return {AlertCodecError::malformed, {}};
    }
    if (encoded[4] != kCriticalAlertSchemaVersion) {
        return {AlertCodecError::unsupported_version, {}};
    }
    if ((encoded[6] & static_cast<std::uint8_t>(~kKnownFlags)) != 0) {
        return {AlertCodecError::reserved_flags_set, {}};
    }
    if (read_u32_le(encoded + kChecksumOffset) !=
        alert_crc32(encoded, kChecksumOffset)) {
        return {AlertCodecError::integrity_failure, {}};
    }

    CriticalAlert alert{};
    alert.utc_present = (encoded[6] & kUtcPresentFlag) != 0;
    alert.value_present = (encoded[6] & kValuePresentFlag) != 0;
    alert.type = static_cast<CriticalAlertType>(encoded[7]);
    alert.severity = static_cast<AlertSeverity>(encoded[8]);
    alert.state = static_cast<AlertState>(encoded[9]);
    alert.quality = static_cast<AlertQuality>(encoded[10]);
    alert.unit = static_cast<AlertUnit>(encoded[11]);
    alert.producer_id = read_u64_le(encoded + 12);
    alert.vehicle_id = read_u64_le(encoded + 20);
    alert.event_id = read_u64_le(encoded + 28);
    alert.condition_id = read_u64_le(encoded + 36);
    alert.event_time_utc_s = read_u32_le(encoded + 44);
    alert.age_ms = read_u32_le(encoded + 48);
    alert.value_milli = read_i32_le(encoded + 52);
    alert.diagnostic_code = read_u32_le(encoded + 56);

    const auto validation = validate_critical_alert(alert);
    if (validation != AlertCodecError::none) {
        return {validation, {}};
    }
    return {AlertCodecError::none, alert};
}

CriticalAlertIngress::RateEntry* CriticalAlertIngress::rate_entry(
    std::uint64_t producer_id,
    std::uint64_t now_ms) {
    for (auto& entry : rates_) {
        if (entry.used && entry.producer_id == producer_id) {
            return &entry;
        }
    }
    for (auto& entry : rates_) {
        if (!entry.used) {
            entry = {};
            entry.used = true;
            entry.producer_id = producer_id;
            entry.window_start_ms = now_ms;
            entry.last_seen_ms = now_ms;
            return &entry;
        }
    }
    return nullptr;
}

void CriticalAlertIngress::remember(
    const CriticalAlert& alert,
    std::uint32_t fingerprint,
    std::uint64_t now_ms) {
    DuplicateEntry* selected = nullptr;
    for (auto& entry : duplicates_) {
        if (!entry.used) {
            selected = &entry;
            break;
        }
    }
    if (selected == nullptr) {
        selected = &duplicates_[0];
        for (auto& entry : duplicates_) {
            if (entry.seen_ms < selected->seen_ms) {
                selected = &entry;
            }
        }
    }
    *selected = {
        true,
        alert.producer_id,
        alert.vehicle_id,
        alert.event_id,
        fingerprint,
        now_ms,
    };
}

AlertIngressResult CriticalAlertIngress::process(
    const std::uint8_t* encoded,
    std::size_t size,
    const AlertIngressContext& context) {
    const auto decoded = decode_critical_alert(encoded, size);
    if (!decoded.decoded()) {
        return rejected(
            AlertIngressError::codec_rejected,
            {},
            decoded.error);
    }
    const auto& alert = decoded.alert;
    if (!context.authenticated) {
        return rejected(AlertIngressError::unauthenticated, alert);
    }
    if (!context.authorized_to_publish) {
        return rejected(AlertIngressError::unauthorized, alert);
    }
    if (context.authenticated_producer_id != alert.producer_id) {
        return rejected(AlertIngressError::producer_mismatch, alert);
    }
    if (alert.age_ms > kMaximumAlertAgeMs) {
        return rejected(AlertIngressError::stale, alert);
    }
    if (alert.utc_present && context.utc_available) {
        if (alert.event_time_utc_s > context.receive_utc_s) {
            if (alert.event_time_utc_s - context.receive_utc_s >
                kMaximumFutureSkewSeconds) {
                return rejected(AlertIngressError::future_timestamp, alert);
            }
        } else if (context.receive_utc_s - alert.event_time_utc_s >
                   kMaximumUtcAgeSeconds) {
            return rejected(AlertIngressError::stale, alert);
        }
    }

    auto* rate = rate_entry(
        alert.producer_id,
        context.receive_monotonic_ms);
    if (rate == nullptr) {
        return rejected(
            AlertIngressError::producer_capacity_exhausted,
            alert);
    }
    if (context.receive_monotonic_ms < rate->last_seen_ms) {
        return rejected(
            AlertIngressError::monotonic_time_rollback,
            alert);
    }

    const auto fingerprint = read_u32_le(encoded + kChecksumOffset);
    for (auto& entry : duplicates_) {
        if (entry.used &&
            context.receive_monotonic_ms >= entry.seen_ms &&
            context.receive_monotonic_ms - entry.seen_ms >
                kDuplicateRetentionMs) {
            entry.used = false;
        }
        if (!entry.used || entry.producer_id != alert.producer_id ||
            entry.vehicle_id != alert.vehicle_id ||
            entry.event_id != alert.event_id) {
            continue;
        }
        rate->last_seen_ms = context.receive_monotonic_ms;
        if (entry.fingerprint == fingerprint) {
            entry.seen_ms = context.receive_monotonic_ms;
            return {
                AlertIngressDisposition::duplicate,
                AlertIngressError::none,
                AlertCodecError::none,
                alert,
            };
        }
        return rejected(AlertIngressError::duplicate_conflict, alert);
    }

    if (context.receive_monotonic_ms - rate->window_start_ms >=
        kRateWindowMs) {
        rate->window_start_ms = context.receive_monotonic_ms;
        rate->general_count = 0;
        rate->emergency_reserve_count = 0;
    }

    if (rate->general_count < kGeneralRateAllowance) {
        ++rate->general_count;
    } else if (
        alert.severity == AlertSeverity::emergency &&
        rate->emergency_reserve_count < kEmergencyRateReserve) {
        ++rate->emergency_reserve_count;
    } else {
        rate->last_seen_ms = context.receive_monotonic_ms;
        return rejected(AlertIngressError::rate_limited, alert);
    }

    rate->last_seen_ms = context.receive_monotonic_ms;
    remember(alert, fingerprint, context.receive_monotonic_ms);
    return {
        AlertIngressDisposition::accepted,
        AlertIngressError::none,
        AlertCodecError::none,
        alert,
    };
}

}  // namespace opentrail::integration
