#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_secure_random.hpp"

namespace {

using opentrail::security::EntropyState;
using opentrail::security::RandomFillError;
using opentrail::security::kMaximumSecureRandomRequestBytes;
using opentrail::security::test_support::FakeSecureRandomSource;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

template <std::size_t Size>
bool all_equal(
    const std::array<std::uint8_t, Size>& bytes,
    std::uint8_t expected) {
    return std::all_of(
        bytes.begin(),
        bytes.end(),
        [expected](std::uint8_t value) { return value == expected; });
}

void test_default_not_ready_fails_closed() {
    FakeSecureRandomSource source;
    std::array<std::uint8_t, 8> output{};
    output.fill(0xA5);

    const auto result = source.fill(output.data(), output.size());
    EXPECT(source.state() == EntropyState::not_ready);
    EXPECT(result.error == RandomFillError::entropy_not_ready);
    EXPECT(result.bytes_written == 0);
    EXPECT(all_equal(output, 0xA5));
    EXPECT(source.consumed_byte_count() == 0);
}

void test_ready_bytes_are_consumed_once_in_order() {
    FakeSecureRandomSource source;
    const std::array<std::uint8_t, 8> script{
        0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22, 0x23};
    EXPECT(source.load_bytes(script.data(), script.size()));
    source.set_state(EntropyState::ready);

    std::array<std::uint8_t, 4> first{};
    std::array<std::uint8_t, 4> second{};
    const auto first_result = source.fill(first.data(), first.size());
    const auto second_result = source.fill(second.data(), second.size());

    EXPECT(first_result.ok());
    EXPECT(second_result.ok());
    const std::array<std::uint8_t, 4> expected_first{0x10, 0x11, 0x12, 0x13};
    const std::array<std::uint8_t, 4> expected_second{0x20, 0x21, 0x22, 0x23};
    EXPECT(first == expected_first);
    EXPECT(second == expected_second);
    EXPECT(source.successful_fill_count() == 2);
    EXPECT(source.consumed_byte_count() == script.size());
}

void test_invalid_requests_do_not_consume_script() {
    FakeSecureRandomSource source;
    const std::array<std::uint8_t, 4> script{1, 2, 3, 4};
    EXPECT(source.load_bytes(script.data(), script.size()));
    source.set_state(EntropyState::ready);
    std::array<std::uint8_t, kMaximumSecureRandomRequestBytes + 1> output{};
    output.fill(0x6C);

    EXPECT(source.fill(nullptr, 4).error == RandomFillError::invalid_argument);
    EXPECT(source.fill(output.data(), 0).error ==
           RandomFillError::invalid_argument);
    EXPECT(source.fill(output.data(), output.size()).error ==
           RandomFillError::request_too_large);
    EXPECT(all_equal(output, 0x6C));
    EXPECT(source.consumed_byte_count() == 0);
    EXPECT(source.successful_fill_count() == 0);
}

void test_script_exhaustion_is_atomic() {
    FakeSecureRandomSource source;
    const std::array<std::uint8_t, 3> script{1, 2, 3};
    EXPECT(source.load_bytes(script.data(), script.size()));
    source.set_state(EntropyState::ready);
    std::array<std::uint8_t, 4> output{};
    output.fill(0x77);

    const auto result = source.fill(output.data(), output.size());
    EXPECT(result.error == RandomFillError::entropy_failed);
    EXPECT(result.bytes_written == 0);
    EXPECT(all_equal(output, 0x77));
    EXPECT(source.consumed_byte_count() == 0);
}

void test_injected_failure_does_not_consume_bytes() {
    FakeSecureRandomSource source;
    const std::array<std::uint8_t, 4> script{9, 8, 7, 6};
    EXPECT(source.load_bytes(script.data(), script.size()));
    source.set_state(EntropyState::ready);
    source.fail_next_fill();
    std::array<std::uint8_t, 4> output{};
    output.fill(0x33);

    const auto failed = source.fill(output.data(), output.size());
    EXPECT(failed.error == RandomFillError::entropy_failed);
    EXPECT(all_equal(output, 0x33));
    EXPECT(source.consumed_byte_count() == 0);

    const auto recovered = source.fill(output.data(), output.size());
    EXPECT(recovered.ok());
    EXPECT(output == script);
    EXPECT(source.consumed_byte_count() == script.size());
}

void test_readiness_transition_does_not_skip_bytes() {
    FakeSecureRandomSource source;
    const std::array<std::uint8_t, 8> script{1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT(source.load_bytes(script.data(), script.size()));
    source.set_state(EntropyState::ready);
    std::array<std::uint8_t, 4> output{};

    EXPECT(source.fill(output.data(), output.size()).ok());
    const std::array<std::uint8_t, 4> expected_first{1, 2, 3, 4};
    EXPECT(output == expected_first);
    source.set_state(EntropyState::not_ready);
    output.fill(0x44);
    EXPECT(source.fill(output.data(), output.size()).error ==
           RandomFillError::entropy_not_ready);
    EXPECT(all_equal(output, 0x44));
    EXPECT(source.consumed_byte_count() == 4);

    source.set_state(EntropyState::ready);
    EXPECT(source.fill(output.data(), output.size()).ok());
    const std::array<std::uint8_t, 4> expected_second{5, 6, 7, 8};
    EXPECT(output == expected_second);
}

void test_failed_state_is_typed_and_sticky() {
    FakeSecureRandomSource source;
    const std::array<std::uint8_t, 4> script{1, 2, 3, 4};
    EXPECT(source.load_bytes(script.data(), script.size()));
    source.set_state(EntropyState::failed);
    std::array<std::uint8_t, 4> output{};
    output.fill(0xE1);

    EXPECT(source.fill(output.data(), output.size()).error ==
           RandomFillError::entropy_failed);
    EXPECT(source.fill(output.data(), output.size()).error ==
           RandomFillError::entropy_failed);
    EXPECT(all_equal(output, 0xE1));
    EXPECT(source.consumed_byte_count() == 0);
}

void test_maximum_bounded_request_succeeds() {
    FakeSecureRandomSource source;
    std::array<std::uint8_t, kMaximumSecureRandomRequestBytes> script{};
    for (std::size_t index = 0; index < script.size(); ++index) {
        script[index] = static_cast<std::uint8_t>(index);
    }
    EXPECT(source.load_bytes(script.data(), script.size()));
    source.set_state(EntropyState::ready);
    std::array<std::uint8_t, kMaximumSecureRandomRequestBytes> output{};

    const auto result = source.fill(output.data(), output.size());
    EXPECT(result.ok());
    EXPECT(result.bytes_written == kMaximumSecureRandomRequestBytes);
    EXPECT(output == script);
    EXPECT(source.fill_attempt_count() == 1);
}

}  // namespace

int main() {
    test_default_not_ready_fails_closed();
    test_ready_bytes_are_consumed_once_in_order();
    test_invalid_requests_do_not_consume_script();
    test_script_exhaustion_is_atomic();
    test_injected_failure_does_not_consume_bytes();
    test_readiness_transition_does_not_skip_bytes();
    test_failed_state_is_typed_and_sticky();
    test_maximum_bounded_request_succeeds();

    if (failures != 0) {
        std::cerr << failures << " secure-randomness assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 secure-randomness boundary scenarios\n";
    return EXIT_SUCCESS;
}
