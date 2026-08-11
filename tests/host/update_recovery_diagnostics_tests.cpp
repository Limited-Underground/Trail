#include <cstdlib>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <utility>

#include "memory_log_sink.hpp"
#include "opentrail/update_recovery_diagnostics.hpp"

namespace {

using namespace opentrail::diagnostics;
using namespace opentrail::diagnostics::test_support;
using namespace opentrail::update;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

template <typename T, typename = void>
struct has_observed_generation : std::false_type {};
template <typename T>
struct has_observed_generation<
    T,
    std::void_t<decltype(std::declval<T>().observed_generation)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_trusted_generation : std::false_type {};
template <typename T>
struct has_trusted_generation<
    T,
    std::void_t<decltype(std::declval<T>().trusted_generation)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_hardware_id : std::false_type {};
template <typename T>
struct has_hardware_id<
    T,
    std::void_t<decltype(std::declval<T>().hardware_id)>> : std::true_type {};

template <typename T, typename = void>
struct has_candidate : std::false_type {};
template <typename T>
struct has_candidate<
    T,
    std::void_t<decltype(std::declval<T>().candidate)>> : std::true_type {};

template <typename T, typename = void>
struct has_checkpoint : std::false_type {};
template <typename T>
struct has_checkpoint<
    T,
    std::void_t<decltype(std::declval<T>().checkpoint)>> : std::true_type {};

template <typename T, typename = void>
struct has_key_handle : std::false_type {};
template <typename T>
struct has_key_handle<
    T,
    std::void_t<decltype(std::declval<T>().key_handle)>> : std::true_type {};

template <typename T, typename = void>
struct has_address : std::false_type {};
template <typename T>
struct has_address<
    T,
    std::void_t<decltype(std::declval<T>().address)>> : std::true_type {};

static_assert(!has_observed_generation<UpdateRecoveryDiagnostic>::value);
static_assert(!has_trusted_generation<UpdateRecoveryDiagnostic>::value);
static_assert(!has_hardware_id<UpdateRecoveryDiagnostic>::value);
static_assert(!has_candidate<UpdateRecoveryDiagnostic>::value);
static_assert(!has_checkpoint<UpdateRecoveryDiagnostic>::value);
static_assert(!has_key_handle<UpdateRecoveryDiagnostic>::value);
static_assert(!has_address<UpdateRecoveryDiagnostic>::value);

UpdateRecoveryStatus baseline_status() {
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
    auto status = baseline_status();
    status.state = UpdateRecoveryOperatorState::trial_active;
    status.reason =
        UpdateRecoveryOperatorReason::trial_confirmation_required;
    status.action = UpdateRecoveryOperatorAction::continue_trial;
    status.confirmation_required = true;
    return status;
}

UpdateRecoveryStatus committed_save_status() {
    auto status = baseline_status();
    status.operation = UpdateRecoveryStatusOperation::save;
    status.state = UpdateRecoveryOperatorState::persistence_committed;
    status.reason = UpdateRecoveryOperatorReason::none;
    status.action = UpdateRecoveryOperatorAction::none;
    return status;
}

UpdateRecoveryStatus rejected_transition_status() {
    UpdateRecoveryStatus status{};
    status.operation = UpdateRecoveryStatusOperation::transition;
    status.state = UpdateRecoveryOperatorState::transition_rejected;
    status.reason = UpdateRecoveryOperatorReason::transition_rejected;
    status.action = UpdateRecoveryOperatorAction::none;
    status.normal_operation_blocked = false;
    return status;
}

UpdateRecoveryStatus service_status() {
    return UpdateRecoveryStatus{};
}

void test_exact_baseline_word_format_and_round_trip() {
    const auto encoded =
        encode_update_recovery_diagnostic(baseline_status());
    EXPECT(encoded.encoded());
    EXPECT(encoded.word == 0xD0105084U);

    const auto decoded = decode_update_recovery_diagnostic(encoded.word);
    EXPECT(decoded.decoded());
    EXPECT(decoded.diagnostic.operation ==
           UpdateRecoveryStatusOperation::boot);
    EXPECT(decoded.diagnostic.state ==
           UpdateRecoveryOperatorState::operational);
    EXPECT(decoded.diagnostic.reason ==
           UpdateRecoveryOperatorReason::clean_baseline);
    EXPECT(decoded.diagnostic.action ==
           UpdateRecoveryOperatorAction::continue_operation);
    EXPECT(decoded.diagnostic.operation_succeeded);
    EXPECT(decoded.diagnostic.sensitive_detail_redacted);

    const auto message =
        detail::format_update_recovery_diagnostic(encoded.word);
    EXPECT(std::string_view(
               message.data(), kUpdateRecoveryDiagnosticMessageBytes) ==
           "OTRD0=D0105084");
}

void test_normal_operation_shapes_round_trip() {
    const auto trial = decode_update_recovery_diagnostic(
        encode_update_recovery_diagnostic(trial_status()).word);
    EXPECT(trial.decoded());
    EXPECT(trial.diagnostic.state ==
           UpdateRecoveryOperatorState::trial_active);
    EXPECT(trial.diagnostic.confirmation_required);

    const auto save = decode_update_recovery_diagnostic(
        encode_update_recovery_diagnostic(committed_save_status()).word);
    EXPECT(save.decoded());
    EXPECT(save.diagnostic.operation ==
           UpdateRecoveryStatusOperation::save);
    EXPECT(save.diagnostic.state ==
           UpdateRecoveryOperatorState::persistence_committed);

    const auto rejected = decode_update_recovery_diagnostic(
        encode_update_recovery_diagnostic(
            rejected_transition_status()).word);
    EXPECT(rejected.decoded());
    EXPECT(rejected.diagnostic.operation ==
           UpdateRecoveryStatusOperation::transition);
    EXPECT(!rejected.diagnostic.operation_succeeded);
    EXPECT(!rejected.diagnostic.normal_operation_blocked);
}

void test_recovery_action_shapes_round_trip() {
    UpdateRecoveryStatus rollback{};
    rollback.operation = UpdateRecoveryStatusOperation::boot;
    rollback.state = UpdateRecoveryOperatorState::rollback_required;
    rollback.reason = UpdateRecoveryOperatorReason::boot_mismatch;
    rollback.action = UpdateRecoveryOperatorAction::reboot_to_baseline;
    rollback.operation_succeeded = true;
    rollback.reboot_required = true;
    const auto rollback_decoded = decode_update_recovery_diagnostic(
        encode_update_recovery_diagnostic(rollback).word);
    EXPECT(rollback_decoded.decoded());
    EXPECT(rollback_decoded.diagnostic.reboot_required);

    auto cleanup = rollback;
    cleanup.state = UpdateRecoveryOperatorState::cleanup_required;
    cleanup.reason = UpdateRecoveryOperatorReason::baseline_recovered;
    cleanup.action = UpdateRecoveryOperatorAction::cleanup_update_state;
    cleanup.normal_operation_blocked = false;
    cleanup.reboot_required = false;
    cleanup.cleanup_required = true;
    const auto cleanup_decoded = decode_update_recovery_diagnostic(
        encode_update_recovery_diagnostic(cleanup).word);
    EXPECT(cleanup_decoded.decoded());
    EXPECT(cleanup_decoded.diagnostic.cleanup_required);

    UpdateRecoveryStatus reconcile{};
    reconcile.operation = UpdateRecoveryStatusOperation::save;
    reconcile.state =
        UpdateRecoveryOperatorState::reboot_reconcile_required;
    reconcile.reason = UpdateRecoveryOperatorReason::commit_uncertain;
    reconcile.action =
        UpdateRecoveryOperatorAction::reboot_and_reconcile;
    reconcile.reboot_required = true;
    const auto reconcile_decoded = decode_update_recovery_diagnostic(
        encode_update_recovery_diagnostic(reconcile).word);
    EXPECT(reconcile_decoded.decoded());
    EXPECT(reconcile_decoded.diagnostic.reboot_required);

    UpdateRecoveryStatus safe{};
    safe.state = UpdateRecoveryOperatorState::safe_mode;
    safe.reason = UpdateRecoveryOperatorReason::rollback_detected;
    const auto safe_decoded = decode_update_recovery_diagnostic(
        encode_update_recovery_diagnostic(safe).word);
    EXPECT(safe_decoded.decoded());
    EXPECT(safe_decoded.diagnostic.normal_operation_blocked);

    const auto service_decoded = decode_update_recovery_diagnostic(
        encode_update_recovery_diagnostic(service_status()).word);
    EXPECT(service_decoded.decoded());
    EXPECT(service_decoded.diagnostic.reason ==
           UpdateRecoveryOperatorReason::invalid_result);
}

void test_records_one_canonical_message_at_state_severity() {
    MemoryLogSink sink{};
    Logger<LogLevel::trace> logger{sink};
    const auto baseline =
        record_update_recovery_status(logger, baseline_status(), 1);
    const auto trial =
        record_update_recovery_status(logger, trial_status(), 2);
    const auto service =
        record_update_recovery_status(logger, service_status(), 3);
    EXPECT(baseline.accepted() && baseline.stored);
    EXPECT(trial.accepted() && trial.stored);
    EXPECT(service.accepted() && service.stored);
    EXPECT(sink.size() == 3);

    EXPECT(sink.at(0)->level == LogLevel::info);
    EXPECT(sink.at(1)->level == LogLevel::warn);
    EXPECT(sink.at(2)->level == LogLevel::error);
    for (std::size_t index = 0; index < sink.size(); ++index) {
        const auto* record = sink.at(index);
        EXPECT(record != nullptr);
        EXPECT(std::string_view(
                   record->component.data(), record->component_bytes) ==
               "update-recovery");
        EXPECT(record->message_bytes ==
               kUpdateRecoveryDiagnosticMessageBytes);
        EXPECT(!record->redacted);
        EXPECT(!record->truncated);
    }
    EXPECT(std::string_view(
               sink.at(0)->message.data(), sink.at(0)->message_bytes) ==
           "OTRD0=D0105084");
}

void test_filtering_and_sink_rejection_remain_distinct() {
    MemoryLogSink filtered_sink{};
    Logger<LogLevel::trace> filtered_logger{filtered_sink};
    filtered_logger.set_runtime_level(LogLevel::error);
    const auto filtered = record_update_recovery_status(
        filtered_logger, baseline_status(), 1);
    EXPECT(filtered.accepted());
    EXPECT(!filtered.stored);
    EXPECT(filtered.filtered);
    EXPECT(!filtered.sink_rejected);
    EXPECT(filtered_sink.empty());

    const auto stored = record_update_recovery_status(
        filtered_logger, service_status(), 2);
    EXPECT(stored.accepted() && stored.stored);

    MemoryLogSink full_sink{};
    Logger<LogLevel::error> full_logger{full_sink};
    for (std::size_t index = 0; index < MemoryLogSink::kCapacity; ++index) {
        EXPECT(full_logger.log<LogLevel::error>(
            index, "test", "filler"));
    }
    const auto rejected = record_update_recovery_status(
        full_logger, service_status(), 20);
    EXPECT(!rejected.accepted());
    EXPECT(!rejected.stored);
    EXPECT(!rejected.filtered);
    EXPECT(rejected.sink_rejected);
}

void test_incoherent_or_unknown_status_is_rejected_before_logging() {
    MemoryLogSink sink{};
    Logger<LogLevel::trace> logger{sink};

    auto invalid = baseline_status();
    invalid.normal_operation_blocked = true;
    EXPECT(encode_update_recovery_diagnostic(invalid).error ==
           UpdateRecoveryDiagnosticError::invalid_status);
    EXPECT(record_update_recovery_status(logger, invalid, 1).error ==
           UpdateRecoveryDiagnosticError::invalid_status);

    invalid = baseline_status();
    invalid.reason = static_cast<UpdateRecoveryOperatorReason>(99);
    EXPECT(record_update_recovery_status(logger, invalid, 1).error ==
           UpdateRecoveryDiagnosticError::invalid_status);

    invalid = service_status();
    invalid.reason = UpdateRecoveryOperatorReason::clean_baseline;
    EXPECT(record_update_recovery_status(logger, invalid, 1).error ==
           UpdateRecoveryDiagnosticError::invalid_status);

    invalid = service_status();
    invalid.state =
        UpdateRecoveryOperatorState::reboot_reconcile_required;
    invalid.reason = UpdateRecoveryOperatorReason::commit_uncertain;
    invalid.action =
        UpdateRecoveryOperatorAction::reboot_and_reconcile;
    EXPECT(record_update_recovery_status(logger, invalid, 1).error ==
           UpdateRecoveryDiagnosticError::invalid_status);
    EXPECT(sink.empty());
}

void test_malformed_words_fail_closed() {
    const auto good =
        encode_update_recovery_diagnostic(baseline_status()).word;
    EXPECT(decode_update_recovery_diagnostic(good ^ 0x10000000U).error ==
           UpdateRecoveryDiagnosticError::invalid_word);
    EXPECT(decode_update_recovery_diagnostic(good | 0x01000000U).error ==
           UpdateRecoveryDiagnosticError::unsupported_version);
    EXPECT(decode_update_recovery_diagnostic(good | 0x00200000U).error ==
           UpdateRecoveryDiagnosticError::invalid_word);
    EXPECT(decode_update_recovery_diagnostic(
               (good & ~(0x0FU << 2U)) | (0x0FU << 2U)).error ==
           UpdateRecoveryDiagnosticError::invalid_word);
    EXPECT(decode_update_recovery_diagnostic(good ^ (1U << 15U)).error ==
           UpdateRecoveryDiagnosticError::invalid_word);
    EXPECT(decode_update_recovery_diagnostic(good ^ (1U << 20U)).error ==
           UpdateRecoveryDiagnosticError::invalid_word);
}

void test_payload_is_fixed_and_identifier_free() {
    static_assert(std::is_trivially_copyable_v<UpdateRecoveryDiagnostic>);
    EXPECT(sizeof(UpdateRecoveryDiagnostic) <= 16);
    EXPECT(sizeof(UpdateRecoveryDiagnosticEncodeResult) <= 8);
    EXPECT(kUpdateRecoveryDiagnosticMessageBytes == 14);
}

}  // namespace

int main() {
    test_exact_baseline_word_format_and_round_trip();
    test_normal_operation_shapes_round_trip();
    test_recovery_action_shapes_round_trip();
    test_records_one_canonical_message_at_state_severity();
    test_filtering_and_sink_rejection_remain_distinct();
    test_incoherent_or_unknown_status_is_rejected_before_logging();
    test_malformed_words_fail_closed();
    test_payload_is_fixed_and_identifier_free();

    if (failures != 0) {
        std::cerr << failures <<
            " update recovery diagnostics assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 redacted update recovery diagnostics groups\n";
    return EXIT_SUCCESS;
}
