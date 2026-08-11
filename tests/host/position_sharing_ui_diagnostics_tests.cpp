#include <cstdlib>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <utility>

#include "memory_log_sink.hpp"
#include "opentrail/position_sharing_ui_diagnostics.hpp"

namespace {

using namespace opentrail::diagnostics;
using namespace opentrail::diagnostics::test_support;
using namespace opentrail::integration;
using namespace opentrail::location;
using namespace opentrail::ui;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

PositionSharingUiServiceResult presented(
    UiNotice notice,
    PositionSharingUiDisposition disposition =
        PositionSharingUiDisposition::presented,
    std::uint32_t revision = 1) {
    PositionSharingUiServiceResult result{};
    result.disposition = disposition;
    result.presented_notice = notice;
    result.revision = revision;
    result.frame_presented = true;
    return result;
}

PositionSharingUiServiceResult failed(PositionSharingUiError error) {
    PositionSharingUiServiceResult result{};
    result.disposition = PositionSharingUiDisposition::failed;
    result.error = error;
    return result;
}

void test_exact_stopped_presentation_word_and_round_trip() {
    const auto encoded = encode_position_sharing_ui_diagnostic(
        presented(UiNotice::position_sharing_stopped));
    EXPECT(encoded.encoded());
    EXPECT(encoded.word == 0xC0012040U);

    const auto decoded =
        decode_position_sharing_ui_diagnostic(encoded.word);
    EXPECT(decoded.decoded());
    EXPECT(decoded.diagnostic.event ==
           PositionSharingUiDiagnosticEvent::presentation);
    EXPECT(decoded.diagnostic.outcome ==
           PositionSharingUiDiagnosticOutcome::succeeded);
    EXPECT(decoded.diagnostic.notice ==
           PositionSharingUiDiagnosticNotice::stopped);
    EXPECT(decoded.diagnostic.reason ==
           PositionSharingUiDiagnosticReason::none);
    EXPECT(decoded.diagnostic.frame_presented);
    EXPECT(decoded.diagnostic.sensitive_detail_redacted);

    const auto message =
        detail::format_position_sharing_ui_diagnostic(encoded.word);
    EXPECT(std::string_view(
               message.data(), kPositionSharingUiDiagnosticMessageBytes) ==
           "OTPD0=C0012040");
}

void test_visible_refresh_notices_map_without_runtime_detail() {
    constexpr UiNotice notices[] = {
        UiNotice::position_sharing_stopped,
        UiNotice::position_sharing_active,
        UiNotice::position_sharing_waiting_for_fix,
        UiNotice::position_sharing_deferred,
    };
    constexpr PositionSharingUiDiagnosticNotice expected[] = {
        PositionSharingUiDiagnosticNotice::stopped,
        PositionSharingUiDiagnosticNotice::active,
        PositionSharingUiDiagnosticNotice::waiting_for_fix,
        PositionSharingUiDiagnosticNotice::deferred,
    };
    for (std::size_t index = 0; index < 4; ++index) {
        const auto decoded = decode_position_sharing_ui_diagnostic(
            encode_position_sharing_ui_diagnostic(presented(
                notices[index],
                PositionSharingUiDisposition::refreshed,
                static_cast<std::uint32_t>(index + 2)))
                .word);
        EXPECT(decoded.decoded());
        EXPECT(decoded.diagnostic.event ==
               PositionSharingUiDiagnosticEvent::state_refresh);
        EXPECT(decoded.diagnostic.notice == expected[index]);
        EXPECT(decoded.diagnostic.reason ==
               PositionSharingUiDiagnosticReason::none);
    }
}

void test_action_success_deferral_and_rejection_are_distinct() {
    auto applied = presented(
        UiNotice::position_sharing_active,
        PositionSharingUiDisposition::action_applied,
        2);
    applied.state_changed = true;
    const auto applied_decoded = decode_position_sharing_ui_diagnostic(
        encode_position_sharing_ui_diagnostic(applied).word);
    EXPECT(applied_decoded.decoded());
    EXPECT(applied_decoded.diagnostic.event ==
           PositionSharingUiDiagnosticEvent::action);
    EXPECT(applied_decoded.diagnostic.outcome ==
           PositionSharingUiDiagnosticOutcome::succeeded);
    EXPECT(applied_decoded.diagnostic.state_changed);

    PositionSharingUiServiceResult deferred{};
    deferred.disposition = PositionSharingUiDisposition::action_deferred;
    deferred.control_error =
        PositionSharingControlError::outbound_not_ready;
    const auto deferred_decoded = decode_position_sharing_ui_diagnostic(
        encode_position_sharing_ui_diagnostic(deferred).word);
    EXPECT(deferred_decoded.decoded());
    EXPECT(deferred_decoded.diagnostic.outcome ==
           PositionSharingUiDiagnosticOutcome::deferred);
    EXPECT(deferred_decoded.diagnostic.reason ==
           PositionSharingUiDiagnosticReason::clock_not_ready);

    auto rejected = presented(
        UiNotice::position_sharing_failed,
        PositionSharingUiDisposition::action_rejected,
        3);
    rejected.control_error =
        PositionSharingControlError::scheduler_rejected;
    rejected.scheduler_error =
        PositionBroadcastScheduleError::invalid_policy;
    rejected.presentation_error =
        PositionSharingPresentationError::invalid_scheduler_status;
    const auto rejected_decoded = decode_position_sharing_ui_diagnostic(
        encode_position_sharing_ui_diagnostic(rejected).word);
    EXPECT(rejected_decoded.decoded());
    EXPECT(rejected_decoded.diagnostic.outcome ==
           PositionSharingUiDiagnosticOutcome::rejected);
    EXPECT(rejected_decoded.diagnostic.reason ==
           PositionSharingUiDiagnosticReason::command_rejected);
}

void test_input_rejection_and_failure_are_coarse() {
    PositionSharingUiServiceResult stale{};
    stale.disposition = PositionSharingUiDisposition::input_rejected;
    stale.action_error = ActionResolutionError::stale_frame;
    auto decoded = decode_position_sharing_ui_diagnostic(
        encode_position_sharing_ui_diagnostic(stale).word);
    EXPECT(decoded.decoded());
    EXPECT(decoded.diagnostic.event ==
           PositionSharingUiDiagnosticEvent::input);
    EXPECT(decoded.diagnostic.reason ==
           PositionSharingUiDiagnosticReason::stale_input);

    stale.action_error = ActionResolutionError::invalid_slot;
    decoded = decode_position_sharing_ui_diagnostic(
        encode_position_sharing_ui_diagnostic(stale).word);
    EXPECT(decoded.decoded());
    EXPECT(decoded.diagnostic.reason ==
           PositionSharingUiDiagnosticReason::invalid_input);

    auto input_failure = failed(PositionSharingUiError::input_failed);
    input_failure.action_error = ActionResolutionError::input_failed;
    decoded = decode_position_sharing_ui_diagnostic(
        encode_position_sharing_ui_diagnostic(input_failure).word);
    EXPECT(decoded.decoded());
    EXPECT(decoded.diagnostic.event ==
           PositionSharingUiDiagnosticEvent::failure);
    EXPECT(decoded.diagnostic.reason ==
           PositionSharingUiDiagnosticReason::input_source_failed);
}

void test_display_and_presentation_failures_preserve_only_category() {
    auto display_not_ready = failed(PositionSharingUiError::display_failed);
    display_not_ready.present_error = PresentError::sink_not_ready;
    auto decoded = decode_position_sharing_ui_diagnostic(
        encode_position_sharing_ui_diagnostic(display_not_ready).word);
    EXPECT(decoded.decoded());
    EXPECT(decoded.diagnostic.reason ==
           PositionSharingUiDiagnosticReason::display_not_ready);
    EXPECT(detail::position_sharing_ui_diagnostic_level(
               decoded.diagnostic) == LogLevel::warn);

    auto display_failed = failed(PositionSharingUiError::display_failed);
    display_failed.present_error = PresentError::sink_failed;
    decoded = decode_position_sharing_ui_diagnostic(
        encode_position_sharing_ui_diagnostic(display_failed).word);
    EXPECT(decoded.decoded());
    EXPECT(decoded.diagnostic.reason ==
           PositionSharingUiDiagnosticReason::display_failed);
    EXPECT(detail::position_sharing_ui_diagnostic_level(
               decoded.diagnostic) == LogLevel::error);

    auto unavailable =
        failed(PositionSharingUiError::presentation_unavailable);
    unavailable.presentation_error =
        PositionSharingPresentationError::invalid_revision;
    decoded = decode_position_sharing_ui_diagnostic(
        encode_position_sharing_ui_diagnostic(unavailable).word);
    EXPECT(decoded.decoded());
    EXPECT(decoded.diagnostic.reason ==
           PositionSharingUiDiagnosticReason::presentation_unavailable);
}

void test_containment_reasons_are_explicit_and_identifier_free() {
    auto external =
        failed(PositionSharingUiError::external_refresh_failed);
    external.present_error = PresentError::sink_failed;
    auto decoded = decode_position_sharing_ui_diagnostic(
        encode_position_sharing_ui_diagnostic(external).word);
    EXPECT(decoded.decoded());
    EXPECT(decoded.diagnostic.outcome ==
           PositionSharingUiDiagnosticOutcome::contained);
    EXPECT(decoded.diagnostic.sharing_contained);
    EXPECT(decoded.diagnostic.reason ==
           PositionSharingUiDiagnosticReason::display_failed);

    auto post_action =
        failed(PositionSharingUiError::post_action_refresh_failed);
    decoded = decode_position_sharing_ui_diagnostic(
        encode_position_sharing_ui_diagnostic(post_action).word);
    EXPECT(decoded.decoded());
    EXPECT(decoded.diagnostic.reason ==
           PositionSharingUiDiagnosticReason::refresh_contained);

    auto presentation =
        failed(PositionSharingUiError::external_refresh_failed);
    presentation.presentation_error =
        PositionSharingPresentationError::invalid_scheduler_status;
    decoded = decode_position_sharing_ui_diagnostic(
        encode_position_sharing_ui_diagnostic(presentation).word);
    EXPECT(decoded.decoded());
    EXPECT(decoded.diagnostic.reason ==
           PositionSharingUiDiagnosticReason::presentation_unavailable);

    auto exhausted = failed(PositionSharingUiError::revision_exhausted);
    decoded = decode_position_sharing_ui_diagnostic(
        encode_position_sharing_ui_diagnostic(exhausted).word);
    EXPECT(decoded.decoded());
    EXPECT(decoded.diagnostic.reason ==
           PositionSharingUiDiagnosticReason::revision_exhausted);
}

void test_outbound_fault_is_critical_even_when_frame_commits() {
    auto fault = presented(
        UiNotice::position_sharing_failed,
        PositionSharingUiDisposition::refreshed,
        4);
    fault.presentation_error =
        PositionSharingPresentationError::outbound_faulted;
    const auto decoded = decode_position_sharing_ui_diagnostic(
        encode_position_sharing_ui_diagnostic(fault).word);
    EXPECT(decoded.decoded());
    EXPECT(decoded.diagnostic.outcome ==
           PositionSharingUiDiagnosticOutcome::succeeded);
    EXPECT(decoded.diagnostic.notice ==
           PositionSharingUiDiagnosticNotice::failed);
    EXPECT(decoded.diagnostic.reason ==
           PositionSharingUiDiagnosticReason::outbound_faulted);
    EXPECT(detail::position_sharing_ui_diagnostic_level(
               decoded.diagnostic) == LogLevel::error);
}

void test_logger_uses_fixed_message_and_typed_severity() {
    MemoryLogSink sink{};
    Logger<LogLevel::trace> logger{sink};
    const auto info = record_position_sharing_ui_result(
        logger, presented(UiNotice::position_sharing_stopped), 10);

    auto waiting = presented(
        UiNotice::position_sharing_waiting_for_fix,
        PositionSharingUiDisposition::refreshed,
        2);
    const auto warn =
        record_position_sharing_ui_result(logger, waiting, 20);

    auto contained = failed(
        PositionSharingUiError::external_refresh_failed);
    contained.present_error = PresentError::sink_failed;
    const auto error =
        record_position_sharing_ui_result(logger, contained, 30);

    EXPECT(info.accepted() && info.stored);
    EXPECT(warn.accepted() && warn.stored);
    EXPECT(error.accepted() && error.stored);
    EXPECT(sink.size() == 3);
    EXPECT(sink.at(0)->level == LogLevel::info);
    EXPECT(sink.at(1)->level == LogLevel::warn);
    EXPECT(sink.at(2)->level == LogLevel::error);
    for (std::size_t index = 0; index < sink.size(); ++index) {
        const auto* record = sink.at(index);
        EXPECT(record != nullptr);
        EXPECT(std::string_view(
                   record->component.data(), record->component_bytes) ==
               "position-ui");
        EXPECT(record->message_bytes ==
               kPositionSharingUiDiagnosticMessageBytes);
        EXPECT(std::string_view(
                   record->message.data(), 6) == "OTPD0=");
        EXPECT(!record->redacted);
        EXPECT(!record->truncated);
    }
}

void test_filtering_sink_rejection_and_idle_suppression_are_distinct() {
    MemoryLogSink sink{};
    Logger<LogLevel::trace> logger{sink};
    logger.set_runtime_level(LogLevel::error);
    const auto filtered = record_position_sharing_ui_result(
        logger, presented(UiNotice::position_sharing_stopped), 1);
    EXPECT(filtered.accepted());
    EXPECT(!filtered.stored);
    EXPECT(filtered.filtered);

    PositionSharingUiServiceResult idle{};
    idle.disposition = PositionSharingUiDisposition::idle;
    idle.action_error = ActionResolutionError::input_not_ready;
    const auto suppressed =
        record_position_sharing_ui_result(logger, idle, 2);
    EXPECT(suppressed.suppressed());
    EXPECT(!suppressed.stored);
    EXPECT(sink.empty());

    MemoryLogSink full_sink{};
    Logger<LogLevel::error> full_logger{full_sink};
    for (std::size_t index = 0; index < MemoryLogSink::kCapacity; ++index) {
        EXPECT(full_logger.log<LogLevel::error>(
            index, "test", "filler"));
    }
    auto contained = failed(PositionSharingUiError::revision_exhausted);
    const auto rejected =
        record_position_sharing_ui_result(full_logger, contained, 20);
    EXPECT(!rejected.accepted());
    EXPECT(rejected.sink_rejected);
    EXPECT(!rejected.filtered);
}

template <typename T, typename = void>
struct has_revision : std::false_type {};
template <typename T>
struct has_revision<
    T,
    std::void_t<decltype(std::declval<T>().revision)>> : std::true_type {};

template <typename T, typename = void>
struct has_timestamp : std::false_type {};
template <typename T>
struct has_timestamp<
    T,
    std::void_t<decltype(std::declval<T>().timestamp_ms)>> : std::true_type {};

template <typename T, typename = void>
struct has_identity : std::false_type {};
template <typename T>
struct has_identity<
    T,
    std::void_t<decltype(std::declval<T>().identity)>> : std::true_type {};

template <typename T, typename = void>
struct has_coordinates : std::false_type {};
template <typename T>
struct has_coordinates<
    T,
    std::void_t<decltype(std::declval<T>().coordinates)>>
    : std::true_type {};

void test_malformed_words_and_incoherent_results_fail_closed() {
    const auto good = encode_position_sharing_ui_diagnostic(
        presented(UiNotice::position_sharing_stopped)).word;
    EXPECT(decode_position_sharing_ui_diagnostic(
               good ^ 0x10000000U).error ==
           PositionSharingUiDiagnosticError::invalid_word);
    EXPECT(decode_position_sharing_ui_diagnostic(
               good | 0x01000000U).error ==
           PositionSharingUiDiagnosticError::unsupported_version);
    EXPECT(decode_position_sharing_ui_diagnostic(
               good | 0x00020000U).error ==
           PositionSharingUiDiagnosticError::invalid_word);
    EXPECT(decode_position_sharing_ui_diagnostic(
               (good & ~0x07U) | 0x07U).error ==
           PositionSharingUiDiagnosticError::invalid_word);
    EXPECT(decode_position_sharing_ui_diagnostic(
               good & ~(1U << 16U)).error ==
           PositionSharingUiDiagnosticError::invalid_word);
    EXPECT(decode_position_sharing_ui_diagnostic(
               good | (1U << 14U)).error ==
           PositionSharingUiDiagnosticError::invalid_word);

    auto invalid = presented(UiNotice::position_sharing_stopped);
    invalid.revision = 0;
    EXPECT(encode_position_sharing_ui_diagnostic(invalid).error ==
           PositionSharingUiDiagnosticError::invalid_result);
    invalid = {};
    invalid.disposition =
        static_cast<PositionSharingUiDisposition>(99);
    EXPECT(encode_position_sharing_ui_diagnostic(invalid).error ==
           PositionSharingUiDiagnosticError::invalid_result);

    static_assert(!has_revision<PositionSharingUiDiagnostic>::value);
    static_assert(!has_timestamp<PositionSharingUiDiagnostic>::value);
    static_assert(!has_identity<PositionSharingUiDiagnostic>::value);
    static_assert(!has_coordinates<PositionSharingUiDiagnostic>::value);
    static_assert(sizeof(PositionSharingUiDiagnostic) <= 8);
    static_assert(kPositionSharingUiDiagnosticMessageBytes == 14);
}

}  // namespace

int main() {
    test_exact_stopped_presentation_word_and_round_trip();
    test_visible_refresh_notices_map_without_runtime_detail();
    test_action_success_deferral_and_rejection_are_distinct();
    test_input_rejection_and_failure_are_coarse();
    test_display_and_presentation_failures_preserve_only_category();
    test_containment_reasons_are_explicit_and_identifier_free();
    test_outbound_fault_is_critical_even_when_frame_commits();
    test_logger_uses_fixed_message_and_typed_severity();
    test_filtering_sink_rejection_and_idle_suppression_are_distinct();
    test_malformed_words_and_incoherent_results_fail_closed();

    if (failures != 0) {
        std::cerr << failures
                  << " position sharing UI diagnostics assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 position sharing UI diagnostics scenario groups\n";
    return EXIT_SUCCESS;
}
