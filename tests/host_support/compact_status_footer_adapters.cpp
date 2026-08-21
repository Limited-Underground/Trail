#include "compact_status_footer_adapters.hpp"

namespace opentrail::ui::compact_status_footer::adapters {
namespace {

Metric unavailable_metric() {
    return {ObservationState::unavailable, 0, 0};
}

Metric invalid_metric() {
    return {ObservationState::invalid, 0, 0};
}

}  // namespace

Metric battery_metric_from_power(
    const power::PowerAssessment& assessment,
    std::uint64_t assessment_now_ms) {
    using power::BatteryPresence;
    using power::PowerAssessmentError;
    using power::PowerAssessmentState;

    if (assessment.error == PowerAssessmentError::source_not_ready) {
        return unavailable_metric();
    }
    if (assessment.error != PowerAssessmentError::none) {
        return invalid_metric();
    }
    if (assessment.battery != BatteryPresence::present ||
        !assessment.battery_percent_valid) {
        return unavailable_metric();
    }
    if (assessment.battery_percent > 100U ||
        assessment.age_ms > assessment_now_ms) {
        return invalid_metric();
    }

    switch (assessment.state) {
        case PowerAssessmentState::normal:
        case PowerAssessmentState::low:
        case PowerAssessmentState::critical:
            return {
                ObservationState::valid,
                assessment.battery_percent,
                assessment_now_ms - assessment.age_ms,
            };
        case PowerAssessmentState::unavailable:
        case PowerAssessmentState::external_only:
        case PowerAssessmentState::indeterminate:
            return unavailable_metric();
        case PowerAssessmentState::fault:
        case PowerAssessmentState::stale:
        case PowerAssessmentState::invalid:
        default:
            return invalid_metric();
    }
}

BleCode ble_code_from_runtime(
    const companion::CompanionBleRuntimeStatus& status) {
    using companion::CompanionBleRuntimeError;
    using companion::CompanionBleRuntimePhase;

    if (status.terminal_error != CompanionBleRuntimeError::none) {
        return BleCode::error;
    }
    switch (status.phase) {
        case CompanionBleRuntimePhase::dormant:
            return BleCode::unavailable;
        case CompanionBleRuntimePhase::waiting_for_host_sync:
            return BleCode::starting;
        case CompanionBleRuntimePhase::advertising:
            return BleCode::advertising;
        case CompanionBleRuntimePhase::connected:
            return BleCode::connected;
        case CompanionBleRuntimePhase::restart_wait:
            return BleCode::retrying;
        case CompanionBleRuntimePhase::contained:
            return BleCode::error;
        default:
            return BleCode::unavailable;
    }
}

Composition compose(
    const power::PowerAssessment& power_assessment,
    const companion::CompanionBleRuntimeStatus& ble_status,
    std::uint64_t battery_fresh_for_ms,
    std::uint64_t assessment_now_ms,
    std::uint64_t render_now_ms) {
    Composition result{};
    result.snapshot.battery_percent =
        battery_metric_from_power(power_assessment, assessment_now_ms);
    if (render_now_ms < assessment_now_ms) {
        result.snapshot.battery_percent = invalid_metric();
    }
    result.snapshot.gps_satellites = unavailable_metric();
    result.snapshot.ble = ble_code_from_runtime(ble_status);
    result.fields = format(
        result.snapshot,
        {battery_fresh_for_ms, 0},
        render_now_ms);
    const ActivityOwner unsupported_activity{false, 0};
    result.page = render(result.fields, unsupported_activity, render_now_ms);
    return result;
}

}  // namespace opentrail::ui::compact_status_footer::adapters
