#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "opentrail/logger.hpp"
#include "opentrail/update_recovery_status.hpp"

namespace opentrail::diagnostics {

inline constexpr std::uint8_t kUpdateRecoveryDiagnosticVersion = 0;
inline constexpr std::size_t kUpdateRecoveryDiagnosticMessageBytes = 14;

enum class UpdateRecoveryDiagnosticError : std::uint8_t {
    none = 0,
    invalid_status,
    invalid_word,
    unsupported_version,
};

// Fixed decoded view of one OTRD0 word. Generation values and all source
// coordinator detail are deliberately absent.
struct UpdateRecoveryDiagnostic {
    update::UpdateRecoveryStatusOperation operation{
        update::UpdateRecoveryStatusOperation::boot};
    update::UpdateRecoveryOperatorState state{
        update::UpdateRecoveryOperatorState::service_required};
    update::UpdateRecoveryOperatorReason reason{
        update::UpdateRecoveryOperatorReason::invalid_result};
    update::UpdateRecoveryOperatorAction action{
        update::UpdateRecoveryOperatorAction::service};
    bool operation_succeeded{false};
    bool normal_operation_blocked{true};
    bool attention_required{true};
    bool reboot_required{false};
    bool confirmation_required{false};
    bool cleanup_required{false};
    bool sensitive_detail_redacted{true};
};

struct UpdateRecoveryDiagnosticEncodeResult {
    UpdateRecoveryDiagnosticError error{
        UpdateRecoveryDiagnosticError::invalid_status};
    std::uint32_t word{0};

    [[nodiscard]] constexpr bool encoded() const {
        return error == UpdateRecoveryDiagnosticError::none;
    }
};

struct UpdateRecoveryDiagnosticDecodeResult {
    UpdateRecoveryDiagnosticError error{
        UpdateRecoveryDiagnosticError::invalid_word};
    UpdateRecoveryDiagnostic diagnostic{};

    [[nodiscard]] constexpr bool decoded() const {
        return error == UpdateRecoveryDiagnosticError::none;
    }
};

struct UpdateRecoveryDiagnosticRecordResult {
    UpdateRecoveryDiagnosticError error{
        UpdateRecoveryDiagnosticError::invalid_status};
    std::uint32_t word{0};
    bool stored{false};
    bool filtered{false};
    bool sink_rejected{false};

    [[nodiscard]] constexpr bool accepted() const {
        return error == UpdateRecoveryDiagnosticError::none &&
               !sink_rejected;
    }
};

static_assert(std::is_trivially_copyable_v<UpdateRecoveryDiagnostic>);
static_assert(sizeof(UpdateRecoveryDiagnostic) <= 16);

// OTRD0 is one versioned 32-bit coarse outcome. It omits both generation
// values, identities, policy, checkpoints, and raw backend/guard errors.
[[nodiscard]] UpdateRecoveryDiagnosticEncodeResult
encode_update_recovery_diagnostic(
    const update::UpdateRecoveryStatus& status);

[[nodiscard]] UpdateRecoveryDiagnosticDecodeResult
decode_update_recovery_diagnostic(std::uint32_t word);

namespace detail {

[[nodiscard]] LogLevel update_recovery_diagnostic_level(
    update::UpdateRecoveryOperatorState state);

[[nodiscard]] std::array<
    char,
    kUpdateRecoveryDiagnosticMessageBytes + 1>
format_update_recovery_diagnostic(std::uint32_t word);

}  // namespace detail

// Records exactly one fixed public message, "OTRD0=XXXXXXXX", through the
// existing Logger authority. Normal logger filtering is an accepted non-write;
// sink rejection remains visible and is not reported as accepted.
template <LogLevel CompiledLevel>
[[nodiscard]] UpdateRecoveryDiagnosticRecordResult
record_update_recovery_status(
    Logger<CompiledLevel>& logger,
    const update::UpdateRecoveryStatus& status,
    std::uint64_t now_ms) {
    const auto encoded = encode_update_recovery_diagnostic(status);
    if (!encoded.encoded()) {
        return {};
    }

    const auto message =
        detail::format_update_recovery_diagnostic(encoded.word);
    const std::string_view view{
        message.data(), kUpdateRecoveryDiagnosticMessageBytes};
    const auto before = logger.status();
    switch (detail::update_recovery_diagnostic_level(status.state)) {
        case LogLevel::info:
            logger.template log<LogLevel::info>(
                now_ms, "update-recovery", view, LogPrivacy::public_data);
            break;
        case LogLevel::warn:
            logger.template log<LogLevel::warn>(
                now_ms, "update-recovery", view, LogPrivacy::public_data);
            break;
        case LogLevel::error:
        default:
            logger.template log<LogLevel::error>(
                now_ms, "update-recovery", view, LogPrivacy::public_data);
            break;
    }
    const auto after = logger.status();
    return {
        UpdateRecoveryDiagnosticError::none,
        encoded.word,
        after.emitted != before.emitted,
        after.filtered != before.filtered,
        after.sink_dropped != before.sink_dropped,
    };
}

}  // namespace opentrail::diagnostics
