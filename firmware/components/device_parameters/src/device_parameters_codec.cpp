#include "opentrail/device_parameters.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace opentrail::device_parameters {
namespace {

bool value_valid(const Battery& v) {
    return v.millivolts >= 2800 && v.millivolts <= 4400 && v.percent <= 100;
}
bool value_valid(PowerSource v) {
    return static_cast<std::uint8_t>(v) <= static_cast<std::uint8_t>(PowerSource::fault);
}
bool value_valid(const Packet& v) {
    return v.rssi_dbm >= -200 && v.rssi_dbm <= 0 &&
           v.snr_quarter_db >= -128 && v.snr_quarter_db <= 127 &&
           v.frequency_hz >= 100000000U && v.frequency_hz <= 1000000000U;
}
bool value_valid(const RadioCounters&) { return true; }
bool value_valid(const Backoffs&) { return true; }
bool value_valid(const Gnss& v) {
    return static_cast<std::uint8_t>(v.state) <= static_cast<std::uint8_t>(GnssState::invalid) &&
           v.satellites <= 99 && (v.satellites_known || v.satellites == 0);
}
bool value_valid(ResetReason v) {
    return static_cast<std::uint8_t>(v) <= static_cast<std::uint8_t>(ResetReason::other);
}
bool value_valid(const Heap& v) {
    return v.free_bytes <= kMaximumHeapBytes && v.minimum_free_bytes <= v.free_bytes;
}
bool value_valid(const Stack& v) {
    return v.minimum_free_bytes <= kMaximumStackBytes;
}
bool value_valid(const Storage& v) {
    return v.total_entries != 0 && v.total_entries <= 1000000U &&
           static_cast<std::uint64_t>(v.used_entries) + v.free_entries == v.total_entries;
}
bool version_character(char value) {
    const auto c = static_cast<unsigned char>(value);
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '+';
}
}  // namespace

bool valid_firmware(const Firmware& v) {
    if (v.release_generation >= 1000000000U || v.version_length == 0 ||
        v.version_length > v.version.size() ||
        (v.application_partition_offset != 0 &&
         (v.application_partition_offset < 0x10000U ||
          v.application_partition_offset > 0xff0000U ||
          v.application_partition_offset % 0x10000U != 0))) return false;
    for (std::size_t i = 0; i < v.version.size(); ++i) {
        if (i < v.version_length ? !version_character(v.version[i]) : v.version[i] != 0)
            return false;
    }
    return true;
}
namespace {
bool value_valid(const Firmware& value) { return valid_firmware(value); }

template <class T> bool sample_valid(const Sample<T>& sample, std::uint64_t at) {
    return sample.available ? sample.age_ms <= at && value_valid(sample.value) :
                              sample.age_ms == 0;
}
void put(std::uint8_t* out, std::size_t at, std::uint64_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i)
        out[at + i] = static_cast<std::uint8_t>(value >> (i * 8U));
}
std::uint64_t get(const std::uint8_t* data, std::size_t at, std::size_t width) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i)
        value |= static_cast<std::uint64_t>(data[at + i]) << (i * 8U);
    return value;
}
std::uint32_t u32(const std::uint8_t* data, std::size_t at) {
    return static_cast<std::uint32_t>(get(data, at, 4));
}
std::int16_t i16(const std::uint8_t* data, std::size_t at) {
    const auto raw = static_cast<std::int32_t>(get(data, at, 2));
    return static_cast<std::int16_t>(raw >= 32768 ? raw - 65536 : raw);
}

}  // namespace

bool valid(const Snapshot& s) {
    return s.sequence != 0 &&
           s.sampled_at_ms <= std::numeric_limits<std::uint64_t>::max() - s.age_ms &&
           sample_valid(s.battery, s.sampled_at_ms) &&
           sample_valid(s.power, s.sampled_at_ms) &&
           sample_valid(s.packet, s.sampled_at_ms) &&
           sample_valid(s.radio, s.sampled_at_ms) &&
           sample_valid(s.backoffs, s.sampled_at_ms) &&
           sample_valid(s.gnss, s.sampled_at_ms) &&
           sample_valid(s.reset, s.sampled_at_ms) &&
           sample_valid(s.heap, s.sampled_at_ms) &&
           sample_valid(s.main_stack, s.sampled_at_ms) &&
           sample_valid(s.ble_stack, s.sampled_at_ms) &&
           sample_valid(s.storage, s.sampled_at_ms) &&
           sample_valid(s.firmware, s.sampled_at_ms);
}

EncodeResult encode(const Snapshot& s, std::uint8_t* output, std::size_t capacity) {
    if (output == nullptr) return {Error::invalid_argument, 0};
    if (capacity < kRecordBytes) return {Error::output_too_small, 0};
    if (!valid(s)) return {Error::invalid_value, 0};
    std::array<std::uint8_t, kRecordBytes> bytes{};
    auto* out = bytes.data();
    out[0] = 'O'; out[1] = 'T'; out[2] = 'D'; out[3] = 'P'; out[4] = kVersion;
    put(out, 6, kRecordBytes, 2);
    put(out, 8, s.sequence, 4);
    put(out, 12, s.age_ms, 4);
    put(out, 16, s.sampled_at_ms, 8);
    std::uint32_t mask = 0;
    const auto group = [&](bool available, std::uint32_t age, unsigned bit, std::size_t at) {
        if (!available) return false;
        mask |= 1U << bit;
        put(out, at, age, 4);
        return true;
    };
    if (group(s.battery.available, s.battery.age_ms, 0, 32)) {
        put(out, 36, s.battery.value.millivolts, 2); out[38] = s.battery.value.percent;
    }
    if (group(s.power.available, s.power.age_ms, 1, 40))
        out[44] = static_cast<std::uint8_t>(s.power.value);
    if (group(s.packet.available, s.packet.age_ms, 2, 48)) {
        put(out, 52, static_cast<std::uint16_t>(s.packet.value.rssi_dbm), 2);
        put(out, 54, static_cast<std::uint16_t>(s.packet.value.snr_quarter_db), 2);
        put(out, 56, s.packet.value.frequency_hz, 4);
    }
    if (group(s.radio.available, s.radio.age_ms, 3, 60)) {
        const auto& c = s.radio.value;
        const std::array<std::uint32_t, 8> values{c.tx_attempts, c.tx_completed, c.rx_frames,
            c.rx_errors, c.rx_dropped, c.tx_refused, c.tx_busy_dropped, c.tx_prelaunch_dropped};
        for (std::size_t i = 0; i < values.size(); ++i) put(out, 64 + 4 * i, values[i], 4);
    }
    if (group(s.backoffs.available, s.backoffs.age_ms, 4, 96))
        put(out, 100, s.backoffs.value.scheduled, 4);
    if (group(s.gnss.available, s.gnss.age_ms, 5, 104)) {
        out[108] = static_cast<std::uint8_t>(s.gnss.value.state);
        out[109] = s.gnss.value.satellites;
        out[110] = s.gnss.value.satellites_known ? 1 : 0;
    }
    if (group(s.reset.available, s.reset.age_ms, 6, 112))
        out[116] = static_cast<std::uint8_t>(s.reset.value);
    if (group(s.heap.available, s.heap.age_ms, 7, 120)) {
        put(out, 124, s.heap.value.free_bytes, 4);
        put(out, 128, s.heap.value.minimum_free_bytes, 4);
    }
    if (group(s.main_stack.available, s.main_stack.age_ms, 8, 132))
        put(out, 136, s.main_stack.value.minimum_free_bytes, 4);
    if (group(s.ble_stack.available, s.ble_stack.age_ms, 9, 140))
        put(out, 144, s.ble_stack.value.minimum_free_bytes, 4);
    if (group(s.storage.available, s.storage.age_ms, 10, 148)) {
        put(out, 152, s.storage.value.used_entries, 4);
        put(out, 156, s.storage.value.free_entries, 4);
        put(out, 160, s.storage.value.total_entries, 4);
    }
    if (group(s.firmware.available, s.firmware.age_ms, 11, 164)) {
        put(out, 168, s.firmware.value.release_generation, 4);
        out[172] = s.firmware.value.version_length;
        put(out, 176, s.firmware.value.application_partition_offset, 4);
        for (std::size_t i = 0; i < s.firmware.value.version.size(); ++i)
            out[180 + i] = static_cast<std::uint8_t>(s.firmware.value.version[i]);
    }
    put(out, 24, mask, 4);
    // Transactional output: no caller bytes change until every field is admitted.
    std::copy(bytes.begin(), bytes.end(), output);
    return {Error::none, kRecordBytes};
}

DecodeResult decode(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr) return {Error::invalid_argument, {}};
    if (size != kRecordBytes) return {Error::malformed, {}};
    if (data[0] != 'O' || data[1] != 'T' || data[2] != 'D' || data[3] != 'P')
        return {Error::malformed, {}};
    if (data[4] != kVersion) return {Error::unsupported_version, {}};
    if (get(data, 6, 2) != kRecordBytes || (u32(data, 24) & ~kKnownFields) != 0)
        return {Error::malformed, {}};
    Snapshot s{};
    s.sequence = u32(data, 8);
    s.age_ms = u32(data, 12);
    s.sampled_at_ms = get(data, 16, 8);
    const auto mask = u32(data, 24);
    const auto group = [&](auto& sample, unsigned bit, std::size_t at) {
        sample.available = (mask & (1U << bit)) != 0;
        sample.age_ms = u32(data, at);
    };
    group(s.battery, 0, 32);
    group(s.power, 1, 40);
    group(s.packet, 2, 48);
    group(s.radio, 3, 60);
    group(s.backoffs, 4, 96);
    group(s.gnss, 5, 104);
    group(s.reset, 6, 112);
    group(s.heap, 7, 120);
    group(s.main_stack, 8, 132);
    group(s.ble_stack, 9, 140);
    group(s.storage, 10, 148);
    group(s.firmware, 11, 164);
    s.battery.value.millivolts = static_cast<std::uint16_t>(get(data, 36, 2));
    s.battery.value.percent = data[38];
    s.power.value = static_cast<PowerSource>(data[44]);
    s.packet.value = {i16(data, 52), i16(data, 54), u32(data, 56)};
    auto& c = s.radio.value;
    const std::array<std::uint32_t*, 8> counters{&c.tx_attempts, &c.tx_completed, &c.rx_frames,
        &c.rx_errors, &c.rx_dropped, &c.tx_refused, &c.tx_busy_dropped, &c.tx_prelaunch_dropped};
    for (std::size_t i = 0; i < counters.size(); ++i) *counters[i] = u32(data, 64 + 4 * i);
    s.backoffs.value.scheduled = u32(data, 100);
    s.gnss.value = {static_cast<GnssState>(data[108]), data[110] != 0, data[109]};
    s.reset.value = static_cast<ResetReason>(data[116]);
    s.heap.value = {u32(data, 124), u32(data, 128)};
    s.main_stack.value.minimum_free_bytes = u32(data, 136);
    s.ble_stack.value.minimum_free_bytes = u32(data, 144);
    s.storage.value = {u32(data, 152), u32(data, 156), u32(data, 160)};
    s.firmware.value.release_generation = u32(data, 168);
    s.firmware.value.version_length = data[172];
    s.firmware.value.application_partition_offset = u32(data, 176);
    for (std::size_t i = 0; i < s.firmware.value.version.size(); ++i)
        s.firmware.value.version[i] = static_cast<char>(data[180 + i]);
    std::array<std::uint8_t, kRecordBytes> canonical{};
    const auto encoded = encode(s, canonical.data(), canonical.size());
    if (encoded.error != Error::none) return {encoded.error, {}};
    // Reject all reserved bytes, noncanonical booleans, absent-field data and
    // covert trailing text. No partial snapshot escapes on a decode failure.
    if (std::memcmp(data, canonical.data(), kRecordBytes) != 0)
        return {Error::malformed, {}};
    return {Error::none, s};
}

}  // namespace opentrail::device_parameters