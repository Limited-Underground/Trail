#include <array>
#include <cstdlib>
#include <iostream>

#include "opentrail/protected_reassembly.hpp"

namespace {

using namespace opentrail::protocol;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

ProtectedReassembler reassembler(std::uint32_t timeout = 100) {
    return ProtectedReassembler({0xAABBCCDDU, 7, timeout});
}

VerifiedProtectedFragment fragment(
    std::uint32_t message_id,
    std::uint8_t index,
    std::uint8_t count,
    const std::uint8_t* data,
    std::size_t size,
    std::uint64_t sender = 11) {
    return {0xAABBCCDDU, sender, 7, message_id, index, count, {data, size}};
}

void test_single_fragment_completes() {
    auto subject = reassembler();
    const std::array<std::uint8_t, 3> bytes{{1, 2, 3}};
    const auto result = subject.process(fragment(1, 0, 1, bytes.data(), 3), 10);
    EXPECT(result.complete());
    EXPECT(result.message.size == 3);
    EXPECT(result.message.bytes[0] == 1 && result.message.bytes[2] == 3);
    EXPECT(subject.status().active_sessions == 0);
}

void test_two_fragment_alert_reorders_canonically() {
    auto subject = reassembler();
    std::array<std::uint8_t, 39> first{};
    std::array<std::uint8_t, 25> second{};
    first.fill(0x11);
    second.fill(0x22);
    const auto later = subject.process(fragment(2, 1, 2, second.data(), 25), 10);
    const auto complete = subject.process(fragment(2, 0, 2, first.data(), 39), 11);
    EXPECT(later.disposition == ProtectedReassemblyDisposition::accepted_incomplete);
    EXPECT(complete.complete() && complete.message.size == 64);
    EXPECT(complete.message.bytes[0] == 0x11);
    EXPECT(complete.message.bytes[38] == 0x11);
    EXPECT(complete.message.bytes[39] == 0x22);
    EXPECT(complete.message.bytes[63] == 0x22);
}

void test_exact_duplicate_is_idempotent() {
    auto subject = reassembler();
    const std::array<std::uint8_t, 2> bytes{{4, 5}};
    EXPECT(subject.process(fragment(3, 0, 2, bytes.data(), 2), 1).error ==
           ProtectedReassemblyError::none);
    const auto duplicate = subject.process(fragment(3, 0, 2, bytes.data(), 2), 2);
    EXPECT(duplicate.disposition == ProtectedReassemblyDisposition::duplicate);
    EXPECT(subject.status().active_sessions == 1);
    EXPECT(subject.status().duplicates == 1);
}

void test_conflicting_fragment_drops_session() {
    auto subject = reassembler();
    const std::array<std::uint8_t, 2> first{{1, 2}};
    const std::array<std::uint8_t, 2> changed{{1, 3}};
    (void)subject.process(fragment(4, 0, 2, first.data(), 2), 1);
    const auto result = subject.process(fragment(4, 0, 2, changed.data(), 2), 2);
    EXPECT(result.error == ProtectedReassemblyError::conflicting_fragment);
    EXPECT(subject.status().active_sessions == 0);
    EXPECT(subject.status().conflicts == 1);
}

void test_conflicting_fragment_count_drops_session() {
    auto subject = reassembler();
    const std::array<std::uint8_t, 1> bytes{{1}};
    (void)subject.process(fragment(5, 0, 2, bytes.data(), 1), 1);
    const auto result = subject.process(fragment(5, 1, 3, bytes.data(), 1), 2);
    EXPECT(result.error == ProtectedReassemblyError::conflicting_metadata);
    EXPECT(subject.status().active_sessions == 0);
}

void test_invalid_and_wrong_context_do_not_allocate() {
    auto subject = reassembler();
    const std::array<std::uint8_t, 1> bytes{{1}};
    auto invalid = fragment(6, 0, 0, bytes.data(), 1);
    auto wrong = fragment(6, 0, 2, bytes.data(), 1);
    wrong.group_epoch = 8;
    EXPECT(subject.process(invalid, 1).error ==
           ProtectedReassemblyError::invalid_fragment);
    EXPECT(subject.process(wrong, 2).error ==
           ProtectedReassemblyError::wrong_context);
    EXPECT(subject.status().active_sessions == 0);
}

void test_capacity_is_bounded() {
    auto subject = reassembler();
    const std::array<std::uint8_t, 1> bytes{{1}};
    for (std::uint32_t id = 1; id <= 4; ++id) {
        EXPECT(subject.process(fragment(id, 0, 2, bytes.data(), 1), id).error ==
               ProtectedReassemblyError::none);
    }
    const auto full = subject.process(fragment(9, 0, 2, bytes.data(), 1), 5);
    EXPECT(full.error == ProtectedReassemblyError::capacity_full);
    EXPECT(subject.status().active_sessions == 4);
}

void test_exact_timeout_frees_capacity() {
    auto subject = reassembler(10);
    const std::array<std::uint8_t, 1> bytes{{1}};
    (void)subject.process(fragment(1, 0, 2, bytes.data(), 1), 5);
    subject.service(14);
    EXPECT(subject.status().active_sessions == 1);
    subject.service(15);
    EXPECT(subject.status().active_sessions == 0);
    EXPECT(subject.status().expired == 1);
}

void test_clock_regression_fails_without_state_change() {
    auto subject = reassembler();
    const std::array<std::uint8_t, 1> bytes{{1}};
    (void)subject.process(fragment(1, 0, 2, bytes.data(), 1), 10);
    const auto result = subject.process(fragment(1, 1, 2, bytes.data(), 1), 9);
    EXPECT(result.error == ProtectedReassemblyError::clock_regression);
    EXPECT(subject.status().active_sessions == 1);
}

void test_maximum_fragment_count_and_size_complete() {
    auto subject = reassembler(1000);
    std::array<std::uint8_t, kProtectedReassemblyFragmentBytes> bytes{};
    for (std::uint8_t index = 0; index < kProtectedReassemblyFragments; ++index) {
        bytes.fill(index);
        const auto result = subject.process(
            fragment(88, index, kProtectedReassemblyFragments,
                     bytes.data(), bytes.size()), index);
        if (index + 1 == kProtectedReassemblyFragments) {
            EXPECT(result.complete());
            EXPECT(result.message.size == kProtectedReassemblyMessageBytes);
            EXPECT(result.message.bytes[0] == 0);
            EXPECT(result.message.bytes.back() == 15);
        } else {
            EXPECT(result.disposition ==
                   ProtectedReassemblyDisposition::accepted_incomplete);
        }
    }
}

}  // namespace

int main() {
    test_single_fragment_completes();
    test_two_fragment_alert_reorders_canonically();
    test_exact_duplicate_is_idempotent();
    test_conflicting_fragment_drops_session();
    test_conflicting_fragment_count_drops_session();
    test_invalid_and_wrong_context_do_not_allocate();
    test_capacity_is_bounded();
    test_exact_timeout_frees_capacity();
    test_clock_regression_fails_without_state_change();
    test_maximum_fragment_count_and_size_complete();
    if (failures != 0) {
        std::cerr << failures << " protected reassembly assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 protected reassembly scenario groups\n";
    return EXIT_SUCCESS;
}
