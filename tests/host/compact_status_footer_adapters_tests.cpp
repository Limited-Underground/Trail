#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>

#include "../host_support/compact_status_footer_adapters.hpp"

namespace {

namespace footer = opentrail::ui::compact_status_footer;
namespace adapters = opentrail::ui::compact_status_footer::adapters;
namespace companion = opentrail::companion;
namespace power = opentrail::power;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

template <std::size_t Size>
std::string visible(const std::array<char, Size>& field) {
    std::size_t length = 0;
    while (length < field.size() && field[length] != '\0') {
        ++length;
    }
    return {field.data(), length};
}

power::PowerAssessment battery_assessment(
    power::PowerAssessmentState state,
    std::uint8_t percent,
    std::uint64_t age_ms = 0) {
    power::PowerAssessment assessment{};
    assessment.state = state;
    assessment.error = power::PowerAssessmentError::none;
    assessment.battery = power::BatteryPresence::present;
    assessment.battery_percent_valid = true;
    assessment.battery_percent = percent;
    assessment.age_ms = age_ms;
    return assessment;
}

companion::CompanionBleRuntimeStatus ble_status(
    companion::CompanionBleRuntimePhase phase) {
    companion::CompanionBleRuntimeStatus status{};
    status.phase = phase;
    status.terminal_error = companion::CompanionBleRuntimeError::none;
    return status;
}

bool same_metric(const footer::Metric& left, const footer::Metric& right) {
    return left.state == right.state && left.value == right.value &&
           left.sampled_at_ms == right.sampled_at_ms;
}

bool same_snapshot(const footer::Snapshot& left, const footer::Snapshot& right) {
    return same_metric(left.battery_percent, right.battery_percent) &&
           same_metric(left.gps_satellites, right.gps_satellites) &&
           left.ble == right.ble;
}

bool same_composition(
    const adapters::Composition& left,
    const adapters::Composition& right) {
    return same_snapshot(left.snapshot, right.snapshot) &&
           left.fields.battery == right.fields.battery &&
           left.fields.gps == right.fields.gps &&
           left.fields.ble == right.fields.ble &&
           left.page.columns == right.page.columns;
}

void expect_activity_blank(const adapters::Composition& value) {
    for (std::size_t column = 121; column <= 125; ++column) {
        EXPECT(value.page.columns[column] == 0U);
    }
}

void test_normal_low_critical_battery_values_including_bounds() {
    const std::array<std::pair<power::PowerAssessmentState, std::uint8_t>, 5>
        cases{{
            {power::PowerAssessmentState::critical, 0},
            {power::PowerAssessmentState::critical, 10},
            {power::PowerAssessmentState::low, 25},
            {power::PowerAssessmentState::normal, 99},
            {power::PowerAssessmentState::normal, 100},
        }};
    for (const auto& item : cases) {
        const auto metric = adapters::battery_metric_from_power(
            battery_assessment(item.first, item.second), 1'000);
        EXPECT(metric.state == footer::ObservationState::valid);
        EXPECT(metric.value == item.second);
        EXPECT(metric.sampled_at_ms == 1'000);
    }
}

void test_absent_unknown_and_missing_battery_percent() {
    auto absent = battery_assessment(power::PowerAssessmentState::external_only, 0);
    absent.battery = power::BatteryPresence::absent;
    auto unknown = battery_assessment(power::PowerAssessmentState::indeterminate, 0);
    unknown.battery = power::BatteryPresence::unknown;
    auto missing = battery_assessment(power::PowerAssessmentState::indeterminate, 0);
    missing.battery_percent_valid = false;
    for (const auto& assessment : {absent, unknown, missing}) {
        const auto metric =
            adapters::battery_metric_from_power(assessment, 100);
        EXPECT(metric.state == footer::ObservationState::unavailable);
        EXPECT(metric.value == 0U);
        EXPECT(metric.sampled_at_ms == 0U);
    }
}

void test_failed_invalid_stale_and_future_assessments() {
    const std::array<
        std::pair<power::PowerAssessmentState, power::PowerAssessmentError>, 5>
        cases{{
            {power::PowerAssessmentState::unavailable,
             power::PowerAssessmentError::source_failed},
            {power::PowerAssessmentState::invalid,
             power::PowerAssessmentError::invalid_policy},
            {power::PowerAssessmentState::invalid,
             power::PowerAssessmentError::invalid_observation},
            {power::PowerAssessmentState::stale,
             power::PowerAssessmentError::observation_stale},
            {power::PowerAssessmentState::invalid,
             power::PowerAssessmentError::timestamp_in_future},
        }};
    for (const auto& item : cases) {
        auto assessment = battery_assessment(item.first, 50);
        assessment.error = item.second;
        const auto metric =
            adapters::battery_metric_from_power(assessment, 1'000);
        EXPECT(metric.state == footer::ObservationState::invalid);
        EXPECT(metric.value == 0U);
    }
    power::PowerAssessment not_ready{};
    EXPECT(adapters::battery_metric_from_power(not_ready, 1'000).state ==
           footer::ObservationState::unavailable);
}

void test_age_to_sample_time_and_unsigned_boundaries() {
    const auto aged = battery_assessment(
        power::PowerAssessmentState::normal, 42, 250);
    const auto metric = adapters::battery_metric_from_power(aged, 1'000);
    EXPECT(metric.state == footer::ObservationState::valid);
    EXPECT(metric.sampled_at_ms == 750U);
    EXPECT(visible(adapters::compose(
        aged, ble_status(companion::CompanionBleRuntimePhase::connected),
        251, 1'000, 1'000).fields.battery) == "BAT:42%");
    EXPECT(visible(adapters::compose(
        aged, ble_status(companion::CompanionBleRuntimePhase::connected),
        250, 1'000, 1'000).fields.battery) == "BAT:--%");

    const auto delayed = adapters::compose(
        aged, ble_status(companion::CompanionBleRuntimePhase::connected),
        251, 1'000, 2'000);
    EXPECT(delayed.snapshot.battery_percent.sampled_at_ms == 750U);
    EXPECT(visible(delayed.fields.battery) == "BAT:--%");
    const auto future_at_render = adapters::compose(
        aged, ble_status(companion::CompanionBleRuntimePhase::connected),
        251, 1'000, 749);
    EXPECT(future_at_render.snapshot.battery_percent.state ==
           footer::ObservationState::invalid);
    EXPECT(future_at_render.snapshot.battery_percent.sampled_at_ms == 0U);
    EXPECT(visible(future_at_render.fields.battery) == "BAT:--%");
    const auto before_assessment = adapters::compose(
        aged, ble_status(companion::CompanionBleRuntimePhase::connected),
        251, 1'000, 800);
    EXPECT(before_assessment.snapshot.battery_percent.state ==
           footer::ObservationState::invalid);
    EXPECT(before_assessment.snapshot.battery_percent.sampled_at_ms == 0U);
    EXPECT(visible(before_assessment.fields.battery) == "BAT:--%");

    const auto underflow = battery_assessment(
        power::PowerAssessmentState::normal, 42, 1'001);
    EXPECT(adapters::battery_metric_from_power(underflow, 1'000).state ==
           footer::ObservationState::invalid);

    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto near_wrap = battery_assessment(
        power::PowerAssessmentState::normal, 77, maximum - 1U);
    const auto boundary_metric =
        adapters::battery_metric_from_power(near_wrap, maximum);
    EXPECT(boundary_metric.state == footer::ObservationState::valid);
    EXPECT(boundary_metric.sampled_at_ms == 1U);
    EXPECT(visible(adapters::compose(
        near_wrap, ble_status(companion::CompanionBleRuntimePhase::advertising),
        maximum, maximum, maximum).fields.battery) == "BAT:77%");
}

void test_every_defined_ble_phase() {
    const std::array<
        std::pair<companion::CompanionBleRuntimePhase, footer::BleCode>, 6>
        cases{{
            {companion::CompanionBleRuntimePhase::dormant,
             footer::BleCode::unavailable},
            {companion::CompanionBleRuntimePhase::waiting_for_host_sync,
             footer::BleCode::starting},
            {companion::CompanionBleRuntimePhase::advertising,
             footer::BleCode::advertising},
            {companion::CompanionBleRuntimePhase::connected,
             footer::BleCode::connected},
            {companion::CompanionBleRuntimePhase::restart_wait,
             footer::BleCode::retrying},
            {companion::CompanionBleRuntimePhase::contained,
             footer::BleCode::error},
        }};
    for (const auto& item : cases) {
        EXPECT(adapters::ble_code_from_runtime(ble_status(item.first)) ==
               item.second);
    }
}

void test_terminal_error_precedence_and_unknown_containment() {
    auto terminal = ble_status(companion::CompanionBleRuntimePhase::advertising);
    terminal.terminal_error = companion::CompanionBleRuntimeError::host_reset;
    EXPECT(adapters::ble_code_from_runtime(terminal) == footer::BleCode::error);
    terminal.phase = companion::CompanionBleRuntimePhase::connected;
    terminal.terminal_error =
        static_cast<companion::CompanionBleRuntimeError>(0xFF);
    EXPECT(adapters::ble_code_from_runtime(terminal) == footer::BleCode::error);
    terminal.phase = static_cast<companion::CompanionBleRuntimePhase>(0xFF);
    terminal.terminal_error = companion::CompanionBleRuntimeError::none;
    EXPECT(adapters::ble_code_from_runtime(terminal) ==
           footer::BleCode::unavailable);
}

void test_exact_integrated_formatting() {
    const auto result = adapters::compose(
        battery_assessment(power::PowerAssessmentState::normal, 42, 25),
        ble_status(companion::CompanionBleRuntimePhase::connected),
        100, 1'000, 1'000);
    EXPECT(visible(result.fields.battery) == "BAT:42%");
    EXPECT(visible(result.fields.gps) == "GPS:--");
    EXPECT(visible(result.fields.ble) == "BLE:C");
    EXPECT(result.snapshot.battery_percent.sampled_at_ms == 975U);
}

void test_mixed_inputs_do_not_cross_contaminate_fields() {
    auto invalid = battery_assessment(power::PowerAssessmentState::invalid, 55);
    invalid.error = power::PowerAssessmentError::invalid_observation;
    const auto bad_battery = adapters::compose(
        invalid, ble_status(companion::CompanionBleRuntimePhase::advertising),
        100, 100, 100);
    EXPECT(visible(bad_battery.fields.battery) == "BAT:--%");
    EXPECT(visible(bad_battery.fields.ble) == "BLE:A");

    auto unknown_ble = ble_status(companion::CompanionBleRuntimePhase::connected);
    unknown_ble.phase = static_cast<companion::CompanionBleRuntimePhase>(0xFF);
    const auto bad_ble = adapters::compose(
        battery_assessment(power::PowerAssessmentState::normal, 88),
        unknown_ble, 100, 100, 100);
    EXPECT(visible(bad_ble.fields.battery) == "BAT:88%");
    EXPECT(visible(bad_ble.fields.ble) == "BLE:-");
}

void test_gps_unavailable_and_activity_blank_in_every_case() {
    for (std::uint8_t phase = 0; phase <= 5; ++phase) {
        for (const bool valid_battery : {false, true}) {
            auto assessment = battery_assessment(
                valid_battery ? power::PowerAssessmentState::normal
                              : power::PowerAssessmentState::invalid,
                50);
            if (!valid_battery) {
                assessment.error = power::PowerAssessmentError::invalid_observation;
            }
            const auto result = adapters::compose(
                assessment,
                ble_status(static_cast<companion::CompanionBleRuntimePhase>(phase)),
                100, 100, 100);
            EXPECT(result.snapshot.gps_satellites.state ==
                   footer::ObservationState::unavailable);
            EXPECT(visible(result.fields.gps) == "GPS:--");
            expect_activity_blank(result);
        }
    }
}

void test_deterministic_stateless_identifier_independent_composition() {
    static_assert(std::is_trivially_copyable<adapters::Composition>::value);
    static_assert(std::is_standard_layout<adapters::Composition>::value);
    const auto assessment = battery_assessment(
        power::PowerAssessmentState::low, 15, 5);
    auto status = ble_status(companion::CompanionBleRuntimePhase::connected);
    const auto expected = adapters::compose(
        assessment, status, 100, 1'000, 1'000);
    for (std::uint64_t index = 0; index < 100; ++index) {
        status.authorization_claims_closed = (index % 2U) == 0U;
        status.normal_commands_closed = (index % 3U) == 0U;
        status.connection_handle = static_cast<std::uint16_t>(index);
        status.restart_token = index;
        status.restart_attempts = static_cast<std::uint8_t>(index);
        status.termination_pending = (index % 5U) == 0U;
        EXPECT(same_composition(
            adapters::compose(assessment, status, 100, 1'000, 1'000),
            expected));
    }
    const auto other = adapters::compose(
        battery_assessment(power::PowerAssessmentState::normal, 90),
        ble_status(companion::CompanionBleRuntimePhase::advertising),
        100, 1'000, 1'000);
    EXPECT(!same_composition(other, expected));
    EXPECT(same_composition(
        adapters::compose(assessment, status, 100, 1'000, 1'000), expected));
}

}  // namespace

int main() {
    test_normal_low_critical_battery_values_including_bounds();
    test_absent_unknown_and_missing_battery_percent();
    test_failed_invalid_stale_and_future_assessments();
    test_age_to_sample_time_and_unsigned_boundaries();
    test_every_defined_ble_phase();
    test_terminal_error_precedence_and_unknown_containment();
    test_exact_integrated_formatting();
    test_mixed_inputs_do_not_cross_contaminate_fields();
    test_gps_unavailable_and_activity_blank_in_every_case();
    test_deterministic_stateless_identifier_independent_composition();

    if (failures != 0) {
        std::cerr << failures << " compact status footer adapter assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 compact status footer adapter scenario groups\n";
    return EXIT_SUCCESS;
}
