#include "opentrail/device_parameters.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
using namespace opentrail::device_parameters;

int failures = 0;
constexpr auto kMaximumAge = std::numeric_limits<std::uint32_t>::max();

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

using Wire = std::array<std::uint8_t, kRecordBytes>;

Wire encoded(const Snapshot& snapshot) {
    Wire wire{};
    const auto result = encode(snapshot, wire.data(), wire.size());
    EXPECT(result.error == Error::none && result.bytes == wire.size());
    return wire;
}

Inputs complete_inputs(std::uint64_t observed_at_ms) {
    Inputs inputs{};
    inputs.battery = {true, observed_at_ms, {3900, 67}};
    inputs.power = {true, observed_at_ms, PowerSource::battery_only};
    inputs.packet = {true, observed_at_ms, {-91, -17, 915000000}};
    inputs.radio = {true, observed_at_ms, {19, 18, 17, 2, 1, 3, 4, 5}};
    inputs.backoffs = {true, observed_at_ms, {9}};
    inputs.gnss = {true, observed_at_ms, {GnssState::fix, true, 8}};
    inputs.reset = {true, observed_at_ms, ResetReason::power_on};
    inputs.heap = {true, observed_at_ms, {100000, 90000}};
    inputs.main_stack = {true, observed_at_ms, {4096}};
    inputs.ble_stack = {true, observed_at_ms, {2048}};
    inputs.storage = {true, observed_at_ms, {120, 32256 - 120, 32256}};
    Firmware firmware{};
    firmware.release_generation = 42;
    firmware.application_partition_offset = 0x10000;
    constexpr char version[] = "v1.2+dev";
    firmware.version_length = sizeof(version) - 1;
    for (std::size_t index = 0; index < firmware.version_length; ++index) {
        firmware.version[index] = version[index];
    }
    inputs.firmware = {true, observed_at_ms, firmware};
    return inputs;
}

Snapshot sentinel() {
    Snapshot snapshot{};
    snapshot.sequence = 77;
    snapshot.sampled_at_ms = 5;
    return snapshot;
}

void collects_coherent_fields_and_ages() {
    Collector collector{7};
    auto inputs = complete_inputs(10);
    inputs.packet.observed_at_ms = 19;
    Snapshot snapshot{};
    EXPECT(collector.collect(inputs, 25, snapshot) == Error::none);
    EXPECT(snapshot.sequence == 7 && snapshot.sampled_at_ms == 25 && snapshot.age_ms == 0);
    EXPECT(snapshot.battery.available && snapshot.battery.age_ms == 15);
    EXPECT(snapshot.battery.value.millivolts == 3900 && snapshot.battery.value.percent == 67);
    EXPECT(snapshot.power.value == PowerSource::battery_only);
    EXPECT(snapshot.packet.age_ms == 6 && snapshot.packet.value.rssi_dbm == -91);
    EXPECT(snapshot.packet.value.snr_quarter_db == -17);
    EXPECT(snapshot.packet.value.frequency_hz == 915000000);
    EXPECT(snapshot.radio.value.tx_attempts == 19 && snapshot.radio.value.tx_prelaunch_dropped == 5);
    EXPECT(snapshot.backoffs.value.scheduled == 9);
    EXPECT(snapshot.gnss.value.state == GnssState::fix && snapshot.gnss.value.satellites == 8);
    EXPECT(snapshot.reset.value == ResetReason::power_on);
    EXPECT(snapshot.heap.value.free_bytes == 100000 && snapshot.heap.age_ms == 15);
    EXPECT(snapshot.main_stack.value.minimum_free_bytes == 4096 && snapshot.main_stack.age_ms == 15);
    EXPECT(snapshot.ble_stack.value.minimum_free_bytes == 2048 && snapshot.ble_stack.age_ms == 15);
    EXPECT(snapshot.storage.value.total_entries == 32256 && snapshot.storage.age_ms == 15);
    EXPECT(snapshot.firmware.value.version_length == 8 && snapshot.firmware.age_ms == 15);
    EXPECT(valid(snapshot));
}

void unavailable_observations_are_canonical() {
    auto inputs = complete_inputs(200);
    inputs.battery.available = false;
    inputs.power.available = false;
    inputs.packet.available = false;
    inputs.radio.available = false;
    inputs.backoffs.available = false;
    inputs.gnss.available = false;
    inputs.reset.available = false;
    inputs.heap.available = false;
    inputs.main_stack.available = false;
    inputs.ble_stack.available = false;
    inputs.storage.available = false;
    inputs.firmware.available = false;
    // Unavailable source values and future source times must be ignored.
    inputs.battery.value = {65535, 255};
    inputs.storage.value = {1, 1, 0};
    inputs.firmware.value.version_length = 255;

    Collector collector;
    Snapshot actual = sentinel();
    EXPECT(collector.collect(inputs, 100, actual) == Error::none);
    Snapshot expected{};
    expected.sequence = 1;
    expected.sampled_at_ms = 100;
    EXPECT(encoded(actual) == encoded(expected));
}

void every_available_future_time_refuses_transactionally() {
    std::array<Inputs, 12> cases{};
    for (auto& inputs : cases) inputs = complete_inputs(50);
    cases[0].battery.observed_at_ms = 101;
    cases[1].power.observed_at_ms = 101;
    cases[2].packet.observed_at_ms = 101;
    cases[3].radio.observed_at_ms = 101;
    cases[4].backoffs.observed_at_ms = 101;
    cases[5].gnss.observed_at_ms = 101;
    cases[6].reset.observed_at_ms = 101;
    cases[7].heap.observed_at_ms = 101;
    cases[8].main_stack.observed_at_ms = 101;
    cases[9].ble_stack.observed_at_ms = 101;
    cases[10].storage.observed_at_ms = 101;
    cases[11].firmware.observed_at_ms = 101;
    for (const auto& inputs : cases) {
        Collector collector{9};
        auto output = sentinel();
        const auto before = encoded(output);
        EXPECT(collector.collect(inputs, 100, output) == Error::clock_regression);
        EXPECT(encoded(output) == before);
        EXPECT(collector.collect(Inputs{}, 20, output) == Error::none);
        EXPECT(output.sequence == 9 && output.sampled_at_ms == 20);
    }
}

void clock_rollback_preserves_collection_state() {
    Collector collector;
    Snapshot output{};
    EXPECT(collector.collect(Inputs{}, 100, output) == Error::none);
    const auto before = encoded(output);
    EXPECT(collector.collect(Inputs{}, 99, output) == Error::clock_regression);
    EXPECT(encoded(output) == before);
    EXPECT(collector.collect(Inputs{}, 100, output) == Error::none);
    EXPECT(output.sequence == 2 && output.sampled_at_ms == 100);
}

void invalid_values_preserve_collection_state() {
    auto inputs = complete_inputs(1);
    inputs.battery.value.millivolts = 4401;
    Collector collector{4};
    auto output = sentinel();
    const auto before = encoded(output);
    EXPECT(collector.collect(inputs, 100, output) == Error::invalid_value);
    EXPECT(encoded(output) == before);
    inputs.battery.value.millivolts = 3900;
    EXPECT(collector.collect(inputs, 2, output) == Error::none);
    EXPECT(output.sequence == 4 && output.sampled_at_ms == 2);
}

void collection_ages_saturate() {
    Collector collector;
    Snapshot output{};
    const auto now_ms = static_cast<std::uint64_t>(kMaximumAge) + 42;
    EXPECT(collector.collect(complete_inputs(0), now_ms, output) == Error::none);
    EXPECT(output.battery.age_ms == kMaximumAge && output.packet.age_ms == kMaximumAge);
    EXPECT(output.firmware.age_ms == kMaximumAge && output.storage.age_ms == kMaximumAge);
    EXPECT(output.sequence == 1 && output.sampled_at_ms == now_ms);
    EXPECT(collector.collect(Inputs{}, now_ms, output) == Error::none);
    EXPECT(output.sequence == 2);
}

void sequence_exhaustion_never_wraps() {
    Collector collector{kMaximumAge};
    Snapshot output{};
    EXPECT(collector.collect(Inputs{}, 0, output) == Error::none);
    EXPECT(output.sequence == kMaximumAge);
    const auto before = encoded(output);
    EXPECT(collector.collect(Inputs{}, 1, output) == Error::sequence_exhausted);
    EXPECT(encoded(output) == before);
    EXPECT(collector.collect(Inputs{}, 0, output) == Error::sequence_exhausted);
    EXPECT(encoded(output) == before);
    Collector zero{0};
    EXPECT(zero.collect(Inputs{}, 0, output) == Error::sequence_exhausted);
    EXPECT(encoded(output) == before);
}

void served_snapshot_preserves_collection_and_source_ages() {
    Collector collector{3};
    Snapshot collected{};
    EXPECT(collector.collect(complete_inputs(40), 100, collected) == Error::none);
    Snapshot response{};
    EXPECT(serve(collected, 160, response) == Error::none);
    EXPECT(response.sequence == 3 && response.sampled_at_ms == 100 && response.age_ms == 60);
    EXPECT(response.battery.age_ms == 60 && response.gnss.age_ms == 60);
    EXPECT(effective_age_ms(response.battery.age_ms, response.age_ms) == 120);

    const auto wire = encoded(response);
    const auto received = decode(wire.data(), wire.size());
    EXPECT(received.error == Error::none);
    EXPECT(encoded(received.snapshot) == wire);
    EXPECT(effective_age_ms(received.snapshot.packet.age_ms, received.snapshot.age_ms) == 120);

    EXPECT(serve(response, 160, response) == Error::none);
    EXPECT(encoded(response) == wire);
    EXPECT(serve(response, 170, response) == Error::none);
    EXPECT(response.age_ms == 70 && response.battery.age_ms == 60);
    EXPECT(response.sampled_at_ms == 100 && response.sequence == 3);
}

void failed_serve_preserves_output_and_aliased_input() {
    Collector collector;
    Snapshot source{};
    EXPECT(collector.collect(Inputs{}, 100, source) == Error::none);
    auto output = sentinel();
    const auto before = encoded(output);
    EXPECT(serve(source, 99, output) == Error::clock_regression);
    EXPECT(encoded(output) == before);
    source.age_ms = 10;
    EXPECT(serve(source, 109, output) == Error::clock_regression);
    EXPECT(encoded(output) == before);
    const auto input_before = encoded(source);
    EXPECT(serve(source, 109, source) == Error::clock_regression);
    EXPECT(encoded(source) == input_before);

    auto invalid = source;
    invalid.battery = {true, 0, {5000, 100}};
    EXPECT(serve(invalid, 150, output) == Error::invalid_value);
    EXPECT(encoded(output) == before);
    Snapshot saturated{};
    EXPECT(serve(source, std::numeric_limits<std::uint64_t>::max(), saturated) == Error::none);
    EXPECT(saturated.age_ms == kMaximumAge);
}

void effective_ages_saturate_without_integer_wrap() {
    EXPECT(effective_age_ms(0, 0) == 0);
    EXPECT(effective_age_ms(10, 20) == 30);
    EXPECT(effective_age_ms(kMaximumAge, 0) == kMaximumAge);
    EXPECT(effective_age_ms(kMaximumAge - 1, 1) == kMaximumAge);
    EXPECT(effective_age_ms(kMaximumAge - 1, 2) == kMaximumAge);
    EXPECT(effective_age_ms(kMaximumAge, kMaximumAge) == kMaximumAge);
}
}  // namespace

int main() {
    collects_coherent_fields_and_ages();
    unavailable_observations_are_canonical();
    every_available_future_time_refuses_transactionally();
    clock_rollback_preserves_collection_state();
    invalid_values_preserve_collection_state();
    collection_ages_saturate();
    sequence_exhaustion_never_wraps();
    served_snapshot_preserves_collection_and_source_ages();
    failed_serve_preserves_output_and_aliased_input();
    effective_ages_saturate_without_integer_wrap();
    if (failures != 0) {
        std::cerr << failures << " device parameter collector assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 device parameter collector scenario groups\n";
    return EXIT_SUCCESS;
}
