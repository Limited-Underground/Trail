#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/update_boot_guard.hpp"

namespace {

using namespace opentrail::update;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

UpdateGuardPolicy policy(std::uint8_t maximum_boots = 2) {
    return {
        0x1234U,
        10,
        ImageSlot::slot_a,
        health_bit(TrialHealth::runtime_started) |
            health_bit(TrialHealth::watchdog_healthy) |
            health_bit(TrialHealth::configuration_loaded),
        100,
        500,
        maximum_boots,
        4U * 1024U * 1024U};
}

VerifiedUpdateCandidate candidate() {
    return {
        0x1234U,
        11,
        ImageSlot::slot_b,
        1024U * 1024U,
        true,
        true,
        true,
        true};
}

CandidateWriteEvidence written() {
    return {11, ImageSlot::slot_b, true, true};
}

void stage_and_write(UpdateBootGuard& guard) {
    EXPECT(guard.start(policy()) == UpdateGuardError::none);
    EXPECT(guard.stage(candidate()) == UpdateGuardError::none);
    EXPECT(guard.mark_written(written()) == UpdateGuardError::none);
}

void test_policy_and_candidate_validation() {
    UpdateBootGuard guard{};
    auto invalid_policy = policy();
    invalid_policy.required_health_mask = 0;
    EXPECT(guard.start(invalid_policy) ==
           UpdateGuardError::invalid_configuration);
    EXPECT(guard.start(policy()) == UpdateGuardError::none);
    EXPECT(guard.start(policy()) == UpdateGuardError::invalid_state);

    auto image = candidate();
    image.hardware_id = 99;
    EXPECT(guard.stage(image) == UpdateGuardError::invalid_candidate);
    image = candidate();
    image.version = 10;
    EXPECT(guard.stage(image) == UpdateGuardError::invalid_candidate);
    image = candidate();
    image.target_slot = ImageSlot::slot_a;
    EXPECT(guard.stage(image) == UpdateGuardError::invalid_candidate);
    image = candidate();
    image.authenticity_verified = false;
    EXPECT(guard.stage(image) == UpdateGuardError::verification_required);
    EXPECT(guard.status().state == UpdateState::idle);
}

void test_staging_cancel_and_write_evidence() {
    UpdateBootGuard guard{};
    EXPECT(guard.start(policy()) == UpdateGuardError::none);
    EXPECT(guard.stage(candidate()) == UpdateGuardError::none);
    auto evidence = written();
    evidence.full_readback_verified = false;
    EXPECT(guard.mark_written(evidence) ==
           UpdateGuardError::verification_required);
    EXPECT(guard.cancel_staged() == UpdateGuardError::none);
    EXPECT(guard.status().state == UpdateState::idle);
    EXPECT(guard.stage(candidate()) == UpdateGuardError::none);
    EXPECT(guard.mark_written(written()) == UpdateGuardError::none);
    EXPECT(guard.status().state == UpdateState::pending_reboot);
    EXPECT(guard.cancel_staged() == UpdateGuardError::invalid_state);
}

void test_health_and_minimum_stable_confirmation() {
    UpdateBootGuard guard{};
    stage_and_write(guard);
    EXPECT(guard.begin_boot({1, 11, ImageSlot::slot_b, 1000}) ==
           UpdateGuardError::none);
    EXPECT(guard.report_health(
               1, health_bit(TrialHealth::runtime_started), 1050) ==
           UpdateGuardError::none);
    EXPECT(guard.confirm(1, 1099) ==
           UpdateGuardError::insufficient_stable_time);
    EXPECT(guard.report_health(
               1,
               health_bit(TrialHealth::watchdog_healthy) |
                   health_bit(TrialHealth::configuration_loaded),
               1100) == UpdateGuardError::none);
    EXPECT(guard.confirm(1, 1100) == UpdateGuardError::none);
    EXPECT(guard.status().state == UpdateState::confirmed);
}

void test_exact_confirmation_deadline_requires_rollback() {
    UpdateBootGuard guard{};
    stage_and_write(guard);
    EXPECT(guard.begin_boot({2, 11, ImageSlot::slot_b, 100}) ==
           UpdateGuardError::none);
    EXPECT(guard.report_health(2, policy().required_health_mask, 599) ==
           UpdateGuardError::none);
    EXPECT(guard.confirm(2, 600) ==
           UpdateGuardError::confirmation_timeout);
    EXPECT(guard.status().state == UpdateState::rollback_required);
    EXPECT(guard.status().rollback_reason ==
           RollbackReason::confirmation_timeout);
}

void test_trial_boot_attempt_limit() {
    UpdateBootGuard guard{};
    stage_and_write(guard);
    EXPECT(guard.begin_boot({1, 11, ImageSlot::slot_b, 0}) ==
           UpdateGuardError::none);
    EXPECT(guard.begin_boot({2, 11, ImageSlot::slot_b, 0}) ==
           UpdateGuardError::none);
    EXPECT(guard.status().trial_boots == 2);
    EXPECT(guard.begin_boot({3, 11, ImageSlot::slot_b, 0}) ==
           UpdateGuardError::boot_attempt_limit);
    EXPECT(guard.status().rollback_reason ==
           RollbackReason::boot_attempt_limit);
}

void test_boot_mismatch_and_rollback_completion() {
    UpdateBootGuard guard{};
    stage_and_write(guard);
    EXPECT(guard.begin_boot({1, 10, ImageSlot::slot_a, 0}) ==
           UpdateGuardError::boot_mismatch);
    EXPECT(guard.status().rollback_reason == RollbackReason::boot_mismatch);
    EXPECT(guard.complete_rollback({2, 9, ImageSlot::slot_a, 0}) ==
           UpdateGuardError::rollback_mismatch);
    EXPECT(guard.complete_rollback({2, 10, ImageSlot::slot_a, 1}) ==
           UpdateGuardError::none);
    EXPECT(guard.status().state == UpdateState::rolled_back);
}

void test_explicit_health_failure_requests_rollback() {
    UpdateBootGuard guard{};
    stage_and_write(guard);
    EXPECT(guard.request_rollback(RollbackReason::none) ==
           UpdateGuardError::invalid_state);
    EXPECT(guard.begin_boot({1, 11, ImageSlot::slot_b, 10}) ==
           UpdateGuardError::none);
    EXPECT(guard.report_health(99, policy().required_health_mask, 11) ==
           UpdateGuardError::invalid_state);
    EXPECT(guard.report_health(1, 1U << 31U, 11) ==
           UpdateGuardError::invalid_candidate);
    EXPECT(guard.request_rollback(
               RollbackReason::explicit_health_failure) ==
           UpdateGuardError::none);
    EXPECT(guard.status().state == UpdateState::rollback_required);
}

void test_monotonic_time_and_duplicate_boot_session() {
    UpdateBootGuard guard{};
    stage_and_write(guard);
    EXPECT(guard.begin_boot({5, 11, ImageSlot::slot_b, 100}) ==
           UpdateGuardError::none);
    EXPECT(guard.begin_boot({5, 11, ImageSlot::slot_b, 101}) ==
           UpdateGuardError::invalid_state);
    EXPECT(guard.report_health(
               5, health_bit(TrialHealth::runtime_started), 99) ==
           UpdateGuardError::clock_regression);
    EXPECT(guard.tick(100) == UpdateGuardError::none);
    guard.stop();
    EXPECT(guard.tick(101) == UpdateGuardError::invalid_state);
}

}  // namespace

int main() {
    test_policy_and_candidate_validation();
    test_staging_cancel_and_write_evidence();
    test_health_and_minimum_stable_confirmation();
    test_exact_confirmation_deadline_requires_rollback();
    test_trial_boot_attempt_limit();
    test_boot_mismatch_and_rollback_completion();
    test_explicit_health_failure_requests_rollback();
    test_monotonic_time_and_duplicate_boot_session();

    if (failures != 0) {
        std::cerr << failures << " update boot guard assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 update boot guard scenario groups\n";
    return EXIT_SUCCESS;
}


