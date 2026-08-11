#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "opentrail/logger.hpp"
#include "opentrail/position_sharing_ui_coordinator.hpp"

namespace opentrail::diagnostics {

inline constexpr std::uint8_t kPositionSharingUiDiagnosticVersion = 0;
inline constexpr std::size_t kPositionSharingUiDiagnosticMessageBytes = 14;

enum class PositionSharingUiDiagnosticError : std::uint8_t {
    none = 0,
    no_event,
    invalid_result,
    invalid_word,
    unsupported_version,
    invalid_message,
};

enum class PositionSharingUiDiagnosticEvent : std::uint8_t {
    presentation = 0,
    state_refresh,
    action,
    input,
    failure,
};

enum class PositionSharingUiDiagnosticOutcome : std::uint8_t {
    succeeded = 0,
    deferred,
    rejected,
    contained,
    failed,
};

enum class PositionSharingUiDiagnosticNotice : std::uint8_t {
    none = 0,
    stopped,
    active,
    waiting_for_fix,
    deferred,
    failed,
};

enum class PositionSharingUiDiagnosticReason : std::uint8_t {
    none = 0,
    clock_not_ready,
    outbound_faulted,
    stale_input,
    invalid_input,
    input_source_failed,
    display_not_ready,
    display_failed,
    revision_exhausted,
    presentation_unavailable,
    command_rejected,
    invalid_initial_revision,
    refresh_contained,
};

// Fixed decoded view of one OTPD0 word. Revisions, timestamps, scheduler
// counters, coordinates, identities, packets, and free text are absent.
struct PositionSharingUiDiagnostic {
    PositionSharingUiDiagnosticEvent event{
        PositionSharingUiDiagnosticEvent::failure};
    PositionSharingUiDiagnosticOutcome outcome{
        PositionSharingUiDiagnosticOutcome::failed};
    PositionSharingUiDiagnosticNotice notice{
        PositionSharingUiDiagnosticNotice::none};
    PositionSharingUiDiagnosticReason reason{
        PositionSharingUiDiagnosticReason::presentation_unavailable};
    bool frame_presented{false};
    bool state_changed{false};
    bool sharing_contained{false};
    bool sensitive_detail_redacted{true};
};

struct PositionSharingUiDiagnosticEncodeResult {
    PositionSharingUiDiagnosticError error{
        PositionSharingUiDiagnosticError::invalid_result};
    std::uint32_t word{0};

    [[nodiscard]] constexpr bool encoded() const {
        return error == PositionSharingUiDiagnosticError::none;
    }

    [[nodiscard]] constexpr bool suppressed() const {
        return error == PositionSharingUiDiagnosticError::no_event;
    }
};

struct PositionSharingUiDiagnosticDecodeResult {
    PositionSharingUiDiagnosticError error{
        PositionSharingUiDiagnosticError::invalid_word};
    PositionSharingUiDiagnostic diagnostic{};

    [[nodiscard]] constexpr bool decoded() const {
        return error == PositionSharingUiDiagnosticError::none;
    }
};

struct PositionSharingUiDiagnosticMessageResult {
    PositionSharingUiDiagnosticError error{
        PositionSharingUiDiagnosticError::invalid_message};
    std::uint32_t word{0};
    PositionSharingUiDiagnostic diagnostic{};

    [[nodiscard]] constexpr bool parsed() const {
        return error == PositionSharingUiDiagnosticError::none;
    }
};

struct PositionSharingUiDiagnosticRecordResult {
    PositionSharingUiDiagnosticError error{
        PositionSharingUiDiagnosticError::invalid_result};
    std::uint32_t word{0};
    bool stored{false};
    bool filtered{false};
    bool sink_rejected{false};

    [[nodiscard]] constexpr bool accepted() const {
        return error == PositionSharingUiDiagnosticError::none &&
               !sink_rejected;
    }

    [[nodiscard]] constexpr bool suppressed() const {
        return error == PositionSharingUiDiagnosticError::no_event;
    }
};

static_assert(std::is_trivially_copyable_v<PositionSharingUiDiagnostic>);
static_assert(sizeof(PositionSharingUiDiagnostic) <= 8);

// OTPD0 is one versioned 32-bit coarse result. Idle polls deliberately produce
// no event so a normal target loop cannot fill a bounded log with no-op work.
[[nodiscard]] PositionSharingUiDiagnosticEncodeResult
encode_position_sharing_ui_diagnostic(
    const integration::PositionSharingUiServiceResult& result);

[[nodiscard]] PositionSharingUiDiagnosticDecodeResult
decode_position_sharing_ui_diagnostic(std::uint32_t word);

// Accepts only the canonical public logger message: "OTPD0=" followed by
// exactly eight uppercase hexadecimal digits. The decoded event is validated
// with the same fail-closed rules as a binary word.
[[nodiscard]] PositionSharingUiDiagnosticMessageResult
parse_position_sharing_ui_diagnostic_message(std::string_view message);

// Stable operator-facing category names. Unknown enum values are rendered as
// "unknown" for defensive display; the strict parser never returns them.
[[nodiscard]] std::string_view position_sharing_ui_diagnostic_error_name(
    PositionSharingUiDiagnosticError error);
[[nodiscard]] std::string_view position_sharing_ui_diagnostic_event_name(
    PositionSharingUiDiagnosticEvent event);
[[nodiscard]] std::string_view position_sharing_ui_diagnostic_outcome_name(
    PositionSharingUiDiagnosticOutcome outcome);
[[nodiscard]] std::string_view position_sharing_ui_diagnostic_notice_name(
    PositionSharingUiDiagnosticNotice notice);
[[nodiscard]] std::string_view position_sharing_ui_diagnostic_reason_name(
    PositionSharingUiDiagnosticReason reason);

namespace detail {

[[nodiscard]] LogLevel position_sharing_ui_diagnostic_level(
    const PositionSharingUiDiagnostic& diagnostic);

[[nodiscard]] std::array<
    char,
    kPositionSharingUiDiagnosticMessageBytes + 1>
format_position_sharing_ui_diagnostic(std::uint32_t word);

}  // namespace detail

// Records exactly one fixed public message, "OTPD0=XXXXXXXX", through the
// existing Logger authority. Filtering is an accepted non-write; sink
// rejection remains visible. Idle service is explicitly suppressed.
template <LogLevel CompiledLevel>
[[nodiscard]] PositionSharingUiDiagnosticRecordResult
record_position_sharing_ui_result(
    Logger<CompiledLevel>& logger,
    const integration::PositionSharingUiServiceResult& result,
    std::uint64_t now_ms) {
    const auto encoded = encode_position_sharing_ui_diagnostic(result);
    if (!encoded.encoded()) {
        return {encoded.error};
    }

    const auto decoded = decode_position_sharing_ui_diagnostic(encoded.word);
    if (!decoded.decoded()) {
        return {PositionSharingUiDiagnosticError::invalid_result};
    }
    const auto message =
        detail::format_position_sharing_ui_diagnostic(encoded.word);
    const std::string_view view{
        message.data(), kPositionSharingUiDiagnosticMessageBytes};
    const auto before = logger.status();
    switch (detail::position_sharing_ui_diagnostic_level(
        decoded.diagnostic)) {
        case LogLevel::info:
            logger.template log<LogLevel::info>(
                now_ms, "position-ui", view, LogPrivacy::public_data);
            break;
        case LogLevel::warn:
            logger.template log<LogLevel::warn>(
                now_ms, "position-ui", view, LogPrivacy::public_data);
            break;
        case LogLevel::error:
        default:
            logger.template log<LogLevel::error>(
                now_ms, "position-ui", view, LogPrivacy::public_data);
            break;
    }
    const auto after = logger.status();
    return {
        PositionSharingUiDiagnosticError::none,
        encoded.word,
        after.emitted != before.emitted,
        after.filtered != before.filtered,
        after.sink_dropped != before.sink_dropped,
    };
}

}  // namespace opentrail::diagnostics
