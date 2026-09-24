#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/selected_enrollment_request_owner.hpp"

namespace {
using namespace opentrail::companion;
int failures = 0;
void expect(bool condition, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << '\n';
        ++failures;
    }
}
#define EXPECT(condition) expect((condition), __LINE__)

DeviceNameAuthority authority(std::uint64_t now = 10) {
    return {DeviceNamePhase::connected, {1, 2, 3, 4, 5, 6, 7}, now};
}

void test_exact_admission_and_reentry() {
    SelectedEnrollmentRequestOwner owner;
    auto current = authority();
    EXPECT(owner.admit(current, 8, 9, 10) ==
           SelectedEnrollmentRequestResult::admitted);
    const auto first = owner.request();
    EXPECT(first.connection_handle == 8);
    EXPECT(first.authority.transport_generation == 5);
    EXPECT(first.authority.session_nonce == 7);
    EXPECT(first.delivery_token == 9);
    EXPECT(first.exchange_id == 10);
    EXPECT(first.deadline_ms == 120010);
    EXPECT(owner.admit(current, 11, 12, 13) ==
           SelectedEnrollmentRequestResult::busy);
    EXPECT(owner.request().delivery_token == first.delivery_token);
    EXPECT(owner.observe(current, 8));
    current.now_ms = 1000;
    EXPECT(owner.observe(current, 8));
    current.now_ms = 999;
    EXPECT(!owner.observe(current, 8));
    EXPECT(!owner.pending());
}

void test_stale_disconnect_cannot_cancel_new_request() {
    SelectedEnrollmentRequestOwner owner;
    auto current = authority();
    EXPECT(owner.admit(current, 8, 9, 10) ==
           SelectedEnrollmentRequestResult::admitted);
    const auto old = owner.request();
    EXPECT(owner.cancel_exact(old));
    current.context.transport_generation = 20;
    current.context.session_nonce = 21;
    EXPECT(owner.admit(current, 8, 22, 23) ==
           SelectedEnrollmentRequestResult::admitted);
    EXPECT(!owner.cancel_exact(old));
    auto wrong = owner.request();
    wrong.connection_handle = 7;
    EXPECT(!owner.cancel_exact(wrong));
    wrong = owner.request();
    wrong.delivery_token = 24;
    EXPECT(!owner.cancel_exact(wrong));
    wrong = owner.request();
    wrong.exchange_id = 24;
    EXPECT(!owner.cancel_exact(wrong));
    EXPECT(owner.pending());
    EXPECT(owner.cancel_exact(owner.request()));
    EXPECT(!owner.pending());
}

void test_current_authority_change_and_expiry() {
    SelectedEnrollmentRequestOwner owner;
    auto current = authority();
    EXPECT(owner.admit(current, 8, 9, 10) ==
           SelectedEnrollmentRequestResult::admitted);
    current.now_ms = 120009;
    EXPECT(owner.observe(current, 8));
    current.now_ms = 120010;
    EXPECT(owner.admit(current, 8, 11, 12) ==
           SelectedEnrollmentRequestResult::admitted);
    EXPECT(owner.request().delivery_token == 11);
    current.now_ms += SelectedEnrollmentRequestOwner::kPreparationLimitMs;
    EXPECT(!owner.observe(current, 8));
    EXPECT(!owner.pending());
    current = authority();
    EXPECT(owner.admit(current, 8, 9, 10) ==
           SelectedEnrollmentRequestResult::admitted);
    current.context.controller = 99;
    EXPECT(!owner.observe(current, 8));
    EXPECT(!owner.pending());
    current = authority();
    EXPECT(owner.admit(current, 8, 9, 10) ==
           SelectedEnrollmentRequestResult::admitted);
    current.phase = DeviceNamePhase::disconnected;
    EXPECT(!owner.observe(current, 8));
    EXPECT(!owner.pending());
    current = authority();
    EXPECT(owner.admit(current, 8, 9, 10) ==
           SelectedEnrollmentRequestResult::admitted);
    current.now_ms = 9;
    EXPECT(!owner.observe(current, 8));
    EXPECT(!owner.pending());
}

void test_invalid_and_terminal_containment() {
    SelectedEnrollmentRequestOwner owner;
    auto current = authority();
    current.context.owner_generation = 0;
    EXPECT(owner.admit(current, 8, 9, 10) ==
           SelectedEnrollmentRequestResult::invalid);
    current = authority();
    EXPECT(owner.admit(current, 0xffff, 9, 10) ==
           SelectedEnrollmentRequestResult::invalid);
    EXPECT(owner.admit(current, 8, 0, 10) ==
           SelectedEnrollmentRequestResult::invalid);
    EXPECT(owner.admit(current, 8, 9, 0) ==
           SelectedEnrollmentRequestResult::invalid);
    current.now_ms = std::numeric_limits<std::uint64_t>::max();
    EXPECT(owner.admit(current, 8, 9, 10) ==
           SelectedEnrollmentRequestResult::invalid);
    current = authority();
    EXPECT(owner.admit(current, 8, 9, 10) ==
           SelectedEnrollmentRequestResult::admitted);
    owner.retire();
    EXPECT(!owner.pending());
    EXPECT(owner.retired());
    EXPECT(owner.admit(current, 8, 9, 10) ==
           SelectedEnrollmentRequestResult::retired);
}
}  // namespace

int main() {
    test_exact_admission_and_reentry();
    test_stale_disconnect_cannot_cancel_new_request();
    test_current_authority_change_and_expiry();
    test_invalid_and_terminal_containment();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "PASS: selected enrollment request owner lifecycle\n";
    return EXIT_SUCCESS;
}
