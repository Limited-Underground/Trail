#include <algorithm>
#include <cstdlib>
#include <iostream>

#include "opentrail/traffic_key_context.hpp"

namespace {

using namespace opentrail::security;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

opentrail::identity::IdentityFingerprint fingerprint(std::uint8_t seed = 1) {
    opentrail::identity::IdentityFingerprint value{};
    for (std::size_t index = 0; index < value.size(); ++index) {
        value[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

TrafficKeyContextRequest request() {
    return {0x0102030405060708ULL, 0x11223344U, fingerprint(),
            TrafficKeyPurpose::group_aead_key};
}

void test_exact_canonical_vector() {
    const auto result = encode_traffic_key_context(request());
    EXPECT(result.encoded());
    EXPECT(result.bytes[0] == 'O' && result.bytes[1] == 'T');
    EXPECT(result.bytes[2] == 'K' && result.bytes[3] == 'D');
    EXPECT(result.bytes[4] == 1 && result.bytes[5] == 1);
    EXPECT(result.bytes[6] == 0 && result.bytes[7] == 52);
    for (std::size_t index = 0; index < 8; ++index) {
        EXPECT(result.bytes[8 + index] == index + 1);
    }
    EXPECT(result.bytes[16] == 0x11 && result.bytes[19] == 0x44);
    EXPECT(std::equal(result.bytes.begin() + 20, result.bytes.end(),
                      fingerprint().begin()));
}

void test_purposes_are_separate_contexts() {
    auto nonce = request();
    nonce.purpose = TrafficKeyPurpose::nonce_prefix;
    auto domain = request();
    domain.purpose = TrafficKeyPurpose::counter_domain_id;
    const auto key_result = encode_traffic_key_context(request());
    const auto nonce_result = encode_traffic_key_context(nonce);
    const auto domain_result = encode_traffic_key_context(domain);
    EXPECT(key_result.encoded() && nonce_result.encoded() && domain_result.encoded());
    EXPECT(key_result.bytes[5] == 1 && nonce_result.bytes[5] == 2);
    EXPECT(domain_result.bytes[5] == 3);
    EXPECT(key_result.bytes != nonce_result.bytes);
    EXPECT(nonce_result.bytes != domain_result.bytes);
}

void test_group_change_changes_context() {
    auto changed = request();
    ++changed.group_id;
    EXPECT(encode_traffic_key_context(request()).bytes !=
           encode_traffic_key_context(changed).bytes);
}

void test_epoch_change_changes_context() {
    auto changed = request();
    ++changed.group_epoch;
    EXPECT(encode_traffic_key_context(request()).bytes !=
           encode_traffic_key_context(changed).bytes);
}

void test_sender_change_changes_context() {
    auto changed = request();
    changed.sender_fingerprint = fingerprint(2);
    EXPECT(encode_traffic_key_context(request()).bytes !=
           encode_traffic_key_context(changed).bytes);
}

void test_zero_group_and_epoch_fail_closed() {
    auto zero_group = request();
    zero_group.group_id = 0;
    auto zero_epoch = request();
    zero_epoch.group_epoch = 0;
    const auto group_result = encode_traffic_key_context(zero_group);
    const auto epoch_result = encode_traffic_key_context(zero_epoch);
    EXPECT(group_result.error == TrafficKeyContextError::invalid_group);
    EXPECT(epoch_result.error == TrafficKeyContextError::invalid_epoch);
    EXPECT(group_result.bytes == decltype(group_result.bytes){});
    EXPECT(epoch_result.bytes == decltype(epoch_result.bytes){});
}

void test_zero_sender_fails_closed() {
    auto zero_sender = request();
    zero_sender.sender_fingerprint = {};
    const auto result = encode_traffic_key_context(zero_sender);
    EXPECT(result.error == TrafficKeyContextError::invalid_sender);
    EXPECT(result.bytes == decltype(result.bytes){});
}

void test_unknown_purpose_fails_closed() {
    auto unknown = request();
    unknown.purpose = static_cast<TrafficKeyPurpose>(255);
    const auto result = encode_traffic_key_context(unknown);
    EXPECT(result.error == TrafficKeyContextError::invalid_purpose);
    EXPECT(result.bytes == decltype(result.bytes){});
}

}  // namespace

int main() {
    test_exact_canonical_vector();
    test_purposes_are_separate_contexts();
    test_group_change_changes_context();
    test_epoch_change_changes_context();
    test_sender_change_changes_context();
    test_zero_group_and_epoch_fail_closed();
    test_zero_sender_fails_closed();
    test_unknown_purpose_fails_closed();
    if (failures != 0) {
        std::cerr << failures << " traffic-key context assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 traffic-key context scenario groups\n";
    return EXIT_SUCCESS;
}
