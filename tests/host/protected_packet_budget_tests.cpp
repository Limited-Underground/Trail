#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/protected_packet_budget.hpp"

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

ProtectedPacketBudgetRequest request(
    std::size_t plaintext_bytes,
    std::size_t mtu = 163,
    std::size_t maximum_fragments = 16) {
    return {
        mtu,
        kCandidateProtectedHeaderBytes,
        kCandidateAuthenticationTagBytes,
        0,
        0,
        plaintext_bytes,
        maximum_fragments,
        {62500, 8, 7, 5, true, true, false},
    };
}

void test_candidate_mtu_budgets_are_explicit() {
    const auto meshcore = calculate_protected_packet_budget(request(1, 163));
    const auto direct = calculate_protected_packet_budget(request(1, 255));
    EXPECT(meshcore.calculated() && direct.calculated());
    EXPECT(meshcore.overhead_bytes == 60);
    EXPECT(meshcore.maximum_plaintext_per_frame == 103);
    EXPECT(direct.maximum_plaintext_per_frame == 195);
}

void test_position_payload_fits_one_protected_frame() {
    const auto result = calculate_protected_packet_budget(request(16));
    EXPECT(result.calculated());
    EXPECT(result.fragment_count == 1);
    EXPECT(result.final_frame_bytes == 76);
    EXPECT(result.total_frame_bytes == 76);
    EXPECT(result.total_airtime_us == 276992);
}

void test_signed_group_source_authentication_is_not_free() {
    auto signed_group = request(16);
    signed_group.source_authentication_bytes =
        kEd25519CandidateSignatureBytes;
    const auto result = calculate_protected_packet_budget(signed_group);
    EXPECT(result.calculated());
    EXPECT(result.overhead_bytes == 124);
    EXPECT(result.maximum_plaintext_per_frame == 39);
    EXPECT(result.final_frame_bytes == 140);
    EXPECT(result.total_airtime_us == 461312);
}

void test_signed_critical_alert_requires_two_candidate_fragments() {
    auto alert = request(64);
    alert.source_authentication_bytes = kEd25519CandidateSignatureBytes;
    const auto result = calculate_protected_packet_budget(alert);
    EXPECT(result.calculated());
    EXPECT(result.maximum_plaintext_per_frame == 39);
    EXPECT(result.fragment_count == 2);
    EXPECT(result.total_frame_bytes == 312);
    EXPECT(result.final_frame_bytes == 149);
    EXPECT(result.total_airtime_us == 1025024);
}

void test_optional_forwarding_wrapper_is_charged_separately() {
    auto layered = request(16);
    layered.source_authentication_bytes = kEd25519CandidateSignatureBytes;
    layered.forwarding_wrapper_bytes = 16;
    const auto result = calculate_protected_packet_budget(layered);
    EXPECT(result.calculated());
    EXPECT(result.overhead_bytes == 140);
    EXPECT(result.maximum_plaintext_per_frame == 23);
    EXPECT(result.final_frame_bytes == 156);
}

void test_fragmentation_charges_every_header_and_tag() {
    const auto result = calculate_protected_packet_budget(request(300));
    EXPECT(result.calculated());
    EXPECT(result.fragment_count == 3);
    EXPECT(result.total_frame_bytes == 480);
    EXPECT(result.final_frame_bytes == 154);
    EXPECT(result.total_airtime_us == 1568256);
}

void test_empty_plaintext_still_has_one_authenticated_frame() {
    const auto result = calculate_protected_packet_budget(request(0));
    EXPECT(result.calculated());
    EXPECT(result.fragment_count == 1);
    EXPECT(result.total_frame_bytes == 60);
    EXPECT(result.final_frame_bytes == 60);
    EXPECT(result.total_airtime_us == 225792);
}

void test_exact_boundary_and_next_byte_are_distinct() {
    const auto exact = calculate_protected_packet_budget(request(103));
    const auto next = calculate_protected_packet_budget(request(104));
    EXPECT(exact.calculated() && exact.fragment_count == 1);
    EXPECT(exact.final_frame_bytes == 163);
    EXPECT(next.calculated() && next.fragment_count == 2);
    EXPECT(next.total_frame_bytes == 224);
    EXPECT(next.final_frame_bytes == 61);
}

void test_fragment_limit_fails_without_partial_totals() {
    const auto result = calculate_protected_packet_budget(request(104, 163, 1));
    EXPECT(result.error == ProtectedPacketBudgetError::fragment_limit_exceeded);
    EXPECT(result.fragment_count == 0);
    EXPECT(result.total_frame_bytes == 0 && result.total_airtime_us == 0);
}

void test_invalid_capacity_and_airtime_fail_closed() {
    auto invalid = request(1);
    invalid.transport_mtu = 0;
    EXPECT(calculate_protected_packet_budget(invalid).error ==
           ProtectedPacketBudgetError::invalid_request);

    invalid = request(1, 60);
    EXPECT(calculate_protected_packet_budget(invalid).error ==
           ProtectedPacketBudgetError::no_plaintext_capacity);

    invalid = request(1);
    invalid.maximum_fragments = 17;
    EXPECT(calculate_protected_packet_budget(invalid).error ==
           ProtectedPacketBudgetError::invalid_request);

    invalid = request(1);
    invalid.airtime.bandwidth_hz = 0;
    EXPECT(calculate_protected_packet_budget(invalid).error ==
           ProtectedPacketBudgetError::airtime_failure);

    invalid = request(std::numeric_limits<std::size_t>::max());
    EXPECT(calculate_protected_packet_budget(invalid).error ==
           ProtectedPacketBudgetError::fragment_limit_exceeded);
}

}  // namespace

int main() {
    test_candidate_mtu_budgets_are_explicit();
    test_position_payload_fits_one_protected_frame();
    test_signed_group_source_authentication_is_not_free();
    test_signed_critical_alert_requires_two_candidate_fragments();
    test_optional_forwarding_wrapper_is_charged_separately();
    test_fragmentation_charges_every_header_and_tag();
    test_empty_plaintext_still_has_one_authenticated_frame();
    test_exact_boundary_and_next_byte_are_distinct();
    test_fragment_limit_fails_without_partial_totals();
    test_invalid_capacity_and_airtime_fail_closed();
    if (failures != 0) {
        std::cerr << failures
                  << " protected packet budget assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 protected packet budget scenario groups\n";
    return EXIT_SUCCESS;
}
