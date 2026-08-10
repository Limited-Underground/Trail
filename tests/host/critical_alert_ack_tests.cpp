#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "opentrail/critical_alert_ack.hpp"

namespace {
using namespace opentrail::integration;
int failures = 0;
void expect(bool condition, const char* text, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << text << '\n';
        ++failures;
    }
}
#define EXPECT(value) expect((value), #value, __LINE__)

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> result;
    std::stringstream stream{line};
    std::string field;
    while (std::getline(stream, field, ',')) {
        result.push_back(field);
    }
    return result;
}

std::array<std::uint8_t, kCriticalAlertAckFrameBytes> hex_bytes(
    const std::string& hex) {
    std::array<std::uint8_t, kCriticalAlertAckFrameBytes> result{};
    EXPECT(hex.size() == result.size() * 2);
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::uint8_t>(
            std::stoul(hex.substr(index * 2, 2), nullptr, 16));
    }
    return result;
}

CriticalAlertAck row_ack(const std::vector<std::string>& fields) {
    CriticalAlertAck value{};
    value.disposition = static_cast<AlertAckDisposition>(std::stoul(fields[1]));
    value.reason = static_cast<AlertAckReason>(std::stoul(fields[2]));
    value.state = static_cast<AlertState>(std::stoul(fields[3]));
    value.consumer_id = std::stoull(fields[4]);
    value.producer_id = std::stoull(fields[5]);
    value.event_id = std::stoull(fields[6]);
    value.condition_id = std::stoull(fields[7]);
    value.consumer_boot_session_id =
        static_cast<std::uint32_t>(std::stoul(fields[8]));
    value.ack_sequence =
        static_cast<std::uint32_t>(std::stoul(fields[9]));
    value.observed_alert_age_ms =
        static_cast<std::uint32_t>(std::stoul(fields[10]));
    return value;
}

CriticalAlertAck accepted() {
    return {AlertAckDisposition::accepted, AlertAckReason::none,
            AlertState::asserted, 1, 2, 3, 4, 5, 6, 7};
}

void test_independent_normative_vectors() {
    std::ifstream input{"tests/fixtures/critical_alert_ack_v0_vectors.csv"};
    EXPECT(input.good());
    std::string line;
    EXPECT(static_cast<bool>(std::getline(input, line)));
    std::size_t rows = 0;
    while (std::getline(input, line)) {
        const auto fields = split(line);
        EXPECT(fields.size() == 12);
        if (fields.size() != 12) {
            continue;
        }
        const auto value = row_ack(fields);
        const auto expected = hex_bytes(fields[11]);
        std::array<std::uint8_t, kCriticalAlertAckFrameBytes> encoded{};
        EXPECT(encode_critical_alert_ack(value, encoded).encoded());
        EXPECT(encoded == expected);
        const auto decoded = decode_critical_alert_ack(
            expected.data(), expected.size());
        EXPECT(decoded.decoded());
        EXPECT(decoded.acknowledgement.consumer_id == value.consumer_id);
        EXPECT(decoded.acknowledgement.event_id == value.event_id);
        EXPECT(decoded.acknowledgement.state == value.state);
        ++rows;
    }
    EXPECT(rows == 3);
}

void test_semantic_validation() {
    auto value = accepted();
    EXPECT(validate_critical_alert_ack(value) == AlertAckCodecError::none);
    value.reason = AlertAckReason::duplicate;
    EXPECT(validate_critical_alert_ack(value) ==
           AlertAckCodecError::inconsistent_disposition);
    value.disposition = AlertAckDisposition::rejected;
    EXPECT(validate_critical_alert_ack(value) == AlertAckCodecError::none);
    value.consumer_boot_session_id = 0;
    EXPECT(validate_critical_alert_ack(value) ==
           AlertAckCodecError::invalid_identity);
}

void test_malformed_and_integrity_rejection() {
    std::array<std::uint8_t, kCriticalAlertAckFrameBytes> bytes{};
    EXPECT(encode_critical_alert_ack(accepted(), bytes).encoded());
    auto changed = bytes;
    changed[4] = 1;
    EXPECT(decode_critical_alert_ack(changed.data(), changed.size()).error ==
           AlertAckCodecError::unsupported_version);
    changed = bytes;
    changed[56] = 1;
    EXPECT(decode_critical_alert_ack(changed.data(), changed.size()).error ==
           AlertAckCodecError::noncanonical);
    changed = bytes;
    changed[30] ^= 1;
    EXPECT(decode_critical_alert_ack(changed.data(), changed.size()).error ==
           AlertAckCodecError::integrity_failure);
}

void test_invalid_encode_preserves_output() {
    auto value = accepted();
    value.event_id = 0;
    std::array<std::uint8_t, kCriticalAlertAckFrameBytes> output{};
    output.fill(0xCCU);
    EXPECT(!encode_critical_alert_ack(value, output).encoded());
    EXPECT(output.front() == 0xCCU && output.back() == 0xCCU);
}
}  // namespace

int main() {
    test_independent_normative_vectors();
    test_semantic_validation();
    test_malformed_and_integrity_rejection();
    test_invalid_encode_preserves_output();
    if (failures != 0) {
        std::cerr << failures << " critical alert ACK assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 4 critical alert ACK scenario groups\n";
    return EXIT_SUCCESS;
}
