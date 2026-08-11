#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "fake_local_interface.hpp"
#include "opentrail/update_recovery_diagnostics.hpp"
#include "opentrail/update_recovery_presentation.hpp"

namespace {

using namespace opentrail::diagnostics;
using namespace opentrail::integration;
using namespace opentrail::ui;
using namespace opentrail::ui::test_support;
using namespace opentrail::update;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

DisplayCapabilities capabilities() {
    return {128, 64, 1, 2, false, true, false};
}

std::uint32_t diagnostic_word(const UpdateRecoveryStatus& status) {
    const auto encoded = encode_update_recovery_diagnostic(status);
    EXPECT(encoded.encoded());
    return encoded.word;
}

UpdateRecoveryStatus operational_status() {
    UpdateRecoveryStatus status{};
    status.operation = UpdateRecoveryStatusOperation::boot;
    status.state = UpdateRecoveryOperatorState::operational;
    status.reason = UpdateRecoveryOperatorReason::clean_baseline;
    status.action = UpdateRecoveryOperatorAction::continue_operation;
    status.operation_succeeded = true;
    status.normal_operation_blocked = false;
    status.attention_required = false;
    return status;
}

UpdateRecoveryStatus trial_status() {
    auto status = operational_status();
    status.state = UpdateRecoveryOperatorState::trial_active;
    status.reason =
        UpdateRecoveryOperatorReason::trial_confirmation_required;
    status.action = UpdateRecoveryOperatorAction::continue_trial;
    status.confirmation_required = true;
    return status;
}

UpdateRecoveryStatus saved_status() {
    auto status = operational_status();
    status.operation = UpdateRecoveryStatusOperation::save;
    status.state = UpdateRecoveryOperatorState::persistence_committed;
    status.reason = UpdateRecoveryOperatorReason::none;
    status.action = UpdateRecoveryOperatorAction::none;
    return status;
}

UpdateRecoveryStatus transition_rejected_status() {
    UpdateRecoveryStatus status{};
    status.operation = UpdateRecoveryStatusOperation::transition;
    status.state = UpdateRecoveryOperatorState::transition_rejected;
    status.reason = UpdateRecoveryOperatorReason::transition_rejected;
    status.action = UpdateRecoveryOperatorAction::none;
    status.normal_operation_blocked = false;
    return status;
}

UpdateRecoveryStatus rollback_status() {
    UpdateRecoveryStatus status{};
    status.operation = UpdateRecoveryStatusOperation::boot;
    status.state = UpdateRecoveryOperatorState::rollback_required;
    status.reason = UpdateRecoveryOperatorReason::boot_mismatch;
    status.action = UpdateRecoveryOperatorAction::reboot_to_baseline;
    status.operation_succeeded = true;
    status.reboot_required = true;
    return status;
}

UpdateRecoveryStatus cleanup_status() {
    auto status = rollback_status();
    status.state = UpdateRecoveryOperatorState::cleanup_required;
    status.reason = UpdateRecoveryOperatorReason::baseline_recovered;
    status.action = UpdateRecoveryOperatorAction::cleanup_update_state;
    status.normal_operation_blocked = false;
    status.reboot_required = false;
    status.cleanup_required = true;
    return status;
}

UpdateRecoveryStatus safe_mode_status() {
    UpdateRecoveryStatus status{};
    status.operation = UpdateRecoveryStatusOperation::boot;
    status.state = UpdateRecoveryOperatorState::safe_mode;
    status.reason = UpdateRecoveryOperatorReason::rollback_detected;
    return status;
}

UpdateRecoveryStatus reconciliation_status() {
    UpdateRecoveryStatus status{};
    status.operation = UpdateRecoveryStatusOperation::save;
    status.state =
        UpdateRecoveryOperatorState::reboot_reconcile_required;
    status.reason = UpdateRecoveryOperatorReason::commit_uncertain;
    status.action =
        UpdateRecoveryOperatorAction::reboot_and_reconcile;
    status.reboot_required = true;
    return status;
}

void expect_checked_frame(const UiFrame& frame) {
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface interface{display, input, capabilities()};
    EXPECT(interface.present(frame).ok());
    EXPECT(display.has_presented_frame());
    EXPECT(display.latest_frame().revision == frame.revision);
}

void test_normal_states_are_quiet_status_frames() {
    const auto operational = make_update_recovery_presentation(
        diagnostic_word(operational_status()), 1);
    const auto saved = make_update_recovery_presentation(
        diagnostic_word(saved_status()), 2);
    EXPECT(operational.decoded() && operational.presentable());
    EXPECT(saved.decoded() && saved.presentable());
    EXPECT(operational.frame.screen == UiScreen::status);
    EXPECT(operational.frame.attention == UiAttention::none);
    EXPECT(operational.frame.notice == UiNotice::none);
    EXPECT(operational.frame.action_count == 0);
    EXPECT(saved.frame.attention == UiAttention::none);
    EXPECT(saved.frame.notice == UiNotice::none);
    expect_checked_frame(operational.frame);
    expect_checked_frame(saved.frame);
}

void test_trial_notice_only_acknowledges_presentation() {
    const auto result = make_update_recovery_presentation(
        diagnostic_word(trial_status()), 10);
    EXPECT(result.decoded());
    EXPECT(result.frame.screen == UiScreen::status);
    EXPECT(result.frame.attention == UiAttention::warning);
    EXPECT(result.frame.notice == UiNotice::update_trial_active);
    EXPECT(result.frame.action_count == 1);
    EXPECT(result.frame.actions[0].action == UiAction::acknowledge_notice);

    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface interface{display, input, capabilities()};
    EXPECT(interface.present(result.frame).ok());
    EXPECT(input.enqueue_action(10, 0));
    const auto action = interface.poll_action();
    EXPECT(action.ok());
    EXPECT(action.action == UiAction::acknowledge_notice);
}

void test_rejected_transition_is_nonblocking_warning() {
    const auto result = make_update_recovery_presentation(
        diagnostic_word(transition_rejected_status()), 11);
    EXPECT(result.decoded());
    EXPECT(result.frame.screen == UiScreen::status);
    EXPECT(result.frame.attention == UiAttention::warning);
    EXPECT(result.frame.notice ==
           UiNotice::update_transition_rejected);
    EXPECT(result.frame.actions[0].action == UiAction::acknowledge_notice);
    expect_checked_frame(result.frame);
}

void test_rollback_requires_reboot_without_local_execution_action() {
    const auto result = make_update_recovery_presentation(
        diagnostic_word(rollback_status()), 12);
    EXPECT(result.decoded());
    EXPECT(result.frame.screen == UiScreen::system_fault);
    EXPECT(result.frame.attention == UiAttention::critical);
    EXPECT(result.frame.notice == UiNotice::update_reboot_required);
    EXPECT(result.frame.action_count == 0);
    expect_checked_frame(result.frame);
}

void test_cleanup_notice_does_not_execute_cleanup() {
    const auto result = make_update_recovery_presentation(
        diagnostic_word(cleanup_status()), 13);
    EXPECT(result.decoded());
    EXPECT(result.frame.screen == UiScreen::status);
    EXPECT(result.frame.attention == UiAttention::warning);
    EXPECT(result.frame.notice == UiNotice::update_cleanup_required);
    EXPECT(result.frame.actions[0].action == UiAction::acknowledge_notice);
    expect_checked_frame(result.frame);
}

void test_safe_and_service_states_are_distinct_system_faults() {
    const auto safe = make_update_recovery_presentation(
        diagnostic_word(safe_mode_status()), 14);
    const auto service = make_update_recovery_presentation(
        diagnostic_word(UpdateRecoveryStatus{}), 15);
    EXPECT(safe.decoded() && service.decoded());
    EXPECT(safe.frame.screen == UiScreen::system_fault);
    EXPECT(service.frame.screen == UiScreen::system_fault);
    EXPECT(safe.frame.notice == UiNotice::update_safe_mode);
    EXPECT(service.frame.notice == UiNotice::update_service_required);
    EXPECT(safe.frame.action_count == 0);
    EXPECT(service.frame.action_count == 0);
    expect_checked_frame(safe.frame);
    expect_checked_frame(service.frame);
}

void test_reconciliation_is_critical_without_false_reboot_action() {
    const auto result = make_update_recovery_presentation(
        diagnostic_word(reconciliation_status()), 16);
    EXPECT(result.decoded());
    EXPECT(result.frame.screen == UiScreen::system_fault);
    EXPECT(result.frame.attention == UiAttention::critical);
    EXPECT(result.frame.notice ==
           UiNotice::update_reconciliation_required);
    EXPECT(result.frame.action_count == 0);
    expect_checked_frame(result.frame);
}

void test_invalid_word_has_safe_service_fallback() {
    const auto result = make_update_recovery_presentation(0, 17);
    EXPECT(result.error ==
           UpdateRecoveryPresentationError::invalid_diagnostic);
    EXPECT(!result.decoded());
    EXPECT(result.presentable());
    EXPECT(result.frame.screen == UiScreen::system_fault);
    EXPECT(result.frame.attention == UiAttention::critical);
    EXPECT(result.frame.notice == UiNotice::update_service_required);
    EXPECT(result.frame.action_count == 0);
    expect_checked_frame(result.frame);
}

void test_zero_revision_cannot_create_a_presentable_frame() {
    const auto result = make_update_recovery_presentation(
        diagnostic_word(operational_status()), 0);
    EXPECT(result.error ==
           UpdateRecoveryPresentationError::invalid_revision);
    EXPECT(!result.presentable());
    EXPECT(result.frame.revision == 0);
}

static_assert(std::is_trivially_copyable_v<
              UpdateRecoveryPresentationResult>);
static_assert(sizeof(UpdateRecoveryPresentationResult) <= 64);

}  // namespace

int main() {
    test_normal_states_are_quiet_status_frames();
    test_trial_notice_only_acknowledges_presentation();
    test_rejected_transition_is_nonblocking_warning();
    test_rollback_requires_reboot_without_local_execution_action();
    test_cleanup_notice_does_not_execute_cleanup();
    test_safe_and_service_states_are_distinct_system_faults();
    test_reconciliation_is_critical_without_false_reboot_action();
    test_invalid_word_has_safe_service_fallback();
    test_zero_revision_cannot_create_a_presentable_frame();

    if (failures != 0) {
        std::cerr << failures
                  << " update recovery presentation assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 9 update recovery presentation scenario groups\n";
    return EXIT_SUCCESS;
}
