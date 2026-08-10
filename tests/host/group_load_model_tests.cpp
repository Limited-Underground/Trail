#include <cstdlib>
#include <iostream>

#include "opentrail/group_load_model.hpp"

namespace {

using opentrail::simulation::GroupLoadError;
using opentrail::simulation::GroupTrafficProfile;
using opentrail::simulation::estimate_group_load;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

GroupTrafficProfile baseline(
    std::size_t members = 4,
    std::size_t relays = 0) {
    return {
        members,
        relays,
        3600000,
        60000,
        38,
        300000,
        22,
        1,
        64,
        1,
        {62500, 8, 7, 5, true, true, false},
    };
}

void test_four_member_standalone_baseline() {
    const auto report = estimate_group_load(baseline());
    EXPECT(report.estimated());
    EXPECT(report.positions.logical_messages == 240);
    EXPECT(report.statuses.logical_messages == 48);
    EXPECT(report.alerts.logical_messages == 4);
    EXPECT(report.logical_messages == 292);
    EXPECT(report.source_transmissions == 292);
    EXPECT(report.relay_transmissions == 0);
    EXPECT(report.radio_transmissions == 292);
    EXPECT(report.total_airtime_us > 0);
    EXPECT(report.channel_utilization_ppm > 0);
}

void test_repeater_copy_is_visible_not_free() {
    const auto direct = estimate_group_load(baseline(4, 0));
    const auto repeated = estimate_group_load(baseline(4, 1));
    EXPECT(direct.estimated());
    EXPECT(repeated.estimated());
    EXPECT(repeated.logical_messages == direct.logical_messages);
    EXPECT(repeated.source_transmissions == direct.source_transmissions);
    EXPECT(repeated.relay_transmissions == direct.source_transmissions);
    EXPECT(repeated.radio_transmissions == direct.radio_transmissions * 2);
    EXPECT(repeated.total_airtime_us == direct.total_airtime_us * 2);
}

void test_eight_member_repeater_phase_scales_from_pilot() {
    const auto pilot = estimate_group_load(baseline(4, 0));
    const auto release = estimate_group_load(baseline(8, 1));
    EXPECT(pilot.estimated());
    EXPECT(release.estimated());
    EXPECT(release.logical_messages == pilot.logical_messages * 2);
    EXPECT(release.source_transmissions == pilot.source_transmissions * 2);
    EXPECT(release.relay_transmissions == pilot.source_transmissions * 2);
    EXPECT(release.radio_transmissions == pilot.radio_transmissions * 4);
    EXPECT(release.total_airtime_us == pilot.total_airtime_us * 4);
}

void test_source_attempts_scale_before_relay_copies() {
    auto profile = baseline(4, 1);
    profile.source_attempts_per_message = 2;
    const auto report = estimate_group_load(profile);
    EXPECT(report.estimated());
    EXPECT(report.logical_messages == 292);
    EXPECT(report.source_transmissions == 584);
    EXPECT(report.relay_transmissions == 584);
    EXPECT(report.radio_transmissions == 1168);
}

void test_disabled_and_long_interval_streams_are_zero() {
    auto profile = baseline();
    profile.duration_ms = 30000;
    profile.position_interval_ms = 60000;
    profile.status_interval_ms = 0;
    profile.status_payload_bytes = 0;
    profile.alerts_per_member = 0;
    profile.alert_payload_bytes = 0;
    const auto report = estimate_group_load(profile);
    EXPECT(report.estimated());
    EXPECT(report.logical_messages == 0);
    EXPECT(report.radio_transmissions == 0);
    EXPECT(report.total_airtime_us == 0);
    EXPECT(report.channel_utilization_ppm == 0);
}

void test_invalid_inputs_fail_closed() {
    auto profile = baseline();
    profile.member_count = 0;
    EXPECT(estimate_group_load(profile).error ==
           GroupLoadError::invalid_member_count);

    profile = baseline();
    profile.forwarding_relays = 17;
    EXPECT(estimate_group_load(profile).error ==
           GroupLoadError::invalid_relay_count);

    profile = baseline();
    profile.duration_ms = 0;
    EXPECT(estimate_group_load(profile).error ==
           GroupLoadError::invalid_duration);

    profile = baseline();
    profile.source_attempts_per_message = 5;
    EXPECT(estimate_group_load(profile).error ==
           GroupLoadError::invalid_source_attempts);

    profile = baseline();
    profile.position_payload_bytes = 0;
    EXPECT(estimate_group_load(profile).error ==
           GroupLoadError::invalid_traffic);

    profile = baseline();
    profile.radio.bandwidth_hz = 0;
    EXPECT(estimate_group_load(profile).error ==
           GroupLoadError::invalid_radio_settings);
}

}  // namespace

int main() {
    test_four_member_standalone_baseline();
    test_repeater_copy_is_visible_not_free();
    test_eight_member_repeater_phase_scales_from_pilot();
    test_source_attempts_scale_before_relay_copies();
    test_disabled_and_long_interval_streams_are_zero();
    test_invalid_inputs_fail_closed();

    if (failures != 0) {
        std::cerr << failures << " group-load assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 6 group-load scenario groups\n";
    return EXIT_SUCCESS;
}
