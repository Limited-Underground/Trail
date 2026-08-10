#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_power_status_source.hpp"
#include "opentrail/power_state.hpp"

namespace {

using opentrail::power::BatteryPresence;
using opentrail::power::ChargeState;
using opentrail::power::ExternalPowerState;
using opentrail::power::PowerAssessmentError;
using opentrail::power::PowerAssessmentState;
using opentrail::power::PowerPolicy;
using opentrail::power::PowerReadError;
using opentrail::power::PowerStateEvaluator;
using opentrail::power::RawPowerObservation;
using opentrail::power::test_support::FakePowerStatusSource;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

PowerPolicy test_policy() {
    return {30, 15, 30'000};
}

RawPowerObservation battery_observation(std::uint8_t percent,
                                        std::uint64_t sampled_at_ms = 1'000) {
    RawPowerObservation observation{};
    observation.error = PowerReadError::none;
    observation.external_power = ExternalPowerState::absent;
    observation.battery = BatteryPresence::present;
    observation.charge = ChargeState::not_charging;
    observation.battery_percent_valid = true;
    observation.battery_percent = percent;
    observation.battery_voltage_valid = true;
    observation.battery_voltage_mv = 3'800;
    observation.sampled_at_ms = sampled_at_ms;
    return observation;
}

void test_not_ready_then_normal() {
    FakePowerStatusSource source;
    PowerStateEvaluator evaluator(source, test_policy());

    const auto unavailable = evaluator.snapshot(1'000);
    EXPECT(unavailable.state == PowerAssessmentState::unavailable);
    EXPECT(unavailable.error == PowerAssessmentError::source_not_ready);
    EXPECT(unavailable.operator_attention_required);
    EXPECT(!unavailable.optional_high_power_allowed);

    EXPECT(source.enqueue(battery_observation(80)));
    const auto normal = evaluator.snapshot(1'000);
    EXPECT(normal.ok());
    EXPECT(normal.state == PowerAssessmentState::normal);
    EXPECT(normal.battery_percent == 80);
    EXPECT(normal.battery_voltage_mv == 3'800);
    EXPECT(!normal.operator_attention_required);
    EXPECT(normal.optional_high_power_allowed);
    EXPECT(source.read_count() == 2);
}

void test_source_failure_and_unknown_error_fail_closed() {
    FakePowerStatusSource source;
    EXPECT(source.enqueue_failure());
    RawPowerObservation unknown{};
    unknown.error = static_cast<PowerReadError>(255);
    EXPECT(source.enqueue(unknown));
    PowerStateEvaluator evaluator(source, test_policy());

    EXPECT(evaluator.snapshot(1'000).error == PowerAssessmentError::source_failed);
    EXPECT(evaluator.snapshot(1'000).error == PowerAssessmentError::source_failed);
}

void test_invalid_policy_does_not_read_source() {
    FakePowerStatusSource source;
    EXPECT(source.enqueue(battery_observation(80)));

    PowerStateEvaluator zero_stale(source, {30, 15, 0});
    EXPECT(zero_stale.snapshot(1'000).error == PowerAssessmentError::invalid_policy);
    PowerStateEvaluator equal_thresholds(source, {30, 30, 1});
    EXPECT(equal_thresholds.snapshot(1'000).error == PowerAssessmentError::invalid_policy);
    PowerStateEvaluator over_range(source, {101, 15, 1});
    EXPECT(over_range.snapshot(1'000).error == PowerAssessmentError::invalid_policy);
    EXPECT(source.read_count() == 0);
    EXPECT(source.queued_count() == 1);
}

void test_exact_percentage_bands() {
    FakePowerStatusSource source;
    EXPECT(source.enqueue(battery_observation(31)));
    EXPECT(source.enqueue(battery_observation(30)));
    EXPECT(source.enqueue(battery_observation(16)));
    EXPECT(source.enqueue(battery_observation(15)));
    EXPECT(source.enqueue(battery_observation(0)));
    PowerStateEvaluator evaluator(source, test_policy());

    EXPECT(evaluator.snapshot(1'000).state == PowerAssessmentState::normal);
    const auto low_boundary = evaluator.snapshot(1'000);
    EXPECT(low_boundary.state == PowerAssessmentState::low);
    EXPECT(low_boundary.operator_attention_required);
    EXPECT(!low_boundary.optional_high_power_allowed);
    EXPECT(evaluator.snapshot(1'000).state == PowerAssessmentState::low);
    EXPECT(evaluator.snapshot(1'000).state == PowerAssessmentState::critical);
    EXPECT(evaluator.snapshot(1'000).state == PowerAssessmentState::critical);
}

void test_charging_is_orthogonal_to_battery_band() {
    FakePowerStatusSource source;
    auto charging_low = battery_observation(20);
    charging_low.external_power = ExternalPowerState::present;
    charging_low.charge = ChargeState::charging;
    auto full_normal = battery_observation(100);
    full_normal.external_power = ExternalPowerState::present;
    full_normal.charge = ChargeState::full;
    EXPECT(source.enqueue(charging_low));
    EXPECT(source.enqueue(full_normal));
    PowerStateEvaluator evaluator(source, test_policy());

    const auto low = evaluator.snapshot(1'000);
    EXPECT(low.state == PowerAssessmentState::low);
    EXPECT(low.charge == ChargeState::charging);
    const auto normal = evaluator.snapshot(1'000);
    EXPECT(normal.state == PowerAssessmentState::normal);
    EXPECT(normal.charge == ChargeState::full);
}

void test_external_only_is_explicit() {
    FakePowerStatusSource source;
    RawPowerObservation observation{};
    observation.error = PowerReadError::none;
    observation.external_power = ExternalPowerState::present;
    observation.battery = BatteryPresence::absent;
    observation.charge = ChargeState::not_charging;
    observation.sampled_at_ms = 5;
    EXPECT(source.enqueue(observation));
    PowerStateEvaluator evaluator(source, test_policy());

    const auto result = evaluator.snapshot(5);
    EXPECT(result.ok());
    EXPECT(result.state == PowerAssessmentState::external_only);
    EXPECT(!result.battery_percent_valid);
    EXPECT(!result.operator_attention_required);
    EXPECT(result.optional_high_power_allowed);
}

void test_missing_measurement_remains_indeterminate() {
    FakePowerStatusSource source;
    auto observation = battery_observation(0);
    observation.battery_percent_valid = false;
    observation.battery_percent = 0;
    EXPECT(source.enqueue(observation));
    PowerStateEvaluator evaluator(source, test_policy());

    const auto result = evaluator.snapshot(1'000);
    EXPECT(result.ok());
    EXPECT(result.state == PowerAssessmentState::indeterminate);
    EXPECT(!result.battery_percent_valid);
    EXPECT(result.battery_voltage_valid);
    EXPECT(result.operator_attention_required);
    EXPECT(!result.optional_high_power_allowed);
}

void test_reported_charger_fault_is_not_a_source_error() {
    FakePowerStatusSource source;
    auto observation = battery_observation(70);
    observation.external_power = ExternalPowerState::present;
    observation.charge = ChargeState::fault;
    EXPECT(source.enqueue(observation));
    PowerStateEvaluator evaluator(source, test_policy());

    const auto result = evaluator.snapshot(1'000);
    EXPECT(result.ok());
    EXPECT(result.state == PowerAssessmentState::fault);
    EXPECT(result.charge == ChargeState::fault);
    EXPECT(result.operator_attention_required);
}

void test_invalid_observation_combinations_are_rejected() {
    FakePowerStatusSource source;
    auto over_range = battery_observation(101);
    auto charging_without_external = battery_observation(70);
    charging_without_external.charge = ChargeState::charging;
    auto percentage_without_battery = battery_observation(70);
    percentage_without_battery.battery = BatteryPresence::absent;
    percentage_without_battery.external_power = ExternalPowerState::present;
    auto hidden_value = battery_observation(70);
    hidden_value.battery_percent_valid = false;
    auto no_power = battery_observation(70);
    no_power.battery = BatteryPresence::absent;
    no_power.battery_percent_valid = false;
    no_power.battery_percent = 0;
    no_power.battery_voltage_valid = false;
    no_power.battery_voltage_mv = 0;
    EXPECT(source.enqueue(over_range));
    EXPECT(source.enqueue(charging_without_external));
    EXPECT(source.enqueue(percentage_without_battery));
    EXPECT(source.enqueue(hidden_value));
    EXPECT(source.enqueue(no_power));
    PowerStateEvaluator evaluator(source, test_policy());

    for (int index = 0; index < 5; ++index) {
        const auto result = evaluator.snapshot(1'000);
        EXPECT(result.state == PowerAssessmentState::invalid);
        EXPECT(result.error == PowerAssessmentError::invalid_observation);
        EXPECT(result.operator_attention_required);
    }
}

void test_time_boundary_rejects_future_and_expires_after_limit() {
    FakePowerStatusSource source;
    EXPECT(source.enqueue(battery_observation(70, 1'001)));
    EXPECT(source.enqueue(battery_observation(70, 1'000)));
    EXPECT(source.enqueue(battery_observation(70, 1'000)));
    PowerStateEvaluator evaluator(source, test_policy());

    const auto future = evaluator.snapshot(1'000);
    EXPECT(future.state == PowerAssessmentState::invalid);
    EXPECT(future.error == PowerAssessmentError::timestamp_in_future);
    const auto exact_limit = evaluator.snapshot(31'000);
    EXPECT(exact_limit.ok());
    EXPECT(exact_limit.state == PowerAssessmentState::normal);
    EXPECT(exact_limit.age_ms == 30'000);
    const auto stale = evaluator.snapshot(31'001);
    EXPECT(stale.state == PowerAssessmentState::stale);
    EXPECT(stale.error == PowerAssessmentError::observation_stale);
    EXPECT(stale.age_ms == 30'001);
}

void test_fake_source_is_bounded_fifo() {
    FakePowerStatusSource source;
    for (std::size_t index = 0; index < source.kCapacity; ++index) {
        EXPECT(source.enqueue(battery_observation(static_cast<std::uint8_t>(index))));
    }
    EXPECT(!source.enqueue(battery_observation(99)));
    EXPECT(source.queued_count() == source.kCapacity);

    for (std::size_t index = 0; index < source.kCapacity; ++index) {
        const auto observation = source.read();
        EXPECT(observation.error == PowerReadError::none);
        EXPECT(observation.battery_percent == index);
    }
    EXPECT(source.read().error == PowerReadError::not_ready);
    EXPECT(source.queued_count() == 0);
    EXPECT(source.read_count() == source.kCapacity + 1);
}

}  // namespace

int main() {
    test_not_ready_then_normal();
    test_source_failure_and_unknown_error_fail_closed();
    test_invalid_policy_does_not_read_source();
    test_exact_percentage_bands();
    test_charging_is_orthogonal_to_battery_band();
    test_external_only_is_explicit();
    test_missing_measurement_remains_indeterminate();
    test_reported_charger_fault_is_not_a_source_error();
    test_invalid_observation_combinations_are_rejected();
    test_time_boundary_rejects_future_and_expires_after_limit();
    test_fake_source_is_bounded_fifo();

    if (failures != 0) {
        std::cerr << failures << " power-state assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 11 power-state boundary scenario groups\n";
    return EXIT_SUCCESS;
}
