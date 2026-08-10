#include "opentrail/power_state.hpp"

namespace opentrail::power {
bool valid_power_policy(const PowerPolicy& policy) {
    return policy.stale_after_ms != 0 &&
           policy.critical_battery_percent < policy.low_battery_percent &&
           policy.low_battery_percent <= 100;
}

namespace {

bool valid_external_power(ExternalPowerState state) {
    return state == ExternalPowerState::unknown ||
           state == ExternalPowerState::absent ||
           state == ExternalPowerState::present;
}

bool valid_battery_presence(BatteryPresence state) {
    return state == BatteryPresence::unknown ||
           state == BatteryPresence::absent ||
           state == BatteryPresence::present;
}

bool valid_charge_state(ChargeState state) {
    return state == ChargeState::unknown || state == ChargeState::not_charging ||
           state == ChargeState::charging || state == ChargeState::full ||
           state == ChargeState::fault;
}

bool valid_observation(const RawPowerObservation& observation) {
    if (!valid_external_power(observation.external_power) ||
        !valid_battery_presence(observation.battery) ||
        !valid_charge_state(observation.charge)) {
        return false;
    }

    if ((!observation.battery_percent_valid && observation.battery_percent != 0) ||
        (!observation.battery_voltage_valid && observation.battery_voltage_mv != 0)) {
        return false;
    }

    if (observation.battery_percent_valid &&
        (observation.battery != BatteryPresence::present ||
         observation.battery_percent > 100)) {
        return false;
    }

    if (observation.battery_voltage_valid &&
        (observation.battery != BatteryPresence::present ||
         observation.battery_voltage_mv == 0)) {
        return false;
    }

    if ((observation.charge == ChargeState::charging ||
         observation.charge == ChargeState::full) &&
        (observation.external_power != ExternalPowerState::present ||
         observation.battery != BatteryPresence::present)) {
        return false;
    }

    return !(observation.external_power == ExternalPowerState::absent &&
             observation.battery == BatteryPresence::absent);
}

PowerAssessment failed_assessment(PowerAssessmentState state,
                                  PowerAssessmentError error) {
    PowerAssessment result{};
    result.state = state;
    result.error = error;
    return result;
}

}  // namespace

PowerStateEvaluator::PowerStateEvaluator(PowerStatusSource& source, PowerPolicy policy)
    : source_(source), policy_(policy) {}

PowerAssessment PowerStateEvaluator::snapshot(std::uint64_t now_ms) {
    if (!valid_power_policy(policy_)) {
        return failed_assessment(PowerAssessmentState::invalid,
                                 PowerAssessmentError::invalid_policy);
    }

    const auto observation = source_.read();
    switch (observation.error) {
        case PowerReadError::not_ready:
            return failed_assessment(PowerAssessmentState::unavailable,
                                     PowerAssessmentError::source_not_ready);
        case PowerReadError::source_failed:
            return failed_assessment(PowerAssessmentState::unavailable,
                                     PowerAssessmentError::source_failed);
        case PowerReadError::none:
            break;
        default:
            return failed_assessment(PowerAssessmentState::unavailable,
                                     PowerAssessmentError::source_failed);
    }

    PowerAssessment result{};
    result.external_power = observation.external_power;
    result.battery = observation.battery;
    result.charge = observation.charge;
    result.battery_percent_valid = observation.battery_percent_valid;
    result.battery_percent = observation.battery_percent;
    result.battery_voltage_valid = observation.battery_voltage_valid;
    result.battery_voltage_mv = observation.battery_voltage_mv;

    if (observation.sampled_at_ms > now_ms) {
        result.state = PowerAssessmentState::invalid;
        result.error = PowerAssessmentError::timestamp_in_future;
        return result;
    }
    result.age_ms = now_ms - observation.sampled_at_ms;

    if (!valid_observation(observation)) {
        result.state = PowerAssessmentState::invalid;
        result.error = PowerAssessmentError::invalid_observation;
        return result;
    }

    if (result.age_ms > policy_.stale_after_ms) {
        result.state = PowerAssessmentState::stale;
        result.error = PowerAssessmentError::observation_stale;
        return result;
    }

    result.error = PowerAssessmentError::none;
    if (observation.charge == ChargeState::fault) {
        result.state = PowerAssessmentState::fault;
        return result;
    }

    if (observation.battery == BatteryPresence::absent &&
        observation.external_power == ExternalPowerState::present) {
        result.state = PowerAssessmentState::external_only;
        result.operator_attention_required = false;
        result.optional_high_power_allowed = true;
        return result;
    }

    if (observation.battery != BatteryPresence::present ||
        !observation.battery_percent_valid) {
        result.state = PowerAssessmentState::indeterminate;
        return result;
    }

    if (observation.battery_percent <= policy_.critical_battery_percent) {
        result.state = PowerAssessmentState::critical;
        return result;
    }
    if (observation.battery_percent <= policy_.low_battery_percent) {
        result.state = PowerAssessmentState::low;
        return result;
    }

    result.state = PowerAssessmentState::normal;
    result.operator_attention_required = false;
    result.optional_high_power_allowed = true;
    return result;
}

}  // namespace opentrail::power
