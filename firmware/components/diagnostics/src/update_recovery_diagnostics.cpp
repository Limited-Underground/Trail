#include "opentrail/update_recovery_diagnostics.hpp"

namespace opentrail::diagnostics {
namespace {

constexpr std::uint32_t kOperationShift = 0;
constexpr std::uint32_t kStateShift = 2;
constexpr std::uint32_t kReasonShift = 6;
constexpr std::uint32_t kActionShift = 11;
constexpr std::uint32_t kOperationSucceededShift = 14;
constexpr std::uint32_t kNormalOperationBlockedShift = 15;
constexpr std::uint32_t kAttentionRequiredShift = 16;
constexpr std::uint32_t kRebootRequiredShift = 17;
constexpr std::uint32_t kConfirmationRequiredShift = 18;
constexpr std::uint32_t kCleanupRequiredShift = 19;
constexpr std::uint32_t kSensitiveDetailRedactedShift = 20;
constexpr std::uint32_t kVersionShift = 24;
constexpr std::uint32_t kMagicShift = 28;

constexpr std::uint32_t kTwoBitMask = 0x03U;
constexpr std::uint32_t kThreeBitMask = 0x07U;
constexpr std::uint32_t kFourBitMask = 0x0FU;
constexpr std::uint32_t kFiveBitMask = 0x1FU;
constexpr std::uint32_t kReservedMask = 0x00E00000U;
constexpr std::uint32_t kUpdateRecoveryDiagnosticMagic = 0x0DU;

template <typename T>
constexpr std::uint32_t enum_value(T value) {
    return static_cast<std::uint32_t>(value);
}

bool known_operation(update::UpdateRecoveryStatusOperation value) {
    return enum_value(value) <=
           enum_value(update::UpdateRecoveryStatusOperation::transition);
}

bool known_state(update::UpdateRecoveryOperatorState value) {
    return enum_value(value) <= enum_value(
               update::UpdateRecoveryOperatorState::
                   reboot_reconcile_required);
}

bool known_reason(update::UpdateRecoveryOperatorReason value) {
    return enum_value(value) <= enum_value(
               update::UpdateRecoveryOperatorReason::confirmation_timeout);
}

bool known_action(update::UpdateRecoveryOperatorAction value) {
    return enum_value(value) <= enum_value(
               update::UpdateRecoveryOperatorAction::cleanup_update_state);
}

bool safe_mode_reason(update::UpdateRecoveryOperatorReason reason) {
    using Reason = update::UpdateRecoveryOperatorReason;
    switch (reason) {
        case Reason::live_state_invalid:
        case Reason::baseline_state_conflict:
        case Reason::recovery_missing:
        case Reason::rollback_detected:
        case Reason::generation_conflict:
        case Reason::checkpoint_rejected:
        case Reason::boot_observation_rejected:
        case Reason::rollback_observation_rejected:
            return true;
        default:
            return false;
    }
}

bool service_reason(update::UpdateRecoveryOperatorReason reason) {
    using Reason = update::UpdateRecoveryOperatorReason;
    switch (reason) {
        case Reason::invalid_result:
        case Reason::invalid_configuration:
        case Reason::trusted_state_unavailable:
        case Reason::trusted_state_invalid:
        case Reason::storage_unavailable:
        case Reason::generation_exhausted:
            return true;
        default:
            return false;
    }
}

bool reboot_reconciliation_reason(
    update::UpdateRecoveryOperatorReason reason) {
    using Reason = update::UpdateRecoveryOperatorReason;
    return reason == Reason::trusted_reconciliation_required ||
           reason == Reason::commit_uncertain ||
           reason == Reason::trust_update_failed;
}

bool state_flags_are_coherent(const UpdateRecoveryDiagnostic& diagnostic) {
    using Action = update::UpdateRecoveryOperatorAction;
    using State = update::UpdateRecoveryOperatorState;
    if (!diagnostic.sensitive_detail_redacted) {
        return false;
    }
    switch (diagnostic.state) {
        case State::operational:
            return diagnostic.operation_succeeded &&
                   !diagnostic.normal_operation_blocked &&
                   !diagnostic.attention_required &&
                   !diagnostic.reboot_required &&
                   !diagnostic.confirmation_required &&
                   !diagnostic.cleanup_required &&
                   diagnostic.action == Action::continue_operation;
        case State::trial_active:
            return diagnostic.operation_succeeded &&
                   !diagnostic.normal_operation_blocked &&
                   !diagnostic.attention_required &&
                   !diagnostic.reboot_required &&
                   diagnostic.confirmation_required &&
                   !diagnostic.cleanup_required &&
                   diagnostic.action == Action::continue_trial;
        case State::persistence_committed:
            return diagnostic.operation_succeeded &&
                   !diagnostic.normal_operation_blocked &&
                   !diagnostic.attention_required &&
                   !diagnostic.reboot_required &&
                   !diagnostic.confirmation_required &&
                   !diagnostic.cleanup_required &&
                   diagnostic.action == Action::none;
        case State::transition_rejected:
            return !diagnostic.operation_succeeded &&
                   !diagnostic.normal_operation_blocked &&
                   diagnostic.attention_required &&
                   !diagnostic.reboot_required &&
                   !diagnostic.confirmation_required &&
                   !diagnostic.cleanup_required &&
                   diagnostic.action == Action::none;
        case State::rollback_required:
            return diagnostic.operation_succeeded &&
                   diagnostic.normal_operation_blocked &&
                   diagnostic.attention_required &&
                   diagnostic.reboot_required &&
                   !diagnostic.confirmation_required &&
                   !diagnostic.cleanup_required &&
                   diagnostic.action == Action::reboot_to_baseline;
        case State::cleanup_required:
            return diagnostic.operation_succeeded &&
                   !diagnostic.normal_operation_blocked &&
                   diagnostic.attention_required &&
                   !diagnostic.reboot_required &&
                   !diagnostic.confirmation_required &&
                   diagnostic.cleanup_required &&
                   diagnostic.action == Action::cleanup_update_state;
        case State::safe_mode:
        case State::service_required:
            return !diagnostic.operation_succeeded &&
                   diagnostic.normal_operation_blocked &&
                   diagnostic.attention_required &&
                   !diagnostic.reboot_required &&
                   !diagnostic.confirmation_required &&
                   !diagnostic.cleanup_required &&
                   diagnostic.action == Action::service;
        case State::reboot_reconcile_required:
            return !diagnostic.operation_succeeded &&
                   diagnostic.normal_operation_blocked &&
                   diagnostic.attention_required &&
                   diagnostic.reboot_required &&
                   !diagnostic.confirmation_required &&
                   !diagnostic.cleanup_required &&
                   diagnostic.action == Action::reboot_and_reconcile;
    }
    return false;
}

bool operation_state_reason_are_coherent(
    const UpdateRecoveryDiagnostic& diagnostic) {
    using Operation = update::UpdateRecoveryStatusOperation;
    using Reason = update::UpdateRecoveryOperatorReason;
    using State = update::UpdateRecoveryOperatorState;

    if (diagnostic.state == State::safe_mode) {
        return safe_mode_reason(diagnostic.reason);
    }
    if (diagnostic.state == State::service_required) {
        return service_reason(diagnostic.reason);
    }
    if (diagnostic.state == State::reboot_reconcile_required) {
        return reboot_reconciliation_reason(diagnostic.reason);
    }

    switch (diagnostic.operation) {
        case Operation::boot:
            switch (diagnostic.state) {
                case State::operational:
                    return diagnostic.reason == Reason::clean_baseline;
                case State::trial_active:
                    return diagnostic.reason ==
                           Reason::trial_confirmation_required;
                case State::rollback_required:
                    return diagnostic.reason == Reason::boot_mismatch ||
                           diagnostic.reason == Reason::trial_boot_limit;
                case State::cleanup_required:
                    return diagnostic.reason == Reason::baseline_recovered ||
                           diagnostic.reason == Reason::cleanup_required;
                default:
                    return false;
            }
        case Operation::save:
            return diagnostic.state == State::persistence_committed &&
                   diagnostic.reason == Reason::none;
        case Operation::transition:
            switch (diagnostic.state) {
                case State::operational:
                    return diagnostic.reason ==
                           Reason::confirmation_committed;
                case State::trial_active:
                    return diagnostic.reason ==
                           Reason::trial_confirmation_required;
                case State::transition_rejected:
                    return diagnostic.reason == Reason::transition_rejected;
                case State::rollback_required:
                    return diagnostic.reason == Reason::explicit_rollback ||
                           diagnostic.reason == Reason::confirmation_timeout;
                default:
                    return false;
            }
    }
    return false;
}

bool coherent(const UpdateRecoveryDiagnostic& diagnostic) {
    return known_operation(diagnostic.operation) &&
           known_state(diagnostic.state) &&
           known_reason(diagnostic.reason) &&
           known_action(diagnostic.action) &&
           state_flags_are_coherent(diagnostic) &&
           operation_state_reason_are_coherent(diagnostic);
}

UpdateRecoveryDiagnostic diagnostic_from_status(
    const update::UpdateRecoveryStatus& status) {
    return {
        status.operation,
        status.state,
        status.reason,
        status.action,
        status.operation_succeeded,
        status.normal_operation_blocked,
        status.attention_required,
        status.reboot_required,
        status.confirmation_required,
        status.cleanup_required,
        true,
    };
}

}  // namespace

UpdateRecoveryDiagnosticEncodeResult encode_update_recovery_diagnostic(
    const update::UpdateRecoveryStatus& status) {
    const auto diagnostic = diagnostic_from_status(status);
    if (!coherent(diagnostic)) {
        return {};
    }

    const std::uint32_t word =
        (kUpdateRecoveryDiagnosticMagic << kMagicShift) |
        (static_cast<std::uint32_t>(kUpdateRecoveryDiagnosticVersion)
         << kVersionShift) |
        (enum_value(diagnostic.operation) << kOperationShift) |
        (enum_value(diagnostic.state) << kStateShift) |
        (enum_value(diagnostic.reason) << kReasonShift) |
        (enum_value(diagnostic.action) << kActionShift) |
        (static_cast<std::uint32_t>(diagnostic.operation_succeeded)
         << kOperationSucceededShift) |
        (static_cast<std::uint32_t>(diagnostic.normal_operation_blocked)
         << kNormalOperationBlockedShift) |
        (static_cast<std::uint32_t>(diagnostic.attention_required)
         << kAttentionRequiredShift) |
        (static_cast<std::uint32_t>(diagnostic.reboot_required)
         << kRebootRequiredShift) |
        (static_cast<std::uint32_t>(diagnostic.confirmation_required)
         << kConfirmationRequiredShift) |
        (static_cast<std::uint32_t>(diagnostic.cleanup_required)
         << kCleanupRequiredShift) |
        (static_cast<std::uint32_t>(diagnostic.sensitive_detail_redacted)
         << kSensitiveDetailRedactedShift);
    return {UpdateRecoveryDiagnosticError::none, word};
}

UpdateRecoveryDiagnosticDecodeResult decode_update_recovery_diagnostic(
    std::uint32_t word) {
    if (((word >> kMagicShift) & kFourBitMask) !=
        kUpdateRecoveryDiagnosticMagic || (word & kReservedMask) != 0) {
        return {};
    }
    const auto version =
        static_cast<std::uint8_t>((word >> kVersionShift) & kFourBitMask);
    if (version != kUpdateRecoveryDiagnosticVersion) {
        return {UpdateRecoveryDiagnosticError::unsupported_version};
    }

    UpdateRecoveryDiagnostic diagnostic{};
    diagnostic.operation =
        static_cast<update::UpdateRecoveryStatusOperation>(
            (word >> kOperationShift) & kTwoBitMask);
    diagnostic.state = static_cast<update::UpdateRecoveryOperatorState>(
        (word >> kStateShift) & kFourBitMask);
    diagnostic.reason = static_cast<update::UpdateRecoveryOperatorReason>(
        (word >> kReasonShift) & kFiveBitMask);
    diagnostic.action = static_cast<update::UpdateRecoveryOperatorAction>(
        (word >> kActionShift) & kThreeBitMask);
    diagnostic.operation_succeeded =
        ((word >> kOperationSucceededShift) & 1U) != 0;
    diagnostic.normal_operation_blocked =
        ((word >> kNormalOperationBlockedShift) & 1U) != 0;
    diagnostic.attention_required =
        ((word >> kAttentionRequiredShift) & 1U) != 0;
    diagnostic.reboot_required =
        ((word >> kRebootRequiredShift) & 1U) != 0;
    diagnostic.confirmation_required =
        ((word >> kConfirmationRequiredShift) & 1U) != 0;
    diagnostic.cleanup_required =
        ((word >> kCleanupRequiredShift) & 1U) != 0;
    diagnostic.sensitive_detail_redacted =
        ((word >> kSensitiveDetailRedactedShift) & 1U) != 0;
    if (!coherent(diagnostic)) {
        return {};
    }
    return {UpdateRecoveryDiagnosticError::none, diagnostic};
}

UpdateRecoveryDiagnosticMessageResult
parse_update_recovery_diagnostic_message(std::string_view message) {
    constexpr std::string_view prefix{"OTRD0="};
    if (message.size() != kUpdateRecoveryDiagnosticMessageBytes ||
        message.substr(0, prefix.size()) != prefix) {
        return {};
    }

    std::uint32_t word = 0;
    for (std::size_t index = prefix.size(); index < message.size(); ++index) {
        const char character = message[index];
        std::uint32_t nibble = 0;
        if (character >= '0' && character <= '9') {
            nibble = static_cast<std::uint32_t>(character - '0');
        } else if (character >= 'A' && character <= 'F') {
            nibble = static_cast<std::uint32_t>(character - 'A' + 10);
        } else {
            return {};
        }
        word = (word << 4U) | nibble;
    }

    const auto decoded = decode_update_recovery_diagnostic(word);
    if (!decoded.decoded()) {
        return {decoded.error, word};
    }
    return {
        UpdateRecoveryDiagnosticError::none,
        word,
        decoded.diagnostic,
    };
}

std::string_view update_recovery_diagnostic_error_name(
    UpdateRecoveryDiagnosticError error) {
    using Error = UpdateRecoveryDiagnosticError;
    switch (error) {
        case Error::none:
            return "none";
        case Error::invalid_status:
            return "invalid_status";
        case Error::invalid_word:
            return "invalid_word";
        case Error::unsupported_version:
            return "unsupported_version";
        case Error::invalid_message:
            return "invalid_message";
    }
    return "unknown";
}

std::string_view update_recovery_diagnostic_operation_name(
    update::UpdateRecoveryStatusOperation operation) {
    using Operation = update::UpdateRecoveryStatusOperation;
    switch (operation) {
        case Operation::boot:
            return "boot";
        case Operation::save:
            return "save";
        case Operation::transition:
            return "transition";
    }
    return "unknown";
}

std::string_view update_recovery_diagnostic_state_name(
    update::UpdateRecoveryOperatorState state) {
    using State = update::UpdateRecoveryOperatorState;
    switch (state) {
        case State::service_required:
            return "service_required";
        case State::operational:
            return "operational";
        case State::trial_active:
            return "trial_active";
        case State::persistence_committed:
            return "persistence_committed";
        case State::transition_rejected:
            return "transition_rejected";
        case State::rollback_required:
            return "rollback_required";
        case State::cleanup_required:
            return "cleanup_required";
        case State::safe_mode:
            return "safe_mode";
        case State::reboot_reconcile_required:
            return "reboot_reconcile_required";
    }
    return "unknown";
}

std::string_view update_recovery_diagnostic_reason_name(
    update::UpdateRecoveryOperatorReason reason) {
    using Reason = update::UpdateRecoveryOperatorReason;
    switch (reason) {
        case Reason::invalid_result:
            return "invalid_result";
        case Reason::none:
            return "none";
        case Reason::clean_baseline:
            return "clean_baseline";
        case Reason::trial_confirmation_required:
            return "trial_confirmation_required";
        case Reason::baseline_recovered:
            return "baseline_recovered";
        case Reason::cleanup_required:
            return "cleanup_required";
        case Reason::invalid_configuration:
            return "invalid_configuration";
        case Reason::live_state_invalid:
            return "live_state_invalid";
        case Reason::trusted_state_unavailable:
            return "trusted_state_unavailable";
        case Reason::trusted_state_invalid:
            return "trusted_state_invalid";
        case Reason::baseline_state_conflict:
            return "baseline_state_conflict";
        case Reason::recovery_missing:
            return "recovery_missing";
        case Reason::storage_unavailable:
            return "storage_unavailable";
        case Reason::rollback_detected:
            return "rollback_detected";
        case Reason::generation_conflict:
            return "generation_conflict";
        case Reason::generation_exhausted:
            return "generation_exhausted";
        case Reason::checkpoint_rejected:
            return "checkpoint_rejected";
        case Reason::boot_observation_rejected:
            return "boot_observation_rejected";
        case Reason::rollback_observation_rejected:
            return "rollback_observation_rejected";
        case Reason::boot_mismatch:
            return "boot_mismatch";
        case Reason::trial_boot_limit:
            return "trial_boot_limit";
        case Reason::trusted_reconciliation_required:
            return "trusted_reconciliation_required";
        case Reason::commit_uncertain:
            return "commit_uncertain";
        case Reason::trust_update_failed:
            return "trust_update_failed";
        case Reason::transition_rejected:
            return "transition_rejected";
        case Reason::confirmation_committed:
            return "confirmation_committed";
        case Reason::explicit_rollback:
            return "explicit_rollback";
        case Reason::confirmation_timeout:
            return "confirmation_timeout";
    }
    return "unknown";
}

std::string_view update_recovery_diagnostic_action_name(
    update::UpdateRecoveryOperatorAction action) {
    using Action = update::UpdateRecoveryOperatorAction;
    switch (action) {
        case Action::service:
            return "service";
        case Action::none:
            return "none";
        case Action::continue_operation:
            return "continue_operation";
        case Action::continue_trial:
            return "continue_trial";
        case Action::reboot_to_baseline:
            return "reboot_to_baseline";
        case Action::reboot_and_reconcile:
            return "reboot_and_reconcile";
        case Action::cleanup_update_state:
            return "cleanup_update_state";
    }
    return "unknown";
}

namespace detail {

LogLevel update_recovery_diagnostic_level(
    update::UpdateRecoveryOperatorState state) {
    using State = update::UpdateRecoveryOperatorState;
    switch (state) {
        case State::operational:
        case State::persistence_committed:
            return LogLevel::info;
        case State::trial_active:
        case State::transition_rejected:
        case State::rollback_required:
        case State::cleanup_required:
        case State::reboot_reconcile_required:
            return LogLevel::warn;
        case State::safe_mode:
        case State::service_required:
            return LogLevel::error;
    }
    return LogLevel::error;
}

std::array<char, kUpdateRecoveryDiagnosticMessageBytes + 1>
format_update_recovery_diagnostic(std::uint32_t word) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::array<char, kUpdateRecoveryDiagnosticMessageBytes + 1> message{
        'O', 'T', 'R', 'D', '0', '='};
    for (std::size_t index = 0; index < 8; ++index) {
        const auto shift = static_cast<std::uint32_t>((7 - index) * 4);
        message[6 + index] = digits[(word >> shift) & 0x0FU];
    }
    message[kUpdateRecoveryDiagnosticMessageBytes] = '\0';
    return message;
}

}  // namespace detail

}  // namespace opentrail::diagnostics
