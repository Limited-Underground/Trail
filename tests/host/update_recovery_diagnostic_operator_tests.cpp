#include <cstdlib>
#include <iostream>
#include <string_view>
#include <type_traits>

#include "opentrail/update_recovery_diagnostics.hpp"

namespace {

using namespace opentrail::diagnostics;
using namespace opentrail::update;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

void test_canonical_baseline_message_parses() {
    const auto parsed = parse_update_recovery_diagnostic_message(
        "OTRD0=D0105084");
    EXPECT(parsed.parsed());
    EXPECT(parsed.word == 0xD0105084U);
    EXPECT(parsed.diagnostic.operation ==
           UpdateRecoveryStatusOperation::boot);
    EXPECT(parsed.diagnostic.state ==
           UpdateRecoveryOperatorState::operational);
    EXPECT(parsed.diagnostic.reason ==
           UpdateRecoveryOperatorReason::clean_baseline);
    EXPECT(parsed.diagnostic.action ==
           UpdateRecoveryOperatorAction::continue_operation);
    EXPECT(parsed.diagnostic.operation_succeeded);
    EXPECT(parsed.diagnostic.sensitive_detail_redacted);
}

void test_operation_names_are_stable() {
    constexpr UpdateRecoveryStatusOperation values[] = {
        UpdateRecoveryStatusOperation::boot,
        UpdateRecoveryStatusOperation::save,
        UpdateRecoveryStatusOperation::transition,
    };
    constexpr std::string_view names[] = {"boot", "save", "transition"};
    for (std::size_t index = 0; index < 3; ++index) {
        EXPECT(update_recovery_diagnostic_operation_name(values[index]) ==
               names[index]);
    }
}

void test_state_names_are_stable() {
    constexpr UpdateRecoveryOperatorState values[] = {
        UpdateRecoveryOperatorState::service_required,
        UpdateRecoveryOperatorState::operational,
        UpdateRecoveryOperatorState::trial_active,
        UpdateRecoveryOperatorState::persistence_committed,
        UpdateRecoveryOperatorState::transition_rejected,
        UpdateRecoveryOperatorState::rollback_required,
        UpdateRecoveryOperatorState::cleanup_required,
        UpdateRecoveryOperatorState::safe_mode,
        UpdateRecoveryOperatorState::reboot_reconcile_required,
    };
    constexpr std::string_view names[] = {
        "service_required",
        "operational",
        "trial_active",
        "persistence_committed",
        "transition_rejected",
        "rollback_required",
        "cleanup_required",
        "safe_mode",
        "reboot_reconcile_required",
    };
    for (std::size_t index = 0; index < 9; ++index) {
        EXPECT(update_recovery_diagnostic_state_name(values[index]) ==
               names[index]);
    }
}

void test_reason_names_are_stable() {
    constexpr UpdateRecoveryOperatorReason values[] = {
        UpdateRecoveryOperatorReason::invalid_result,
        UpdateRecoveryOperatorReason::none,
        UpdateRecoveryOperatorReason::clean_baseline,
        UpdateRecoveryOperatorReason::trial_confirmation_required,
        UpdateRecoveryOperatorReason::baseline_recovered,
        UpdateRecoveryOperatorReason::cleanup_required,
        UpdateRecoveryOperatorReason::invalid_configuration,
        UpdateRecoveryOperatorReason::live_state_invalid,
        UpdateRecoveryOperatorReason::trusted_state_unavailable,
        UpdateRecoveryOperatorReason::trusted_state_invalid,
        UpdateRecoveryOperatorReason::baseline_state_conflict,
        UpdateRecoveryOperatorReason::recovery_missing,
        UpdateRecoveryOperatorReason::storage_unavailable,
        UpdateRecoveryOperatorReason::rollback_detected,
        UpdateRecoveryOperatorReason::generation_conflict,
        UpdateRecoveryOperatorReason::generation_exhausted,
        UpdateRecoveryOperatorReason::checkpoint_rejected,
        UpdateRecoveryOperatorReason::boot_observation_rejected,
        UpdateRecoveryOperatorReason::rollback_observation_rejected,
        UpdateRecoveryOperatorReason::boot_mismatch,
        UpdateRecoveryOperatorReason::trial_boot_limit,
        UpdateRecoveryOperatorReason::trusted_reconciliation_required,
        UpdateRecoveryOperatorReason::commit_uncertain,
        UpdateRecoveryOperatorReason::trust_update_failed,
        UpdateRecoveryOperatorReason::transition_rejected,
        UpdateRecoveryOperatorReason::confirmation_committed,
        UpdateRecoveryOperatorReason::explicit_rollback,
        UpdateRecoveryOperatorReason::confirmation_timeout,
    };
    constexpr std::string_view names[] = {
        "invalid_result",
        "none",
        "clean_baseline",
        "trial_confirmation_required",
        "baseline_recovered",
        "cleanup_required",
        "invalid_configuration",
        "live_state_invalid",
        "trusted_state_unavailable",
        "trusted_state_invalid",
        "baseline_state_conflict",
        "recovery_missing",
        "storage_unavailable",
        "rollback_detected",
        "generation_conflict",
        "generation_exhausted",
        "checkpoint_rejected",
        "boot_observation_rejected",
        "rollback_observation_rejected",
        "boot_mismatch",
        "trial_boot_limit",
        "trusted_reconciliation_required",
        "commit_uncertain",
        "trust_update_failed",
        "transition_rejected",
        "confirmation_committed",
        "explicit_rollback",
        "confirmation_timeout",
    };
    for (std::size_t index = 0; index < 28; ++index) {
        EXPECT(update_recovery_diagnostic_reason_name(values[index]) ==
               names[index]);
    }
}

void test_action_names_are_stable() {
    constexpr UpdateRecoveryOperatorAction values[] = {
        UpdateRecoveryOperatorAction::service,
        UpdateRecoveryOperatorAction::none,
        UpdateRecoveryOperatorAction::continue_operation,
        UpdateRecoveryOperatorAction::continue_trial,
        UpdateRecoveryOperatorAction::reboot_to_baseline,
        UpdateRecoveryOperatorAction::reboot_and_reconcile,
        UpdateRecoveryOperatorAction::cleanup_update_state,
    };
    constexpr std::string_view names[] = {
        "service",
        "none",
        "continue_operation",
        "continue_trial",
        "reboot_to_baseline",
        "reboot_and_reconcile",
        "cleanup_update_state",
    };
    for (std::size_t index = 0; index < 7; ++index) {
        EXPECT(update_recovery_diagnostic_action_name(values[index]) ==
               names[index]);
    }
}

void test_message_shape_is_exact_and_uppercase() {
    constexpr std::string_view invalid[] = {
        "",
        "OTRD0=D010508",
        "OTRD0=D01050840",
        "OTRD1=D0105084",
        "otrd0=D0105084",
        "OTRD0=d0105084",
        " OTRD0=D0105084",
        "OTRD0=D0105084 ",
        "OTRD0=D01050G4",
    };
    for (const auto message : invalid) {
        const auto parsed =
            parse_update_recovery_diagnostic_message(message);
        EXPECT(!parsed.parsed());
        EXPECT(parsed.error ==
               UpdateRecoveryDiagnosticError::invalid_message);
    }
}

void test_invalid_word_is_distinct_from_invalid_text() {
    const auto invalid_magic =
        parse_update_recovery_diagnostic_message("OTRD0=C0105084");
    EXPECT(!invalid_magic.parsed());
    EXPECT(invalid_magic.error == UpdateRecoveryDiagnosticError::invalid_word);

    const auto reserved =
        parse_update_recovery_diagnostic_message("OTRD0=D0305084");
    EXPECT(!reserved.parsed());
    EXPECT(reserved.error == UpdateRecoveryDiagnosticError::invalid_word);
}

void test_unsupported_version_is_preserved() {
    const auto parsed =
        parse_update_recovery_diagnostic_message("OTRD0=D1105084");
    EXPECT(!parsed.parsed());
    EXPECT(parsed.word == 0xD1105084U);
    EXPECT(parsed.error ==
           UpdateRecoveryDiagnosticError::unsupported_version);
}

void test_error_names_and_unknown_values_are_defensive() {
    EXPECT(update_recovery_diagnostic_error_name(
               UpdateRecoveryDiagnosticError::invalid_message) ==
           "invalid_message");
    EXPECT(update_recovery_diagnostic_error_name(
               UpdateRecoveryDiagnosticError::invalid_word) ==
           "invalid_word");
    EXPECT(update_recovery_diagnostic_error_name(
               UpdateRecoveryDiagnosticError::unsupported_version) ==
           "unsupported_version");
    EXPECT(update_recovery_diagnostic_state_name(
               static_cast<UpdateRecoveryOperatorState>(255)) ==
           "unknown");
    EXPECT(update_recovery_diagnostic_reason_name(
               static_cast<UpdateRecoveryOperatorReason>(255)) ==
           "unknown");
}

void test_operator_result_remains_fixed_and_trivially_copyable() {
    EXPECT(std::is_trivially_copyable_v<
           UpdateRecoveryDiagnosticMessageResult>);
    EXPECT(sizeof(UpdateRecoveryDiagnosticMessageResult) <= 24);
    const auto parsed =
        parse_update_recovery_diagnostic_message("OTRD0=D0105084");
    EXPECT(parsed.diagnostic.sensitive_detail_redacted);
}

}  // namespace

int main() {
    test_canonical_baseline_message_parses();
    test_operation_names_are_stable();
    test_state_names_are_stable();
    test_reason_names_are_stable();
    test_action_names_are_stable();
    test_message_shape_is_exact_and_uppercase();
    test_invalid_word_is_distinct_from_invalid_text();
    test_unsupported_version_is_preserved();
    test_error_names_and_unknown_values_are_defensive();
    test_operator_result_remains_fixed_and_trivially_copyable();

    if (failures != 0) {
        std::cerr << failures
                  << " update-recovery operator test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "update-recovery diagnostic operator tests passed\n";
    return EXIT_SUCCESS;
}
