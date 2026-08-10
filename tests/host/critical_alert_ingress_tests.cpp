#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "opentrail/critical_alert.hpp"

namespace {

using namespace opentrail::integration;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

std::vector<std::string> split(const std::string& text, char delimiter) {
    std::vector<std::string> fields;
    std::stringstream stream(text);
    std::string field;
    while (std::getline(stream, field, delimiter)) {
        fields.push_back(field);
    }
    return fields;
}

std::vector<std::uint8_t> from_hex(const std::string& text) {
    std::vector<std::uint8_t> bytes;
    if (text.size() % 2 != 0) {
        return bytes;
    }
    bytes.reserve(text.size() / 2);
    for (std::size_t index = 0; index < text.size(); index += 2) {
        bytes.push_back(static_cast<std::uint8_t>(
            std::stoul(text.substr(index, 2), nullptr, 16)));
    }
    return bytes;
}

std::string to_hex(
    const std::array<std::uint8_t, kCriticalAlertFrameBytes>& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        text.push_back(digits[(byte >> 4U) & 0x0FU]);
        text.push_back(digits[byte & 0x0FU]);
    }
    return text;
}

void write_u32_le(std::uint8_t* destination, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        destination[index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

void refresh_crc(
    std::array<std::uint8_t, kCriticalAlertFrameBytes>& frame) {
    write_u32_le(frame.data() + 60, alert_crc32(frame.data(), 60));
}

CriticalAlert base_alert() {
    return {
        CriticalAlertType::engine_over_temperature,
        AlertSeverity::critical,
        AlertState::asserted,
        AlertQuality::valid,
        AlertUnit::celsius,
        0x0102030405060708ULL,
        0x1112131415161718ULL,
        0x2122232425262728ULL,
        0x3132333435363738ULL,
        1786243200U,
        1000U,
        118333,
        0x00010001U,
        true,
        true,
    };
}

AlertIngressContext trusted_context(
    const CriticalAlert& alert,
    std::uint64_t monotonic_ms = 1000,
    std::uint32_t utc_s = 1786243201U) {
    return {
        true,
        true,
        alert.producer_id,
        monotonic_ms,
        true,
        utc_s,
    };
}

std::array<std::uint8_t, kCriticalAlertFrameBytes> encoded(
    const CriticalAlert& alert) {
    std::array<std::uint8_t, kCriticalAlertFrameBytes> frame{};
    EXPECT(encode_critical_alert(alert, frame).encoded());
    return frame;
}

void test_crc_and_normative_fixtures() {
    const std::array<std::uint8_t, 9> check{
        '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT(alert_crc32(check.data(), check.size()) == 0xCBF43926U);

    std::ifstream input("tests/fixtures/critical_alert_v0_vectors.csv");
    EXPECT(input.good());
    std::string line;
    EXPECT(static_cast<bool>(std::getline(input, line)));
    std::size_t rows = 0;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = split(line, ',');
        EXPECT(fields.size() == 17);
        if (fields.size() != 17) {
            continue;
        }

        CriticalAlert alert{};
        alert.type = static_cast<CriticalAlertType>(std::stoul(fields[1]));
        alert.severity =
            static_cast<AlertSeverity>(std::stoul(fields[2]));
        alert.state = static_cast<AlertState>(std::stoul(fields[3]));
        alert.quality = static_cast<AlertQuality>(std::stoul(fields[4]));
        alert.unit = static_cast<AlertUnit>(std::stoul(fields[5]));
        alert.producer_id = std::stoull(fields[6]);
        alert.vehicle_id = std::stoull(fields[7]);
        alert.event_id = std::stoull(fields[8]);
        alert.condition_id = std::stoull(fields[9]);
        alert.utc_present = std::stoul(fields[10]) != 0;
        alert.event_time_utc_s =
            static_cast<std::uint32_t>(std::stoul(fields[11]));
        alert.age_ms = static_cast<std::uint32_t>(std::stoul(fields[12]));
        alert.value_present = std::stoul(fields[13]) != 0;
        alert.value_milli = static_cast<std::int32_t>(
            std::stol(fields[14]));
        alert.diagnostic_code =
            static_cast<std::uint32_t>(std::stoul(fields[15]));

        std::array<std::uint8_t, kCriticalAlertFrameBytes> frame{};
        const auto result = encode_critical_alert(alert, frame);
        EXPECT(result.encoded());
        EXPECT(result.encoded_bytes == kCriticalAlertFrameBytes);
        EXPECT(to_hex(frame) == fields[16]);

        const auto expected = from_hex(fields[16]);
        EXPECT(expected.size() == frame.size());
        EXPECT(std::equal(frame.begin(), frame.end(), expected.begin()));
        const auto decoded = decode_critical_alert(frame.data(), frame.size());
        EXPECT(decoded.decoded());
        EXPECT(decoded.alert.event_id == alert.event_id);
        EXPECT(decoded.alert.condition_id == alert.condition_id);
        EXPECT(decoded.alert.value_milli == alert.value_milli);
        ++rows;
    }
    EXPECT(rows == 3);
}

void test_framing_and_integrity_rejection() {
    const auto alert = base_alert();
    auto frame = encoded(alert);

    EXPECT(decode_critical_alert(nullptr, frame.size()).error ==
           AlertCodecError::invalid_argument);
    EXPECT(decode_critical_alert(frame.data(), frame.size() - 1).error ==
           AlertCodecError::malformed);

    auto mutated = frame;
    mutated[0] = 'X';
    EXPECT(decode_critical_alert(mutated.data(), mutated.size()).error ==
           AlertCodecError::malformed);

    mutated = frame;
    mutated[4] = 1;
    EXPECT(decode_critical_alert(mutated.data(), mutated.size()).error ==
           AlertCodecError::unsupported_version);

    mutated = frame;
    mutated[6] |= 0x80U;
    EXPECT(decode_critical_alert(mutated.data(), mutated.size()).error ==
           AlertCodecError::reserved_flags_set);

    mutated = frame;
    mutated[52] ^= 0x01U;
    EXPECT(decode_critical_alert(mutated.data(), mutated.size()).error ==
           AlertCodecError::integrity_failure);
}

void test_semantic_validation() {
    auto alert = base_alert();
    alert.producer_id = 0;
    EXPECT(validate_critical_alert(alert) ==
           AlertCodecError::invalid_identity);

    alert = base_alert();
    alert.utc_present = false;
    EXPECT(validate_critical_alert(alert) ==
           AlertCodecError::inconsistent_time);

    alert = base_alert();
    alert.value_present = false;
    EXPECT(validate_critical_alert(alert) ==
           AlertCodecError::inconsistent_value);

    alert = base_alert();
    alert.value_milli = 250001;
    EXPECT(validate_critical_alert(alert) ==
           AlertCodecError::value_out_of_range);

    alert = base_alert();
    alert.unit = AlertUnit::volt;
    alert.value_milli = 12000;
    EXPECT(validate_critical_alert(alert) ==
           AlertCodecError::invalid_type_unit);

    alert = base_alert();
    alert.severity = AlertSeverity::warning;
    EXPECT(validate_critical_alert(alert) ==
           AlertCodecError::invalid_severity);

    alert = base_alert();
    alert.quality = AlertQuality::unavailable;
    EXPECT(validate_critical_alert(alert) ==
           AlertCodecError::inconsistent_quality);

    alert = base_alert();
    alert.type = CriticalAlertType::rollover_detected;
    alert.unit = AlertUnit::boolean;
    alert.value_milli = 1000;
    EXPECT(validate_critical_alert(alert) ==
           AlertCodecError::invalid_severity);
}

void test_trust_boundary() {
    const auto alert = base_alert();
    const auto frame = encoded(alert);
    CriticalAlertIngress ingress;
    auto context = trusted_context(alert);

    context.authenticated = false;
    EXPECT(ingress.process(frame.data(), frame.size(), context).error ==
           AlertIngressError::unauthenticated);

    context = trusted_context(alert);
    context.authorized_to_publish = false;
    EXPECT(ingress.process(frame.data(), frame.size(), context).error ==
           AlertIngressError::unauthorized);

    context = trusted_context(alert);
    ++context.authenticated_producer_id;
    EXPECT(ingress.process(frame.data(), frame.size(), context).error ==
           AlertIngressError::producer_mismatch);
}

void test_freshness_boundaries() {
    auto alert = base_alert();
    alert.age_ms = kMaximumAlertAgeMs;
    auto frame = encoded(alert);
    CriticalAlertIngress ingress;
    auto context = trusted_context(
        alert,
        1000,
        alert.event_time_utc_s + kMaximumUtcAgeSeconds);
    EXPECT(ingress.process(frame.data(), frame.size(), context).accepted());

    alert = base_alert();
    alert.event_id += 1;
    alert.age_ms = kMaximumAlertAgeMs + 1;
    frame = encoded(alert);
    CriticalAlertIngress stale_age;
    EXPECT(stale_age.process(
               frame.data(),
               frame.size(),
               trusted_context(alert)).error == AlertIngressError::stale);

    alert = base_alert();
    alert.event_id += 2;
    frame = encoded(alert);
    context = trusted_context(
        alert,
        1000,
        alert.event_time_utc_s + kMaximumUtcAgeSeconds + 1);
    CriticalAlertIngress stale_utc;
    EXPECT(stale_utc.process(frame.data(), frame.size(), context).error ==
           AlertIngressError::stale);

    context = trusted_context(
        alert,
        1000,
        alert.event_time_utc_s - kMaximumFutureSkewSeconds);
    CriticalAlertIngress future_boundary;
    EXPECT(future_boundary.process(
               frame.data(), frame.size(), context).accepted());

    context = trusted_context(
        alert,
        1000,
        alert.event_time_utc_s - kMaximumFutureSkewSeconds - 1);
    CriticalAlertIngress future_reject;
    EXPECT(future_reject.process(frame.data(), frame.size(), context).error ==
           AlertIngressError::future_timestamp);

    alert = base_alert();
    alert.utc_present = false;
    alert.event_time_utc_s = 0;
    alert.event_id += 3;
    frame = encoded(alert);
    context = trusted_context(alert);
    context.utc_available = false;
    context.receive_utc_s = 0;
    CriticalAlertIngress no_utc;
    EXPECT(no_utc.process(frame.data(), frame.size(), context).accepted());
}

void test_duplicate_and_conflict() {
    auto alert = base_alert();
    auto frame = encoded(alert);
    auto context = trusted_context(alert);
    CriticalAlertIngress ingress;
    EXPECT(ingress.process(frame.data(), frame.size(), context).accepted());

    context.receive_monotonic_ms += 1;
    const auto duplicate =
        ingress.process(frame.data(), frame.size(), context);
    EXPECT(duplicate.disposition == AlertIngressDisposition::duplicate);
    EXPECT(duplicate.error == AlertIngressError::none);

    auto conflict = frame;
    conflict[56] ^= 0x01U;
    refresh_crc(conflict);
    context.receive_monotonic_ms += 1;
    EXPECT(ingress.process(
               conflict.data(), conflict.size(), context).error ==
           AlertIngressError::duplicate_conflict);

    context.receive_monotonic_ms += kDuplicateRetentionMs + 1;
    EXPECT(ingress.process(frame.data(), frame.size(), context).accepted());
}

void test_rate_limit_and_emergency_reserve() {
    CriticalAlertIngress ingress;
    auto alert = base_alert();
    auto context = trusted_context(alert, 1000);
    for (std::uint64_t index = 0; index < kGeneralRateAllowance; ++index) {
        alert.event_id = 100 + index;
        const auto frame = encoded(alert);
        context.receive_monotonic_ms = 1000 + index;
        EXPECT(ingress.process(
                   frame.data(), frame.size(), context).accepted());
    }

    alert.event_id = 200;
    auto frame = encoded(alert);
    context.receive_monotonic_ms = 1005;
    EXPECT(ingress.process(frame.data(), frame.size(), context).error ==
           AlertIngressError::rate_limited);

    alert.type = CriticalAlertType::rollover_detected;
    alert.severity = AlertSeverity::emergency;
    alert.value_present = false;
    alert.unit = AlertUnit::none;
    alert.value_milli = 0;
    for (std::uint64_t index = 0; index < kEmergencyRateReserve; ++index) {
        alert.event_id = 300 + index;
        frame = encoded(alert);
        context.receive_monotonic_ms = 1006 + index;
        EXPECT(ingress.process(
                   frame.data(), frame.size(), context).accepted());
    }

    alert.event_id = 400;
    frame = encoded(alert);
    context.receive_monotonic_ms = 1009;
    EXPECT(ingress.process(frame.data(), frame.size(), context).error ==
           AlertIngressError::rate_limited);

    alert.type = CriticalAlertType::engine_over_temperature;
    alert.severity = AlertSeverity::critical;
    alert.value_present = true;
    alert.unit = AlertUnit::celsius;
    alert.value_milli = 118333;
    alert.event_id = 500;
    frame = encoded(alert);
    context.receive_monotonic_ms = 11000;
    EXPECT(ingress.process(frame.data(), frame.size(), context).accepted());
}

void test_monotonic_rollback_and_cleared_quality() {
    auto alert = base_alert();
    auto frame = encoded(alert);
    auto context = trusted_context(alert, 5000);
    CriticalAlertIngress ingress;
    EXPECT(ingress.process(frame.data(), frame.size(), context).accepted());

    alert.event_id += 1;
    frame = encoded(alert);
    context.receive_monotonic_ms = 4999;
    EXPECT(ingress.process(frame.data(), frame.size(), context).error ==
           AlertIngressError::monotonic_time_rollback);

    alert = base_alert();
    alert.state = AlertState::cleared;
    alert.quality = AlertQuality::unavailable;
    alert.value_present = false;
    alert.unit = AlertUnit::none;
    alert.value_milli = 0;
    alert.event_id += 2;
    frame = encoded(alert);
    CriticalAlertIngress cleared_ingress;
    EXPECT(cleared_ingress.process(
               frame.data(),
               frame.size(),
               trusted_context(alert)).accepted());
}

void test_authorized_producer_capacity_fails_closed() {
    CriticalAlertIngress ingress;
    auto alert = base_alert();
    for (std::uint64_t index = 0;
         index < kRateProducerCapacity;
         ++index) {
        alert.producer_id = 1000 + index;
        alert.event_id = 2000 + index;
        const auto frame = encoded(alert);
        const auto context = trusted_context(alert, 1000 + index);
        EXPECT(ingress.process(
                   frame.data(), frame.size(), context).accepted());
    }

    alert.producer_id = 9999;
    alert.event_id = 9999;
    const auto frame = encoded(alert);
    const auto context = trusted_context(alert, 2000);
    EXPECT(ingress.process(frame.data(), frame.size(), context).error ==
           AlertIngressError::producer_capacity_exhausted);
}

}  // namespace

int main() {
    test_crc_and_normative_fixtures();
    test_framing_and_integrity_rejection();
    test_semantic_validation();
    test_trust_boundary();
    test_freshness_boundaries();
    test_duplicate_and_conflict();
    test_rate_limit_and_emergency_reserve();
    test_monotonic_rollback_and_cleared_quality();
    test_authorized_producer_capacity_fails_closed();

    if (failures != 0) {
        std::cerr << failures << " critical alert assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 9 critical alert ingress scenario groups\n";
    return EXIT_SUCCESS;
}
