#include "opentrail/device_parameters.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {
using namespace opentrail::device_parameters;
using Wire = std::array<std::uint8_t, kRecordBytes>;

int failures = 0;
#define EXPECT(condition)                                                       \
    do {                                                                        \
        if (!(condition)) {                                                     \
            ++failures;                                                         \
            std::cerr << "FAIL line " << __LINE__ << ": " #condition << '\n';    \
        }                                                                       \
    } while (false)

constexpr std::size_t kStarts[] = {32, 40, 48, 60, 96, 104, 112, 120, 132, 140, 148, 164};
constexpr std::size_t kSizes[] = {8, 8, 12, 36, 8, 8, 8, 12, 8, 8, 16, 48};

void put16(Wire& wire, std::size_t at, std::uint16_t value) {
    wire[at] = static_cast<std::uint8_t>(value);
    wire[at + 1] = static_cast<std::uint8_t>(value >> 8);
}
void put32(Wire& wire, std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) wire[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

Snapshot sample() {
    Snapshot value{};
    value.sequence = 0x01020304;
    value.sampled_at_ms = 1'000'000;
    value.age_ms = 42;
    value.battery = {true, 1, {3700, 56}};
    value.power = {true, 2, PowerSource::battery_only};
    value.packet = {true, 3, {-84, -13, 915'000'000}};
    value.radio = {true, 4, {1, 2, 3, 4, 5, 6, 7, 8}};
    value.backoffs = {true, 5, {9}};
    value.gnss = {true, 6, {GnssState::fix, true, 12}};
    value.reset = {true, 7, ResetReason::software};
    value.heap = {true, 8, {200'000, 100'000}};
    value.main_stack = {true, 9, {12'000}};
    value.ble_stack = {true, 10, {5'000}};
    value.storage = {true, 11, {6, 4, 10}};
    value.firmware.available = true;
    value.firmware.age_ms = 12;
    value.firmware.value.release_generation = 7;
    value.firmware.value.application_partition_offset = 0x10000;
    const std::string version = "v1.2.3-test+4";
    value.firmware.value.version_length = static_cast<std::uint8_t>(version.size());
    std::copy(version.begin(), version.end(), value.firmware.value.version.begin());
    return value;
}

Wire encoded(const Snapshot& value) {
    Wire wire{};
    const auto result = encode(value, wire.data(), wire.size());
    EXPECT(result.error == Error::none && result.bytes == wire.size());
    return wire;
}

void expect_default_failure(const DecodeResult& result) {
    EXPECT(result.error != Error::none);
    const auto& value = result.snapshot;
    EXPECT(value.sequence == 0 && value.sampled_at_ms == 0 && value.age_ms == 0);
    EXPECT(!value.battery.available && !value.power.available && !value.packet.available);
    EXPECT(!value.radio.available && !value.backoffs.available && !value.gnss.available);
    EXPECT(!value.reset.available && !value.heap.available && !value.main_stack.available);
    EXPECT(!value.ble_stack.available && !value.storage.available && !value.firmware.available);
    EXPECT(value.battery.age_ms == 0 && value.power.age_ms == 0 && value.packet.age_ms == 0);
    EXPECT(value.radio.age_ms == 0 && value.backoffs.age_ms == 0 && value.gnss.age_ms == 0);
    EXPECT(value.reset.age_ms == 0 && value.heap.age_ms == 0 && value.main_stack.age_ms == 0);
    EXPECT(value.ble_stack.age_ms == 0 && value.storage.age_ms == 0 && value.firmware.age_ms == 0);
    EXPECT(value.battery.value.millivolts == 0 && value.battery.value.percent == 0);
    EXPECT(value.power.value == PowerSource::unknown);
    EXPECT(value.packet.value.rssi_dbm == 0 && value.packet.value.snr_quarter_db == 0 &&
           value.packet.value.frequency_hz == 0);
    const auto& radio = value.radio.value;
    EXPECT(radio.tx_attempts == 0 && radio.tx_completed == 0 && radio.rx_frames == 0 &&
           radio.rx_errors == 0 && radio.rx_dropped == 0 && radio.tx_refused == 0 &&
           radio.tx_busy_dropped == 0 && radio.tx_prelaunch_dropped == 0);
    EXPECT(value.backoffs.value.scheduled == 0);
    EXPECT(value.gnss.value.state == GnssState::unknown && !value.gnss.value.satellites_known &&
           value.gnss.value.satellites == 0);
    EXPECT(value.reset.value == ResetReason::unknown);
    EXPECT(value.heap.value.free_bytes == 0 && value.heap.value.minimum_free_bytes == 0);
    EXPECT(value.main_stack.value.minimum_free_bytes == 0 && value.ble_stack.value.minimum_free_bytes == 0);
    EXPECT(value.storage.value.used_entries == 0 && value.storage.value.free_entries == 0 &&
           value.storage.value.total_entries == 0);
    EXPECT(value.firmware.value.release_generation == 0 && value.firmware.value.version_length == 0 &&
           value.firmware.value.application_partition_offset == 0);
    EXPECT(std::all_of(value.firmware.value.version.begin(), value.firmware.value.version.end(),
                       [](char c) { return c == 0; }));
}

void expect_bad(const Wire& wire) {
    expect_default_failure(decode(wire.data(), wire.size()));
}

void expect_atomic_refusal(const Snapshot& value) {
    std::array<std::uint8_t, kRecordBytes + 5> wire{};
    wire.fill(0xa5);
    const auto before = wire;
    EXPECT(!valid(value));
    const auto result = encode(value, wire.data(), wire.size());
    EXPECT(result.error != Error::none && result.bytes == 0 && wire == before);
}

void test_layout_and_all_fields() {
    const auto value = sample();
    EXPECT(valid(value));
    const auto wire = encoded(value);
    const std::array<std::uint8_t, 32> header{
        'O', 'T', 'D', 'P', 1, 0, 212, 0, 4, 3, 2, 1, 42, 0, 0, 0,
        0x40, 0x42, 0x0f, 0, 0, 0, 0, 0, 0xff, 0x0f, 0, 0, 0, 0, 0, 0};
    EXPECT(std::equal(header.begin(), header.end(), wire.begin()));
    for (std::size_t i = 0; i < 12; ++i) {
        EXPECT(wire[kStarts[i]] == i + 1);
        EXPECT(wire[kStarts[i] + 1] == 0 && wire[kStarts[i] + 2] == 0 && wire[kStarts[i] + 3] == 0);
    }
    EXPECT(wire[36] == 0x74 && wire[37] == 0x0e && wire[38] == 56);
    EXPECT(wire[44] == 1);
    EXPECT(wire[52] == 0xac && wire[53] == 0xff && wire[54] == 0xf3 && wire[55] == 0xff);
    EXPECT(wire[56] == 0xc0 && wire[57] == 0xca && wire[58] == 0x89 && wire[59] == 0x36);
    for (std::size_t i = 0; i < 8; ++i) EXPECT(wire[64 + i * 4] == i + 1);
    EXPECT(wire[100] == 9 && wire[108] == 3 && wire[109] == 12 && wire[110] == 1);
    EXPECT(wire[116] == 3 && wire[168] == 7 && wire[172] == 13);
    EXPECT(wire[176] == 0 && wire[177] == 0 && wire[178] == 1 && wire[179] == 0);
    EXPECT(std::equal(value.firmware.value.version.begin(), value.firmware.value.version.end(), wire.begin() + 180));
    const auto result = decode(wire.data(), wire.size());
    EXPECT(result.error == Error::none && encoded(result.snapshot) == wire);

    std::array<std::uint8_t, kRecordBytes + 1> larger{};
    larger.fill(0xa5);
    EXPECT(encode(value, larger.data(), larger.size()).error == Error::none);
    EXPECT(larger.back() == 0xa5);
}

void test_sparse_groups_are_canonical() {
    const auto full = encoded(sample());
    for (std::size_t group = 0; group < 12; ++group) {
        auto wire = full;
        put32(wire, 24, kKnownFields & ~(1U << group));
        std::fill_n(wire.begin() + kStarts[group], kSizes[group], 0);
        auto result = decode(wire.data(), wire.size());
        EXPECT(result.error == Error::none && encoded(result.snapshot) == wire);
        for (std::size_t byte = kStarts[group]; byte < kStarts[group] + kSizes[group]; ++byte) {
            auto hidden = wire;
            hidden[byte] = 1;
            expect_bad(hidden);
        }
    }
    Snapshot empty{};
    empty.sequence = 1;
    const auto wire = encoded(empty);
    EXPECT(std::all_of(wire.begin() + 24, wire.end(), [](std::uint8_t b) { return b == 0; }));

    auto ignored = empty;
    ignored.battery.value = {1, 255};
    ignored.packet.value = {100, 1000, 1};
    ignored.firmware.value.version.fill('\n');
    EXPECT(encoded(ignored) == wire);
    ignored.battery.age_ms = 1;
    expect_atomic_refusal(ignored);
}

void test_buffer_and_prefix_refusals() {
    const auto wire = encoded(sample());
    for (std::size_t size = 0; size < wire.size(); ++size) {
        expect_default_failure(decode(wire.data(), size));
    }
    std::array<std::uint8_t, kRecordBytes + 1> longer{};
    std::copy(wire.begin(), wire.end(), longer.begin());
    expect_default_failure(decode(longer.data(), longer.size()));
    expect_default_failure(decode(nullptr, kRecordBytes));
    expect_default_failure(decode(nullptr, 0));
    for (std::size_t at = 0; at < 4; ++at) {
        auto altered = wire;
        altered[at] ^= 1;
        expect_bad(altered);
    }
    for (unsigned version = 0; version <= 255; ++version) {
        if (version == kVersion) continue;
        auto altered = wire;
        altered[4] = static_cast<std::uint8_t>(version);
        const auto result = decode(altered.data(), altered.size());
        EXPECT(result.error == Error::unsupported_version);
        expect_default_failure(result);
    }
    for (const unsigned size : {0U, 211U, 213U, 65535U}) {
        auto altered = wire;
        put16(altered, 6, static_cast<std::uint16_t>(size));
        expect_bad(altered);
    }
    auto impossible_time = wire;
    std::fill_n(impossible_time.begin() + 16, 8, 0xff);
    expect_bad(impossible_time);
    auto zero_sequence = wire;
    put32(zero_sequence, 8, 0);
    expect_bad(zero_sequence);
    for (const auto at : kStarts) {
        auto old_sample = wire;
        put32(old_sample, at, 1'000'001);
        expect_bad(old_sample);
    }
    for (unsigned bit = 12; bit < 32; ++bit) {
        auto altered = wire;
        put32(altered, 24, kKnownFields | (1U << bit));
        expect_bad(altered);
    }
    std::array<std::uint8_t, kRecordBytes> untouched{};
    untouched.fill(0xa5);
    const auto before = untouched;
    for (std::size_t size = 0; size < kRecordBytes; ++size) {
        const auto result = encode(sample(), untouched.data(), size);
        EXPECT(result.error != Error::none && result.bytes == 0 && untouched == before);
    }
    EXPECT(encode(sample(), nullptr, kRecordBytes).error == Error::invalid_argument);
}

void test_reserved_and_value_bytes() {
    const auto original = encoded(sample());
    for (const std::size_t at : {5U, 28U, 29U, 30U, 31U, 39U, 45U, 46U, 47U,
                                 111U, 117U, 118U, 119U, 173U, 174U, 175U}) {
        auto wire = original;
        wire[at] = 1;
        expect_bad(wire);
    }
    for (unsigned value = 5; value <= 255; ++value) {
        auto wire = original;
        wire[44] = static_cast<std::uint8_t>(value);
        expect_bad(wire);
    }
    for (unsigned value = 6; value <= 255; ++value) {
        auto wire = original;
        wire[108] = static_cast<std::uint8_t>(value);
        expect_bad(wire);
    }
    for (unsigned value = 9; value <= 255; ++value) {
        auto wire = original;
        wire[116] = static_cast<std::uint8_t>(value);
        expect_bad(wire);
    }
    auto wire = original;
    wire[110] = 2;
    expect_bad(wire);
    wire = original;
    wire[110] = 0;
    expect_bad(wire);  // a count with its validity flag clear is hidden data
    wire = original;
    wire[109] = 100;
    expect_bad(wire);
    for (const auto mv : {0U, 2799U, 4401U, 65535U}) {
        wire = original;
        put16(wire, 36, static_cast<std::uint16_t>(mv));
        expect_bad(wire);
    }
    wire = original;
    wire[38] = 101;
    expect_bad(wire);
    for (const int rssi : {-32768, -201, 1, 32767}) {
        wire = original;
        put16(wire, 52, static_cast<std::uint16_t>(rssi));
        expect_bad(wire);
    }
    for (const int snr : {-32768, -129, 128, 32767}) {
        wire = original;
        put16(wire, 54, static_cast<std::uint16_t>(snr));
        expect_bad(wire);
    }
    for (const auto hz : {0U, 99'999'999U, 1'000'000'001U, UINT32_MAX}) {
        wire = original;
        put32(wire, 56, hz);
        expect_bad(wire);
    }
}

void test_resource_and_firmware_ranges() {
    const auto original = encoded(sample());
    auto wire = original;
    put32(wire, 124, kMaximumHeapBytes + 1);
    expect_bad(wire);
    wire = original;
    put32(wire, 128, 200'001);
    expect_bad(wire);
    for (const std::size_t at : {136U, 144U}) {
        wire = original;
        put32(wire, at, kMaximumStackBytes + 1);
        expect_bad(wire);
    }
    for (const std::array<std::uint32_t, 3> entries :
         {std::array<std::uint32_t, 3>{0, 0, 0}, {1, 1, 1}, {UINT32_MAX, 2, 1},
          {1'000'001, 0, 1'000'001}}) {
        wire = original;
        put32(wire, 152, entries[0]);
        put32(wire, 156, entries[1]);
        put32(wire, 160, entries[2]);
        expect_bad(wire);
    }
    wire = original;
    put32(wire, 168, 1'000'000'000);
    expect_bad(wire);
    for (const unsigned length : {0U, 33U, 255U}) {
        wire = original;
        wire[172] = static_cast<std::uint8_t>(length);
        expect_bad(wire);
    }
    for (const auto offset : {1U, 0xffffU, 0x10001U, 0x01000000U, UINT32_MAX}) {
        wire = original;
        put32(wire, 176, offset);
        expect_bad(wire);
    }
    for (const unsigned byte : {0U, 10U, 13U, 32U, 47U, 58U, 92U, 127U, 255U}) {
        wire = original;
        wire[180] = static_cast<std::uint8_t>(byte);
        expect_bad(wire);
    }
    wire = original;
    wire[180 + sample().firmware.value.version_length] = 'x';
    expect_bad(wire);
}

void test_encode_ranges_and_age_boundaries() {
    auto value = sample();
    value.sequence = 0;
    expect_atomic_refusal(value);
    value = sample();
    value.sampled_at_ms = UINT64_MAX;
    value.age_ms = 1;
    expect_atomic_refusal(value);
    value = sample();
    value.battery.age_ms = 1'000'001;
    expect_atomic_refusal(value);
    value = sample();
    value.battery.value.millivolts = 2799;
    expect_atomic_refusal(value);
    value = sample();
    value.power.value = static_cast<PowerSource>(255);
    expect_atomic_refusal(value);
    value = sample();
    value.packet.value.rssi_dbm = 1;
    expect_atomic_refusal(value);
    value = sample();
    value.packet.value.snr_quarter_db = 128;
    expect_atomic_refusal(value);
    value = sample();
    value.packet.value.frequency_hz = 1;
    expect_atomic_refusal(value);
    value = sample();
    value.gnss.value.satellites = 100;
    expect_atomic_refusal(value);
    value = sample();
    value.reset.value = static_cast<ResetReason>(255);
    expect_atomic_refusal(value);
    value = sample();
    value.heap.value.free_bytes = kMaximumHeapBytes + 1;
    expect_atomic_refusal(value);
    value = sample();
    value.main_stack.value.minimum_free_bytes = kMaximumStackBytes + 1;
    expect_atomic_refusal(value);
    value = sample();
    value.storage.value = {UINT32_MAX, 2, 1};
    expect_atomic_refusal(value);
    value = sample();
    value.firmware.value.version[0] = '\n';
    expect_atomic_refusal(value);

    value = sample();
    value.battery.value = {2800, 0};
    value.packet.value = {-200, -128, 100'000'000};
    value.heap.value = {kMaximumHeapBytes, kMaximumHeapBytes};
    value.main_stack.value.minimum_free_bytes = kMaximumStackBytes;
    value.ble_stack.value.minimum_free_bytes = 0;
    value.storage.value = {1'000'000, 0, 1'000'000};
    value.firmware.value.release_generation = 999'999'999;
    value.firmware.value.application_partition_offset = 0x00ff0000;
    EXPECT(valid(value));
    EXPECT(decode(encoded(value).data(), kRecordBytes).error == Error::none);
    value.battery.value = {4400, 100};
    value.packet.value = {0, 127, 1'000'000'000};
    value.gnss.value = {GnssState::unknown, false, 0};
    value.firmware.value.application_partition_offset = 0;
    EXPECT(valid(value));
    EXPECT(decode(encoded(value).data(), kRecordBytes).error == Error::none);

    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._+-";
    for (std::size_t start = 0; start < alphabet.size(); ++start) {
        value = sample();
        value.firmware.value.version_length = 32;
        for (std::size_t i = 0; i < 32; ++i) value.firmware.value.version[i] = alphabet[(start + i) % alphabet.size()];
        const auto wire = encoded(value);
        EXPECT(decode(wire.data(), wire.size()).error == Error::none);
    }
}

void test_single_bit_mutations_stay_canonical() {
    const auto original = encoded(sample());
    unsigned successes = 0;
    unsigned refused = 0;
    for (std::size_t index = 0; index < original.size(); ++index) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            auto mutated = original;
            mutated[index] ^= static_cast<std::uint8_t>(1U << bit);
            const auto result = decode(mutated.data(), mutated.size());
            if (result.error == Error::none) {
                EXPECT(valid(result.snapshot) && encoded(result.snapshot) == mutated);
                ++successes;
            } else {
                expect_default_failure(result);
                ++refused;
            }
        }
    }
    EXPECT(successes != 0 && refused != 0 && successes + refused == kRecordBytes * 8);
    std::cout << "Codec mutation cases accepted=" << successes << " refused=" << refused << '\n';
}

}  // namespace

int main() {
    test_layout_and_all_fields();
    test_sparse_groups_are_canonical();
    test_buffer_and_prefix_refusals();
    test_reserved_and_value_bytes();
    test_resource_and_firmware_ranges();
    test_encode_ranges_and_age_boundaries();
    test_single_bit_mutations_stay_canonical();
    if (failures != 0) {
        std::cerr << failures << " device parameters codec assertions failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 7 device parameters codec groups\n";
    return EXIT_SUCCESS;
}
