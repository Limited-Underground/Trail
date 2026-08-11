#include <cstdlib>
#include <iostream>
#include <string_view>
#include <type_traits>

#include "opentrail/position_sharing_ui_diagnostics.hpp"

namespace {

using namespace opentrail::diagnostics;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

void test_canonical_stopped_message_parses() {
    const auto parsed = parse_position_sharing_ui_diagnostic_message(
        "OTPD0=C0012040");
    EXPECT(parsed.parsed());
    EXPECT(parsed.word == 0xC0012040U);
    EXPECT(parsed.diagnostic.event ==
           PositionSharingUiDiagnosticEvent::presentation);
    EXPECT(parsed.diagnostic.outcome ==
           PositionSharingUiDiagnosticOutcome::succeeded);
    EXPECT(parsed.diagnostic.notice ==
           PositionSharingUiDiagnosticNotice::stopped);
    EXPECT(parsed.diagnostic.reason ==
           PositionSharingUiDiagnosticReason::none);
    EXPECT(parsed.diagnostic.frame_presented);
    EXPECT(parsed.diagnostic.sensitive_detail_redacted);
}

void test_event_names_are_stable() {
    constexpr PositionSharingUiDiagnosticEvent values[] = {
        PositionSharingUiDiagnosticEvent::presentation,
        PositionSharingUiDiagnosticEvent::state_refresh,
        PositionSharingUiDiagnosticEvent::action,
        PositionSharingUiDiagnosticEvent::input,
        PositionSharingUiDiagnosticEvent::failure,
    };
    constexpr std::string_view names[] = {
        "presentation", "state_refresh", "action", "input", "failure"};
    for (std::size_t index = 0; index < 5; ++index) {
        EXPECT(position_sharing_ui_diagnostic_event_name(values[index]) ==
               names[index]);
    }
}

void test_outcome_names_are_stable() {
    constexpr PositionSharingUiDiagnosticOutcome values[] = {
        PositionSharingUiDiagnosticOutcome::succeeded,
        PositionSharingUiDiagnosticOutcome::deferred,
        PositionSharingUiDiagnosticOutcome::rejected,
        PositionSharingUiDiagnosticOutcome::contained,
        PositionSharingUiDiagnosticOutcome::failed,
    };
    constexpr std::string_view names[] = {
        "succeeded", "deferred", "rejected", "contained", "failed"};
    for (std::size_t index = 0; index < 5; ++index) {
        EXPECT(position_sharing_ui_diagnostic_outcome_name(values[index]) ==
               names[index]);
    }
}

void test_notice_names_are_stable() {
    constexpr PositionSharingUiDiagnosticNotice values[] = {
        PositionSharingUiDiagnosticNotice::none,
        PositionSharingUiDiagnosticNotice::stopped,
        PositionSharingUiDiagnosticNotice::active,
        PositionSharingUiDiagnosticNotice::waiting_for_fix,
        PositionSharingUiDiagnosticNotice::deferred,
        PositionSharingUiDiagnosticNotice::failed,
    };
    constexpr std::string_view names[] = {
        "none", "stopped", "active", "waiting_for_fix", "deferred", "failed"};
    for (std::size_t index = 0; index < 6; ++index) {
        EXPECT(position_sharing_ui_diagnostic_notice_name(values[index]) ==
               names[index]);
    }
}

void test_reason_names_are_stable() {
    constexpr PositionSharingUiDiagnosticReason values[] = {
        PositionSharingUiDiagnosticReason::none,
        PositionSharingUiDiagnosticReason::clock_not_ready,
        PositionSharingUiDiagnosticReason::outbound_faulted,
        PositionSharingUiDiagnosticReason::stale_input,
        PositionSharingUiDiagnosticReason::invalid_input,
        PositionSharingUiDiagnosticReason::input_source_failed,
        PositionSharingUiDiagnosticReason::display_not_ready,
        PositionSharingUiDiagnosticReason::display_failed,
        PositionSharingUiDiagnosticReason::revision_exhausted,
        PositionSharingUiDiagnosticReason::presentation_unavailable,
        PositionSharingUiDiagnosticReason::command_rejected,
        PositionSharingUiDiagnosticReason::invalid_initial_revision,
        PositionSharingUiDiagnosticReason::refresh_contained,
    };
    constexpr std::string_view names[] = {
        "none",
        "clock_not_ready",
        "outbound_faulted",
        "stale_input",
        "invalid_input",
        "input_source_failed",
        "display_not_ready",
        "display_failed",
        "revision_exhausted",
        "presentation_unavailable",
        "command_rejected",
        "invalid_initial_revision",
        "refresh_contained",
    };
    for (std::size_t index = 0; index < 13; ++index) {
        EXPECT(position_sharing_ui_diagnostic_reason_name(values[index]) ==
               names[index]);
    }
}

void test_message_shape_is_exact_and_uppercase() {
    constexpr std::string_view invalid[] = {
        "",
        "OTPD0=C001204",
        "OTPD0=C00120400",
        "OTPD1=C0012040",
        "otpd0=C0012040",
        "OTPD0=c0012040",
        " OTPD0=C0012040",
        "OTPD0=C0012040 ",
        "OTPD0=C00120G0",
    };
    for (const auto message : invalid) {
        const auto parsed =
            parse_position_sharing_ui_diagnostic_message(message);
        EXPECT(!parsed.parsed());
        EXPECT(parsed.error ==
               PositionSharingUiDiagnosticError::invalid_message);
    }
}

void test_invalid_word_is_distinct_from_invalid_text() {
    const auto invalid_magic =
        parse_position_sharing_ui_diagnostic_message("OTPD0=00012040");
    EXPECT(!invalid_magic.parsed());
    EXPECT(invalid_magic.error ==
           PositionSharingUiDiagnosticError::invalid_word);

    const auto reserved_bits =
        parse_position_sharing_ui_diagnostic_message("OTPD0=C0032040");
    EXPECT(!reserved_bits.parsed());
    EXPECT(reserved_bits.error ==
           PositionSharingUiDiagnosticError::invalid_word);
}

void test_unsupported_version_is_preserved() {
    const auto parsed = parse_position_sharing_ui_diagnostic_message(
        "OTPD0=C1012040");
    EXPECT(!parsed.parsed());
    EXPECT(parsed.word == 0xC1012040U);
    EXPECT(parsed.error ==
           PositionSharingUiDiagnosticError::unsupported_version);
}

void test_error_names_are_fixed_and_unknown_values_are_defensive() {
    EXPECT(position_sharing_ui_diagnostic_error_name(
               PositionSharingUiDiagnosticError::invalid_message) ==
           "invalid_message");
    EXPECT(position_sharing_ui_diagnostic_error_name(
               PositionSharingUiDiagnosticError::invalid_word) ==
           "invalid_word");
    EXPECT(position_sharing_ui_diagnostic_error_name(
               PositionSharingUiDiagnosticError::unsupported_version) ==
           "unsupported_version");
    EXPECT(position_sharing_ui_diagnostic_event_name(
               static_cast<PositionSharingUiDiagnosticEvent>(255)) ==
           "unknown");
    EXPECT(position_sharing_ui_diagnostic_reason_name(
               static_cast<PositionSharingUiDiagnosticReason>(255)) ==
           "unknown");
}

void test_operator_result_remains_fixed_and_trivially_copyable() {
    EXPECT(std::is_trivially_copyable_v<
           PositionSharingUiDiagnosticMessageResult>);
    EXPECT(sizeof(PositionSharingUiDiagnosticMessageResult) <= 16);
    const auto parsed = parse_position_sharing_ui_diagnostic_message(
        "OTPD0=C0012040");
    EXPECT(parsed.diagnostic.sensitive_detail_redacted);
}

}  // namespace

int main() {
    test_canonical_stopped_message_parses();
    test_event_names_are_stable();
    test_outcome_names_are_stable();
    test_notice_names_are_stable();
    test_reason_names_are_stable();
    test_message_shape_is_exact_and_uppercase();
    test_invalid_word_is_distinct_from_invalid_text();
    test_unsupported_version_is_preserved();
    test_error_names_are_fixed_and_unknown_values_are_defensive();
    test_operator_result_remains_fixed_and_trivially_copyable();

    if (failures != 0) {
        std::cerr << failures << " operator diagnostic test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "position-sharing UI diagnostic operator tests passed\n";
    return EXIT_SUCCESS;
}
