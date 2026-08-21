#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

#include "../host_support/compact_status_footer.hpp"

namespace {

namespace footer = opentrail::ui::compact_status_footer;

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

footer::Metric metric(
    footer::ObservationState state,
    std::uint16_t value,
    std::uint64_t sampled_at_ms) {
    return {state, value, sampled_at_ms};
}

std::array<std::uint8_t, footer::kGlyphWidth> glyph_at(
    const footer::Page& page,
    std::size_t start_x) {
    std::array<std::uint8_t, footer::kGlyphWidth> value{};
    for (std::size_t index = 0; index < value.size(); ++index) {
        value[index] = page.columns[start_x + index];
    }
    return value;
}

footer::Snapshot valid_snapshot(
    std::uint16_t battery,
    std::uint16_t satellites,
    std::uint64_t sampled_at_ms) {
    footer::Snapshot snapshot{};
    snapshot.battery_percent = metric(
        footer::ObservationState::valid, battery, sampled_at_ms);
    snapshot.gps_satellites = metric(
        footer::ObservationState::valid, satellites, sampled_at_ms);
    snapshot.ble = footer::BleCode::connected;
    return snapshot;
}

void test_constants_and_default_unavailable_fields() {
    EXPECT(footer::kWidth == 128);
    EXPECT(footer::kHeight == 8);
    EXPECT(footer::kGlyphWidth == 5);
    const auto fields = footer::format({}, {1, 1}, 0);
    EXPECT(visible(fields.battery) == "BAT:--%");
    EXPECT(visible(fields.gps) == "GPS:--");
    EXPECT(visible(fields.ble) == "BLE:-");
}

void test_battery_valid_boundaries_and_compact_width() {
    const std::array<std::pair<std::uint16_t, const char*>, 7> cases{{
        {0, "BAT:0%"}, {9, "BAT:9%"}, {10, "BAT:10%"},
        {42, "BAT:42%"}, {99, "BAT:99%"}, {100, "BAT:100%"},
        {1, "BAT:1%"},
    }};
    for (const auto& item : cases) {
        const auto fields = footer::format(
            valid_snapshot(item.first, 0, 10), {100, 100}, 10);
        EXPECT(visible(fields.battery) == item.second);
        EXPECT(fields.battery.size() == 8);
    }
}

void test_battery_invalid_unavailable_and_out_of_range_are_hidden() {
    auto snapshot = valid_snapshot(50, 0, 10);
    const auto freshness = footer::Freshness{100, 100};
    snapshot.battery_percent.state = footer::ObservationState::invalid;
    EXPECT(visible(footer::format(snapshot, freshness, 10).battery) ==
           "BAT:--%");
    snapshot.battery_percent.state = footer::ObservationState::unavailable;
    EXPECT(visible(footer::format(snapshot, freshness, 10).battery) ==
           "BAT:--%");
    snapshot.battery_percent.state =
        static_cast<footer::ObservationState>(0xFF);
    EXPECT(visible(footer::format(snapshot, freshness, 10).battery) ==
           "BAT:--%");
    snapshot.battery_percent = metric(
        footer::ObservationState::valid, 101, 10);
    EXPECT(visible(footer::format(snapshot, freshness, 10).battery) ==
           "BAT:--%");
    snapshot.battery_percent.value = 65535;
    EXPECT(visible(footer::format(snapshot, freshness, 10).battery) ==
           "BAT:--%");
}

void test_battery_freshness_future_expiry_and_uint64_boundaries() {
    const auto snapshot = valid_snapshot(88, 4, 100);
    EXPECT(visible(footer::format(snapshot, {0, 100}, 100).battery) ==
           "BAT:--%");
    EXPECT(visible(footer::format(snapshot, {100, 100}, 99).battery) ==
           "BAT:--%");
    EXPECT(visible(footer::format(snapshot, {100, 100}, 199).battery) ==
           "BAT:88%");
    EXPECT(visible(footer::format(snapshot, {100, 100}, 200).battery) ==
           "BAT:--%");

    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto near_wrap = valid_snapshot(77, 4, maximum - 5U);
    EXPECT(visible(footer::format(near_wrap, {6, 6}, maximum).battery) ==
           "BAT:77%");
    EXPECT(visible(footer::format(near_wrap, {5, 6}, maximum).battery) ==
           "BAT:--%");
}

void test_gps_valid_boundaries_and_compact_width() {
    const std::array<std::pair<std::uint16_t, const char*>, 6> cases{{
        {0, "GPS:0"}, {1, "GPS:1"}, {9, "GPS:9"},
        {10, "GPS:10"}, {12, "GPS:12"}, {99, "GPS:99"},
    }};
    for (const auto& item : cases) {
        const auto fields = footer::format(
            valid_snapshot(50, item.first, 10), {100, 100}, 10);
        EXPECT(visible(fields.gps) == item.second);
        EXPECT(fields.gps.size() == 6);
    }
}

void test_gps_invalid_stale_future_and_out_of_range_are_hidden() {
    auto snapshot = valid_snapshot(50, 12, 100);
    EXPECT(visible(footer::format(snapshot, {100, 0}, 100).gps) == "GPS:--");
    EXPECT(visible(footer::format(snapshot, {100, 10}, 99).gps) == "GPS:--");
    EXPECT(visible(footer::format(snapshot, {100, 10}, 109).gps) == "GPS:12");
    EXPECT(visible(footer::format(snapshot, {100, 10}, 110).gps) == "GPS:--");
    snapshot.gps_satellites.value = 100;
    EXPECT(visible(footer::format(snapshot, {100, 100}, 100).gps) == "GPS:--");
    snapshot.gps_satellites.value = 65535;
    EXPECT(visible(footer::format(snapshot, {100, 100}, 100).gps) == "GPS:--");
    snapshot.gps_satellites = metric(
        footer::ObservationState::invalid, 12, 100);
    EXPECT(visible(footer::format(snapshot, {100, 100}, 100).gps) == "GPS:--");
}

void test_ble_code_set_and_unknown_value_fail_closed() {
    auto snapshot = valid_snapshot(50, 12, 10);
    const std::array<std::pair<footer::BleCode, const char*>, 6> cases{{
        {footer::BleCode::unavailable, "BLE:-"},
        {footer::BleCode::starting, "BLE:S"},
        {footer::BleCode::advertising, "BLE:A"},
        {footer::BleCode::connected, "BLE:C"},
        {footer::BleCode::retrying, "BLE:R"},
        {footer::BleCode::error, "BLE:E"},
    }};
    for (const auto& item : cases) {
        snapshot.ble = item.first;
        EXPECT(visible(footer::format(snapshot, {100, 100}, 10).ble) ==
               item.second);
    }
    snapshot.ble = static_cast<footer::BleCode>(0xFF);
    EXPECT(visible(footer::format(snapshot, {100, 100}, 10).ble) == "BLE:-");
}

void test_exact_numeric_punctuation_and_activity_glyphs() {
    const std::array<std::array<std::uint8_t, footer::kGlyphWidth>, 10> digits{{
        {{0x3E, 0x51, 0x49, 0x45, 0x3E}},
        {{0x00, 0x42, 0x7F, 0x40, 0x00}},
        {{0x42, 0x61, 0x51, 0x49, 0x46}},
        {{0x21, 0x41, 0x45, 0x4B, 0x31}},
        {{0x18, 0x14, 0x12, 0x7F, 0x10}},
        {{0x27, 0x45, 0x45, 0x45, 0x39}},
        {{0x3C, 0x4A, 0x49, 0x49, 0x30}},
        {{0x01, 0x71, 0x09, 0x05, 0x03}},
        {{0x36, 0x49, 0x49, 0x49, 0x36}},
        {{0x06, 0x49, 0x49, 0x29, 0x1E}},
    }};
    const std::array<std::uint8_t, footer::kGlyphWidth> colon{
        0x00, 0x36, 0x36, 0x00, 0x00};
    const std::array<std::uint8_t, footer::kGlyphWidth> percent{
        0x63, 0x13, 0x08, 0x64, 0x63};
    const std::array<std::uint8_t, footer::kGlyphWidth> dash{
        0x08, 0x08, 0x08, 0x08, 0x08};
    const std::array<std::uint8_t, footer::kGlyphWidth> up{
        0x04, 0x02, 0x7F, 0x02, 0x04};
    const std::array<std::uint8_t, footer::kGlyphWidth> down{
        0x10, 0x20, 0x7F, 0x20, 0x10};
    const std::array<std::uint8_t, footer::kGlyphWidth> blank{};
    footer::ActivityOwner unsupported(false, 100);

    for (std::uint16_t value = 0; value <= 9; ++value) {
        const auto fields = footer::format(
            valid_snapshot(value, 12, 10), {100, 100}, 10);
        const auto page = footer::render(fields, unsupported, 10);
        EXPECT(glyph_at(page, 25) == digits[value]);
    }
    auto fields = footer::format(valid_snapshot(0, 12, 10), {100, 100}, 10);
    EXPECT(glyph_at(footer::render(fields, unsupported, 10), 19) == colon);
    EXPECT(glyph_at(footer::render(fields, unsupported, 10), 31) == percent);
    auto invalid = valid_snapshot(0, 12, 10);
    invalid.battery_percent.state = footer::ObservationState::invalid;
    fields = footer::format(invalid, {100, 100}, 10);
    EXPECT(glyph_at(footer::render(fields, unsupported, 10), 25) == dash);

    fields = footer::format(valid_snapshot(100, 12, 10), {100, 100}, 10);
    footer::ActivityOwner activity(true, 100);
    EXPECT(glyph_at(footer::render(fields, activity, 10), 121) == blank);
    EXPECT(!activity.observe_accepted_transport_event(
        static_cast<footer::Direction>(0xFF), 10));
    EXPECT(glyph_at(footer::render(fields, activity, 10), 121) == blank);
    EXPECT(activity.observe_accepted_transport_event(footer::Direction::tx, 10));
    EXPECT(glyph_at(footer::render(fields, activity, 109), 121) == up);
    EXPECT(glyph_at(footer::render(fields, activity, 110), 121) == blank);
    EXPECT(activity.observe_accepted_transport_event(footer::Direction::rx, 200));
    EXPECT(glyph_at(footer::render(fields, activity, 200), 121) == down);
    EXPECT(glyph_at(footer::render(fields, unsupported, 200), 121) == blank);
    EXPECT(activity.observe_accepted_transport_event(footer::Direction::tx, 300));
    EXPECT(glyph_at(footer::render(fields, activity, 299), 121) == blank);
}
void test_render_geometry_gaps_height_and_unknown_glyphs() {
    auto fields = footer::format(valid_snapshot(100, 12, 10), {100, 100}, 10);
    footer::ActivityOwner activity(true, 100);
    EXPECT(activity.observe_accepted_transport_event(footer::Direction::tx, 10));
    auto page = footer::render(fields, activity, 10);
    EXPECT(page.columns.size() == footer::kWidth);
    for (const auto column : page.columns) {
        EXPECT((column & 0x80U) == 0U);
    }
    for (const auto index : std::array<std::size_t, 9>{
             0, 48, 49, 50, 86, 87, 88, 118, 120}) {
        EXPECT(page.columns[index] == 0U);
    }
    EXPECT(page.columns[47] != 0U);
    EXPECT(page.columns[85] != 0U);
    EXPECT(page.columns[117] != 0U);
    EXPECT(page.columns[121] != 0U);
    EXPECT(page.columns[125] != 0U);
    EXPECT(page.columns[126] == 0U);
    EXPECT(page.columns[127] == 0U);

    fields.battery[0] = '?';
    page = footer::render(fields, activity, 10);
    for (std::size_t index = 1; index <= 5; ++index) {
        EXPECT(page.columns[index] == 0U);
    }
}

void test_render_exact_128_column_golden() {
    footer::ActivityOwner activity(true, 100);
    EXPECT(activity.observe_accepted_transport_event(footer::Direction::tx, 10));
    const auto page = footer::render(
        footer::format(valid_snapshot(100, 12, 10), {100, 100}, 10),
        activity, 10);
    const std::array<std::uint8_t, 128> expected{{
        0x00, 0x7F, 0x49, 0x49, 0x49, 0x36, 0x00, 0x7E,
        0x11, 0x11, 0x11, 0x7E, 0x00, 0x01, 0x01, 0x7F,
        0x01, 0x01, 0x00, 0x00, 0x36, 0x36, 0x00, 0x00,
        0x00, 0x00, 0x42, 0x7F, 0x40, 0x00, 0x00, 0x3E,
        0x51, 0x49, 0x45, 0x3E, 0x00, 0x3E, 0x51, 0x49,
        0x45, 0x3E, 0x00, 0x63, 0x13, 0x08, 0x64, 0x63,
        0x00, 0x00, 0x00, 0x3E, 0x41, 0x49, 0x49, 0x7A,
        0x00, 0x7F, 0x09, 0x09, 0x09, 0x06, 0x00, 0x46,
        0x49, 0x49, 0x49, 0x31, 0x00, 0x00, 0x36, 0x36,
        0x00, 0x00, 0x00, 0x00, 0x42, 0x7F, 0x40, 0x00,
        0x00, 0x42, 0x61, 0x51, 0x49, 0x46, 0x00, 0x00,
        0x00, 0x7F, 0x49, 0x49, 0x49, 0x36, 0x00, 0x7F,
        0x40, 0x40, 0x40, 0x40, 0x00, 0x7F, 0x49, 0x49,
        0x49, 0x41, 0x00, 0x00, 0x36, 0x36, 0x00, 0x00,
        0x00, 0x3E, 0x41, 0x41, 0x41, 0x22, 0x00, 0x00,
        0x00, 0x04, 0x02, 0x7F, 0x02, 0x04, 0x00, 0x00,
    }};
    EXPECT(page.columns == expected);
}

void test_activity_latest_equal_time_future_and_expiry() {
    footer::ActivityOwner owner(true, 100);
    EXPECT(owner.visible_direction(0) == footer::Direction::none);
    EXPECT(owner.observe_accepted_transport_event(footer::Direction::tx, 10));
    EXPECT(owner.visible_direction(9) == footer::Direction::none);
    EXPECT(owner.visible_direction(10) == footer::Direction::tx);
    EXPECT(owner.visible_direction(109) == footer::Direction::tx);
    EXPECT(owner.visible_direction(110) == footer::Direction::none);
    EXPECT(owner.observe_accepted_transport_event(footer::Direction::rx, 20));
    EXPECT(owner.visible_direction(20) == footer::Direction::rx);
    EXPECT(owner.observe_accepted_transport_event(footer::Direction::tx, 20));
    EXPECT(owner.visible_direction(20) == footer::Direction::tx);
}

void test_activity_rejections_clear_and_uint64_boundaries() {
    footer::ActivityOwner unsupported(false, 100);
    EXPECT(!unsupported.observe_accepted_transport_event(
        footer::Direction::tx, 1));
    EXPECT(unsupported.visible_direction(1) == footer::Direction::none);
    footer::ActivityOwner zero_lifetime(true, 0);
    EXPECT(!zero_lifetime.observe_accepted_transport_event(
        footer::Direction::rx, 1));

    footer::ActivityOwner owner(true, 10);
    EXPECT(!owner.observe_accepted_transport_event(footer::Direction::none, 1));
    EXPECT(!owner.observe_accepted_transport_event(
        static_cast<footer::Direction>(0xFF), 1));
    EXPECT(owner.observe_accepted_transport_event(footer::Direction::tx, 20));
    EXPECT(!owner.observe_accepted_transport_event(footer::Direction::rx, 19));
    EXPECT(owner.visible_direction(20) == footer::Direction::tx);
    owner.clear();
    EXPECT(owner.visible_direction(20) == footer::Direction::none);

    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    EXPECT(owner.observe_accepted_transport_event(
        footer::Direction::rx, maximum - 5U));
    EXPECT(owner.visible_direction(maximum) == footer::Direction::rx);
    EXPECT(!owner.observe_accepted_transport_event(
        footer::Direction::tx, maximum - 6U));
}

}  // namespace

int main() {
    test_constants_and_default_unavailable_fields();
    test_battery_valid_boundaries_and_compact_width();
    test_battery_invalid_unavailable_and_out_of_range_are_hidden();
    test_battery_freshness_future_expiry_and_uint64_boundaries();
    test_gps_valid_boundaries_and_compact_width();
    test_gps_invalid_stale_future_and_out_of_range_are_hidden();
    test_ble_code_set_and_unknown_value_fail_closed();
    test_exact_numeric_punctuation_and_activity_glyphs();
    test_render_geometry_gaps_height_and_unknown_glyphs();
    test_render_exact_128_column_golden();
    test_activity_latest_equal_time_future_and_expiry();
    test_activity_rejections_clear_and_uint64_boundaries();

    if (failures != 0) {
        std::cerr << failures << " compact status footer assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 12 compact status footer scenario groups\n";
    return EXIT_SUCCESS;
}
