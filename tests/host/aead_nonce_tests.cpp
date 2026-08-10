#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/aead_nonce.hpp"

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

opentrail::persistence::CounterDomainId domain(std::uint8_t seed = 1) {
    opentrail::persistence::CounterDomainId value{};
    for (std::size_t index = 0; index < value.size(); ++index) {
        value[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

AeadNonceRequest request(std::uint64_t counter = 1) {
    const auto id = domain();
    return {id, id, {0x10, 0x20, 0x30, 0x40}, counter};
}

void test_exact_canonical_bytes() {
    const auto result = compose_aead_nonce(request(0x0102030405060708ULL));
    EXPECT(result.composed());
    EXPECT((result.bytes == AeadNonceBytes{
        0x10, 0x20, 0x30, 0x40,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}));
}

void test_counter_changes_only_counter_suffix() {
    const auto first = compose_aead_nonce(request(1));
    const auto second = compose_aead_nonce(request(2));
    EXPECT(first.composed() && second.composed());
    EXPECT(std::equal(first.bytes.begin(), first.bytes.begin() + 4,
                      second.bytes.begin()));
    EXPECT(first.bytes != second.bytes);
    EXPECT(first.bytes[11] == 1 && second.bytes[11] == 2);
}

void test_prefix_changes_nonce_domain() {
    auto second_request = request(42);
    second_request.key_domain_prefix = {0x10, 0x20, 0x30, 0x41};
    const auto first = compose_aead_nonce(request(42));
    const auto second = compose_aead_nonce(second_request);
    EXPECT(first.composed() && second.composed());
    EXPECT(first.bytes != second.bytes);
    EXPECT(std::equal(first.bytes.begin() + 4, first.bytes.end(),
                      second.bytes.begin() + 4));
}

void test_zero_prefix_is_valid_key_material_output() {
    auto zero_prefix = request(1);
    zero_prefix.key_domain_prefix = {};
    const auto result = compose_aead_nonce(zero_prefix);
    EXPECT(result.composed());
    EXPECT(result.bytes[0] == 0 && result.bytes[3] == 0);
    EXPECT(result.bytes[11] == 1);
}

void test_domain_mismatch_fails_without_nonce() {
    auto mismatch = request();
    mismatch.key_domain_id = domain(2);
    const auto result = compose_aead_nonce(mismatch);
    EXPECT(result.error == AeadNonceError::domain_mismatch);
    EXPECT(result.bytes == AeadNonceBytes{});
}

void test_zero_domains_fail_closed() {
    auto invalid_lease = request();
    invalid_lease.lease_domain_id = {};
    auto invalid_key = request();
    invalid_key.key_domain_id = {};
    const auto lease_result = compose_aead_nonce(invalid_lease);
    const auto key_result = compose_aead_nonce(invalid_key);
    EXPECT(lease_result.error == AeadNonceError::invalid_domain);
    EXPECT(key_result.error == AeadNonceError::invalid_domain);
    EXPECT(lease_result.bytes == AeadNonceBytes{});
    EXPECT(key_result.bytes == AeadNonceBytes{});
}

void test_counter_boundaries() {
    const auto zero = compose_aead_nonce(request(0));
    const auto maximum = compose_aead_nonce(
        request(std::numeric_limits<std::uint64_t>::max()));
    EXPECT(zero.error == AeadNonceError::invalid_counter);
    EXPECT(zero.bytes == AeadNonceBytes{});
    EXPECT(maximum.composed());
    for (std::size_t index = 4; index < maximum.bytes.size(); ++index) {
        EXPECT(maximum.bytes[index] == 0xFF);
    }
}

}  // namespace

int main() {
    test_exact_canonical_bytes();
    test_counter_changes_only_counter_suffix();
    test_prefix_changes_nonce_domain();
    test_zero_prefix_is_valid_key_material_output();
    test_domain_mismatch_fails_without_nonce();
    test_zero_domains_fail_closed();
    test_counter_boundaries();
    if (failures != 0) {
        std::cerr << failures << " AEAD nonce assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 7 AEAD nonce composition scenario groups\n";
    return EXIT_SUCCESS;
}
