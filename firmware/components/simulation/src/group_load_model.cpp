#include "opentrail/group_load_model.hpp"

#include <limits>

#include "opentrail/group_access_controller.hpp"

namespace opentrail::simulation {
namespace {

bool checked_add(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& output) {
    if (left > std::numeric_limits<std::uint64_t>::max() - right) {
        return false;
    }
    output = left + right;
    return true;
}

bool checked_multiply(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& output) {
    if (left != 0 &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    output = left * right;
    return true;
}

bool valid_stream(
    std::uint32_t interval_ms,
    std::size_t payload_bytes) {
    return interval_ms == 0
        ? payload_bytes == 0
        : payload_bytes > 0 && payload_bytes <= 255;
}

bool build_class_load(
    const GroupTrafficProfile& profile,
    std::uint64_t logical_messages,
    std::size_t payload_bytes,
    TrafficClassLoad& output) {
    if (logical_messages == 0) {
        output = {};
        return true;
    }

    const auto airtime =
        radio::calculate_lora_airtime(profile.radio, payload_bytes);
    if (!airtime.calculated()) {
        return false;
    }

    std::uint64_t source_transmissions = 0;
    std::uint64_t relay_transmissions = 0;
    std::uint64_t radio_transmissions = 0;
    std::uint64_t total_airtime_us = 0;
    if (!checked_multiply(
            logical_messages,
            profile.source_attempts_per_message,
            source_transmissions) ||
        !checked_multiply(
            source_transmissions,
            profile.forwarding_relays,
            relay_transmissions) ||
        !checked_add(
            source_transmissions,
            relay_transmissions,
            radio_transmissions) ||
        !checked_multiply(
            radio_transmissions,
            airtime.airtime_us,
            total_airtime_us)) {
        return false;
    }

    output = {
        logical_messages,
        source_transmissions,
        relay_transmissions,
        airtime.airtime_us,
        total_airtime_us,
    };
    return true;
}

bool add_class(
    const TrafficClassLoad& load,
    GroupLoadReport& report) {
    std::uint64_t next = 0;
    if (!checked_add(
            report.logical_messages, load.logical_messages, next)) {
        return false;
    }
    report.logical_messages = next;
    if (!checked_add(
            report.source_transmissions,
            load.source_transmissions,
            next)) {
        return false;
    }
    report.source_transmissions = next;
    if (!checked_add(
            report.relay_transmissions,
            load.relay_transmissions,
            next)) {
        return false;
    }
    report.relay_transmissions = next;
    if (!checked_add(
            report.total_airtime_us, load.total_airtime_us, next)) {
        return false;
    }
    report.total_airtime_us = next;
    return true;
}

}  // namespace

GroupLoadReport estimate_group_load(const GroupTrafficProfile& profile) {
    GroupLoadReport report{};
    if (profile.member_count == 0 ||
        profile.member_count > identity::kMaximumGroupMembers) {
        report.error = GroupLoadError::invalid_member_count;
        return report;
    }
    if (profile.forwarding_relays > identity::kMaximumGroupMembers) {
        report.error = GroupLoadError::invalid_relay_count;
        return report;
    }
    if (profile.duration_ms == 0 ||
        profile.duration_ms > kMaximumScenarioDurationMs) {
        report.error = GroupLoadError::invalid_duration;
        return report;
    }
    if (profile.source_attempts_per_message == 0 ||
        profile.source_attempts_per_message > kMaximumSourceAttempts) {
        report.error = GroupLoadError::invalid_source_attempts;
        return report;
    }
    if (!valid_stream(
            profile.position_interval_ms,
            profile.position_payload_bytes) ||
        !valid_stream(
            profile.status_interval_ms,
            profile.status_payload_bytes) ||
        (profile.alerts_per_member == 0
             ? profile.alert_payload_bytes != 0
             : profile.alert_payload_bytes == 0 ||
                   profile.alert_payload_bytes > 255)) {
        report.error = GroupLoadError::invalid_traffic;
        return report;
    }
    if (!radio::calculate_lora_airtime(profile.radio, 0).calculated()) {
        report.error = GroupLoadError::invalid_radio_settings;
        return report;
    }

    std::uint64_t position_messages = 0;
    std::uint64_t status_messages = 0;
    std::uint64_t alert_messages = 0;
    if (profile.position_interval_ms != 0 &&
        !checked_multiply(
            profile.duration_ms / profile.position_interval_ms,
            profile.member_count,
            position_messages)) {
        report.error = GroupLoadError::arithmetic_overflow;
        return report;
    }
    if (profile.status_interval_ms != 0 &&
        !checked_multiply(
            profile.duration_ms / profile.status_interval_ms,
            profile.member_count,
            status_messages)) {
        report.error = GroupLoadError::arithmetic_overflow;
        return report;
    }
    if (!checked_multiply(
            profile.alerts_per_member,
            profile.member_count,
            alert_messages)) {
        report.error = GroupLoadError::arithmetic_overflow;
        return report;
    }

    if (!build_class_load(
            profile,
            position_messages,
            profile.position_payload_bytes,
            report.positions) ||
        !build_class_load(
            profile,
            status_messages,
            profile.status_payload_bytes,
            report.statuses) ||
        !build_class_load(
            profile,
            alert_messages,
            profile.alert_payload_bytes,
            report.alerts) ||
        !add_class(report.positions, report) ||
        !add_class(report.statuses, report) ||
        !add_class(report.alerts, report) ||
        !checked_add(
            report.source_transmissions,
            report.relay_transmissions,
            report.radio_transmissions)) {
        report = {};
        report.error = GroupLoadError::arithmetic_overflow;
        return report;
    }

    const auto duration_us = profile.duration_ms * 1000U;
    const auto whole = report.total_airtime_us / duration_us;
    const auto remainder = report.total_airtime_us % duration_us;
    std::uint64_t whole_ppm = 0;
    std::uint64_t remainder_ppm_numerator = 0;
    if (!checked_multiply(whole, 1000000U, whole_ppm) ||
        !checked_multiply(
            remainder, 1000000U, remainder_ppm_numerator) ||
        !checked_add(
            whole_ppm,
            remainder_ppm_numerator / duration_us,
            report.channel_utilization_ppm)) {
        report = {};
        report.error = GroupLoadError::arithmetic_overflow;
        return report;
    }

    report.error = GroupLoadError::none;
    return report;
}

}  // namespace opentrail::simulation
