#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_gps_provider.hpp"
#include "opentrail/location_tracker.hpp"

namespace {

using opentrail::location::FixError;
using opentrail::location::FixState;
using opentrail::location::GpsFix;
using opentrail::location::LocationTracker;
using opentrail::location::test_support::FakeGpsProvider;

constexpr std::uint32_t kStaleAfterMs = 30000;
int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

GpsFix valid_fix(std::uint64_t received_at_ms = 1000) {
    GpsFix fix{};
    fix.latitude_e7 = 449775000;
    fix.longitude_e7 = -677500000;
    fix.altitude_cm = 12345;
    fix.horizontal_accuracy_cm = 250;
    fix.speed_cm_per_second = 345;
    fix.heading_centidegrees = 27125;
    fix.received_at_ms = received_at_ms;
    fix.utc_seconds = 1786204800;
    fix.altitude_valid = true;
    fix.horizontal_accuracy_valid = true;
    fix.speed_valid = true;
    fix.heading_valid = true;
    fix.utc_valid = true;
    return fix;
}

void test_unavailable_provider() {
    FakeGpsProvider provider;
    LocationTracker tracker(provider, kStaleAfterMs);
    const auto result = tracker.snapshot(5000);
    EXPECT(result.state == FixState::unavailable);
    EXPECT(result.error == FixError::no_fix);
    EXPECT(!result.usable());
    EXPECT(provider.read_count() == 1);
}

void test_complete_valid_fix() {
    FakeGpsProvider provider;
    provider.set_fix(valid_fix());
    LocationTracker tracker(provider, kStaleAfterMs);
    const auto result = tracker.snapshot(1250);
    EXPECT(result.state == FixState::valid);
    EXPECT(result.error == FixError::none);
    EXPECT(result.usable());
    EXPECT(result.age_ms == 250);
    EXPECT(result.fix.latitude_e7 == 449775000);
    EXPECT(result.fix.utc_valid);
}

void test_no_utc_boot_is_still_valid() {
    FakeGpsProvider provider;
    auto fix = valid_fix(50);
    fix.utc_seconds = 0;
    fix.utc_valid = false;
    provider.set_fix(fix);
    LocationTracker tracker(provider, kStaleAfterMs);
    const auto result = tracker.snapshot(100);
    EXPECT(result.state == FixState::valid);
    EXPECT(!result.fix.utc_valid);
    EXPECT(result.age_ms == 50);
}

void test_coordinate_validation() {
    FakeGpsProvider provider;
    LocationTracker tracker(provider, kStaleAfterMs);

    auto fix = valid_fix();
    fix.latitude_e7 = 900000001;
    provider.set_fix(fix);
    auto result = tracker.snapshot(1000);
    EXPECT(result.state == FixState::invalid);
    EXPECT(result.error == FixError::latitude_out_of_range);

    fix = valid_fix();
    fix.longitude_e7 = -1800000001;
    provider.set_fix(fix);
    result = tracker.snapshot(1000);
    EXPECT(result.state == FixState::invalid);
    EXPECT(result.error == FixError::longitude_out_of_range);
}

void test_optional_field_validation() {
    FakeGpsProvider provider;
    LocationTracker tracker(provider, kStaleAfterMs);

    auto fix = valid_fix();
    fix.heading_centidegrees = 36000;
    provider.set_fix(fix);
    auto result = tracker.snapshot(1000);
    EXPECT(result.state == FixState::invalid);
    EXPECT(result.error == FixError::heading_out_of_range);

    fix = valid_fix();
    fix.horizontal_accuracy_cm = 0;
    provider.set_fix(fix);
    result = tracker.snapshot(1000);
    EXPECT(result.state == FixState::invalid);
    EXPECT(result.error == FixError::accuracy_zero);
}

void test_absent_optional_fields_are_ignored() {
    FakeGpsProvider provider;
    auto fix = valid_fix();
    fix.heading_valid = false;
    fix.heading_centidegrees = 65535;
    fix.horizontal_accuracy_valid = false;
    fix.horizontal_accuracy_cm = 0;
    provider.set_fix(fix);
    LocationTracker tracker(provider, kStaleAfterMs);
    EXPECT(tracker.snapshot(1000).state == FixState::valid);
}

void test_stale_boundary() {
    FakeGpsProvider provider;
    provider.set_fix(valid_fix(1000));
    LocationTracker tracker(provider, kStaleAfterMs);
    EXPECT(tracker.snapshot(30999).state == FixState::valid);
    const auto stale = tracker.snapshot(31000);
    EXPECT(stale.state == FixState::stale);
    EXPECT(stale.error == FixError::none);
    EXPECT(stale.age_ms == kStaleAfterMs);
    EXPECT(!stale.usable());
}

void test_new_fix_recovers_from_stale() {
    FakeGpsProvider provider;
    provider.set_fix(valid_fix(1000));
    LocationTracker tracker(provider, kStaleAfterMs);
    EXPECT(tracker.snapshot(31000).state == FixState::stale);
    provider.set_fix(valid_fix(30950));
    const auto recovered = tracker.snapshot(31000);
    EXPECT(recovered.state == FixState::valid);
    EXPECT(recovered.age_ms == 50);
}

void test_future_monotonic_timestamp_is_invalid() {
    FakeGpsProvider provider;
    provider.set_fix(valid_fix(1001));
    LocationTracker tracker(provider, kStaleAfterMs);
    const auto result = tracker.snapshot(1000);
    EXPECT(result.state == FixState::invalid);
    EXPECT(result.error == FixError::timestamp_in_future);
    EXPECT(result.age_ms == 0);
}

}  // namespace

int main() {
    test_unavailable_provider();
    test_complete_valid_fix();
    test_no_utc_boot_is_still_valid();
    test_coordinate_validation();
    test_optional_field_validation();
    test_absent_optional_fields_are_ignored();
    test_stale_boundary();
    test_new_fix_recovers_from_stale();
    test_future_monotonic_timestamp_is_invalid();

    if (failures != 0) {
        std::cerr << failures << " location tracker assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 9 GPS/location abstraction scenarios\n";
    return EXIT_SUCCESS;
}
