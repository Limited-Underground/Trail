#include "opentrail/position_sharing_ui_diagnostics.hpp"

namespace opentrail::diagnostics {
namespace {

constexpr std::uint32_t kEventShift = 0;
constexpr std::uint32_t kOutcomeShift = 3;
constexpr std::uint32_t kNoticeShift = 6;
constexpr std::uint32_t kReasonShift = 9;
constexpr std::uint32_t kFramePresentedShift = 13;
constexpr std::uint32_t kStateChangedShift = 14;
constexpr std::uint32_t kSharingContainedShift = 15;
constexpr std::uint32_t kSensitiveDetailRedactedShift = 16;
constexpr std::uint32_t kVersionShift = 24;
constexpr std::uint32_t kMagicShift = 28;

constexpr std::uint32_t kThreeBitMask = 0x07U;
constexpr std::uint32_t kFourBitMask = 0x0FU;
constexpr std::uint32_t kReservedMask = 0x00FE0000U;
constexpr std::uint32_t kPositionSharingUiDiagnosticMagic = 0x0CU;

template <typename T>
constexpr std::uint32_t enum_value(T value) {
    return static_cast<std::uint32_t>(value);
}

bool known_event(PositionSharingUiDiagnosticEvent value) {
    return enum_value(value) <=
           enum_value(PositionSharingUiDiagnosticEvent::failure);
}

bool known_outcome(PositionSharingUiDiagnosticOutcome value) {
    return enum_value(value) <=
           enum_value(PositionSharingUiDiagnosticOutcome::failed);
}

bool known_notice(PositionSharingUiDiagnosticNotice value) {
    return enum_value(value) <=
           enum_value(PositionSharingUiDiagnosticNotice::failed);
}

bool known_reason(PositionSharingUiDiagnosticReason value) {
    return enum_value(value) <=
           enum_value(PositionSharingUiDiagnosticReason::refresh_contained);
}

bool frame_shape_is_coherent(
    const PositionSharingUiDiagnostic& diagnostic) {
    if (diagnostic.frame_presented !=
        (diagnostic.notice != PositionSharingUiDiagnosticNotice::none)) {
        return false;
    }
    if (diagnostic.state_changed &&
        (diagnostic.event != PositionSharingUiDiagnosticEvent::action ||
         diagnostic.outcome !=
             PositionSharingUiDiagnosticOutcome::succeeded ||
         !diagnostic.frame_presented)) {
        return false;
    }
    return diagnostic.sharing_contained ==
           (diagnostic.outcome ==
            PositionSharingUiDiagnosticOutcome::contained);
}

bool event_shape_is_coherent(
    const PositionSharingUiDiagnostic& diagnostic) {
    using Event = PositionSharingUiDiagnosticEvent;
    using Outcome = PositionSharingUiDiagnosticOutcome;
    switch (diagnostic.event) {
        case Event::presentation:
        case Event::state_refresh:
            return diagnostic.outcome == Outcome::succeeded &&
                   diagnostic.frame_presented;
        case Event::action:
            if (diagnostic.outcome == Outcome::deferred) {
                return !diagnostic.frame_presented;
            }
            return (diagnostic.outcome == Outcome::succeeded ||
                    diagnostic.outcome == Outcome::rejected) &&
                   diagnostic.frame_presented;
        case Event::input:
            return diagnostic.outcome == Outcome::rejected &&
                   !diagnostic.frame_presented;
        case Event::failure:
            return (diagnostic.outcome == Outcome::contained ||
                    diagnostic.outcome == Outcome::failed) &&
                   !diagnostic.frame_presented;
    }
    return false;
}

bool reason_shape_is_coherent(
    const PositionSharingUiDiagnostic& diagnostic) {
    using Event = PositionSharingUiDiagnosticEvent;
    using Notice = PositionSharingUiDiagnosticNotice;
    using Outcome = PositionSharingUiDiagnosticOutcome;
    using Reason = PositionSharingUiDiagnosticReason;

    if (diagnostic.notice == Notice::stopped ||
        diagnostic.notice == Notice::active ||
        diagnostic.notice == Notice::waiting_for_fix ||
        diagnostic.notice == Notice::deferred) {
        return diagnostic.reason == Reason::none;
    }
    if (diagnostic.notice == Notice::failed) {
        return diagnostic.reason == Reason::outbound_faulted ||
               diagnostic.reason == Reason::presentation_unavailable ||
               diagnostic.reason == Reason::command_rejected;
    }
    if (diagnostic.reason == Reason::none) {
        return false;
    }

    if (diagnostic.event == Event::action &&
        diagnostic.outcome == Outcome::deferred) {
        return diagnostic.reason == Reason::clock_not_ready;
    }
    if (diagnostic.event == Event::action &&
        diagnostic.outcome == Outcome::rejected) {
        return diagnostic.reason == Reason::outbound_faulted ||
               diagnostic.reason == Reason::command_rejected;
    }
    if (diagnostic.event == Event::input) {
        return diagnostic.reason == Reason::stale_input ||
               diagnostic.reason == Reason::invalid_input;
    }
    if (diagnostic.event == Event::failure) {
        return diagnostic.reason == Reason::input_source_failed ||
               diagnostic.reason == Reason::display_not_ready ||
               diagnostic.reason == Reason::display_failed ||
               diagnostic.reason == Reason::revision_exhausted ||
               diagnostic.reason == Reason::presentation_unavailable ||
               diagnostic.reason == Reason::invalid_initial_revision ||
               diagnostic.reason == Reason::refresh_contained;
    }
    return false;
}

bool coherent(const PositionSharingUiDiagnostic& diagnostic) {
    return known_event(diagnostic.event) &&
           known_outcome(diagnostic.outcome) &&
           known_notice(diagnostic.notice) &&
           known_reason(diagnostic.reason) &&
           diagnostic.sensitive_detail_redacted &&
           frame_shape_is_coherent(diagnostic) &&
           event_shape_is_coherent(diagnostic) &&
           reason_shape_is_coherent(diagnostic);
}

bool known_disposition(integration::PositionSharingUiDisposition value) {
    return enum_value(value) <= enum_value(
               integration::PositionSharingUiDisposition::failed);
}

bool known_ui_error(integration::PositionSharingUiError value) {
    return enum_value(value) <= enum_value(
               integration::PositionSharingUiError::
                   post_action_refresh_failed);
}

bool known_presentation_error(
    integration::PositionSharingPresentationError value) {
    return enum_value(value) <= enum_value(
               integration::PositionSharingPresentationError::
                   outbound_faulted);
}

bool known_present_error(ui::PresentError value) {
    return enum_value(value) <= enum_value(ui::PresentError::sink_failed);
}

bool known_action_error(ui::ActionResolutionError value) {
    return enum_value(value) <= enum_value(
               ui::ActionResolutionError::hold_required);
}

bool known_control_error(integration::PositionSharingControlError value) {
    return enum_value(value) <= enum_value(
               integration::PositionSharingControlError::outbound_faulted);
}

bool known_scheduler_error(
    location::PositionBroadcastScheduleError value) {
    return enum_value(value) <= enum_value(
               location::PositionBroadcastScheduleError::sink_failed);
}

bool position_notice(ui::UiNotice notice) {
    return notice == ui::UiNotice::position_sharing_stopped ||
           notice == ui::UiNotice::position_sharing_active ||
           notice == ui::UiNotice::position_sharing_waiting_for_fix ||
           notice == ui::UiNotice::position_sharing_deferred ||
           notice == ui::UiNotice::position_sharing_failed;
}

bool valid_result(const integration::PositionSharingUiServiceResult& result) {
    using Disposition = integration::PositionSharingUiDisposition;
    using Error = integration::PositionSharingUiError;
    if (!known_disposition(result.disposition) ||
        !known_ui_error(result.error) ||
        !known_presentation_error(result.presentation_error) ||
        !known_present_error(result.present_error) ||
        !known_action_error(result.action_error) ||
        !known_control_error(result.control_error) ||
        !known_scheduler_error(result.scheduler_error) ||
        (result.frame_presented &&
         (!position_notice(result.presented_notice) ||
          result.revision == 0)) ||
        (!result.frame_presented &&
         result.presented_notice != ui::UiNotice::none) ||
        (result.state_changed &&
         result.disposition != Disposition::action_applied)) {
        return false;
    }

    if (result.disposition == Disposition::idle) {
        return result.error == Error::none &&
               result.action_error ==
                   ui::ActionResolutionError::input_not_ready &&
               !result.frame_presented && !result.state_changed;
    }
    if (result.disposition != Disposition::failed &&
        result.error != Error::none) {
        return false;
    }
    if (result.disposition == Disposition::failed) {
        return result.error != Error::none &&
               !result.frame_presented && !result.state_changed;
    }
    if (result.disposition == Disposition::presented ||
        result.disposition == Disposition::refreshed) {
        return result.frame_presented && !result.state_changed;
    }
    if (result.disposition == Disposition::action_applied) {
        return result.frame_presented &&
               result.action_error == ui::ActionResolutionError::none &&
               result.control_error ==
                   integration::PositionSharingControlError::none;
    }
    if (result.disposition == Disposition::action_deferred) {
        return !result.frame_presented &&
               result.control_error ==
                   integration::PositionSharingControlError::
                       outbound_not_ready;
    }
    if (result.disposition == Disposition::input_rejected) {
        return !result.frame_presented &&
               result.action_error != ui::ActionResolutionError::none &&
               result.action_error !=
                   ui::ActionResolutionError::input_not_ready &&
               result.action_error !=
                   ui::ActionResolutionError::input_failed;
    }
    return result.disposition == Disposition::action_rejected &&
           result.frame_presented &&
           result.control_error !=
               integration::PositionSharingControlError::none &&
           result.control_error !=
               integration::PositionSharingControlError::
                   outbound_not_ready;
}

PositionSharingUiDiagnosticNotice diagnostic_notice(ui::UiNotice notice) {
    using Notice = PositionSharingUiDiagnosticNotice;
    switch (notice) {
        case ui::UiNotice::position_sharing_stopped:
            return Notice::stopped;
        case ui::UiNotice::position_sharing_active:
            return Notice::active;
        case ui::UiNotice::position_sharing_waiting_for_fix:
            return Notice::waiting_for_fix;
        case ui::UiNotice::position_sharing_deferred:
            return Notice::deferred;
        case ui::UiNotice::position_sharing_failed:
            return Notice::failed;
        default:
            return Notice::none;
    }
}

PositionSharingUiDiagnosticReason display_reason(ui::PresentError error) {
    return error == ui::PresentError::sink_not_ready
               ? PositionSharingUiDiagnosticReason::display_not_ready
               : PositionSharingUiDiagnosticReason::display_failed;
}

PositionSharingUiDiagnosticReason diagnostic_reason(
    const integration::PositionSharingUiServiceResult& result) {
    using ActionError = ui::ActionResolutionError;
    using ControlError = integration::PositionSharingControlError;
    using Error = integration::PositionSharingUiError;
    using PresentationError =
        integration::PositionSharingPresentationError;
    using Reason = PositionSharingUiDiagnosticReason;

    switch (result.error) {
        case Error::invalid_initial_revision:
            return Reason::invalid_initial_revision;
        case Error::revision_exhausted:
            return Reason::revision_exhausted;
        case Error::presentation_unavailable:
            return Reason::presentation_unavailable;
        case Error::display_failed:
            return display_reason(result.present_error);
        case Error::input_failed:
            return Reason::input_source_failed;
        case Error::external_refresh_failed:
        case Error::post_action_refresh_failed:
            if (result.present_error != ui::PresentError::none) {
                return display_reason(result.present_error);
            }
            return result.presentation_error != PresentationError::none
                       ? Reason::presentation_unavailable
                       : Reason::refresh_contained;
        case Error::none:
            break;
    }

    switch (result.control_error) {
        case ControlError::outbound_not_ready:
            return Reason::clock_not_ready;
        case ControlError::outbound_faulted:
            return Reason::outbound_faulted;
        case ControlError::invalid_action:
        case ControlError::scheduler_rejected:
            return Reason::command_rejected;
        case ControlError::none:
            break;
    }

    if (result.action_error == ActionError::stale_frame) {
        return Reason::stale_input;
    }
    if (result.action_error != ActionError::none &&
        result.action_error != ActionError::input_not_ready) {
        return result.action_error == ActionError::input_failed
                   ? Reason::input_source_failed
                   : Reason::invalid_input;
    }
    if (result.presentation_error == PresentationError::outbound_faulted) {
        return Reason::outbound_faulted;
    }
    if (result.presentation_error != PresentationError::none) {
        return Reason::presentation_unavailable;
    }
    if (result.present_error != ui::PresentError::none) {
        return display_reason(result.present_error);
    }
    return Reason::none;
}

PositionSharingUiDiagnostic diagnostic_from_result(
    const integration::PositionSharingUiServiceResult& result) {
    using Disposition = integration::PositionSharingUiDisposition;
    using Event = PositionSharingUiDiagnosticEvent;
    using Outcome = PositionSharingUiDiagnosticOutcome;

    Event event = Event::failure;
    Outcome outcome = Outcome::failed;
    switch (result.disposition) {
        case Disposition::presented:
            event = Event::presentation;
            outcome = Outcome::succeeded;
            break;
        case Disposition::refreshed:
            event = Event::state_refresh;
            outcome = Outcome::succeeded;
            break;
        case Disposition::action_applied:
            event = Event::action;
            outcome = Outcome::succeeded;
            break;
        case Disposition::action_deferred:
            event = Event::action;
            outcome = Outcome::deferred;
            break;
        case Disposition::input_rejected:
            event = Event::input;
            outcome = Outcome::rejected;
            break;
        case Disposition::action_rejected:
            event = Event::action;
            outcome = Outcome::rejected;
            break;
        case Disposition::failed:
            event = Event::failure;
            outcome = result.error ==
                              integration::PositionSharingUiError::
                                  revision_exhausted ||
                          result.error ==
                              integration::PositionSharingUiError::
                                  external_refresh_failed ||
                          result.error ==
                              integration::PositionSharingUiError::
                                  post_action_refresh_failed
                      ? Outcome::contained
                      : Outcome::failed;
            break;
        case Disposition::idle:
            break;
    }

    return {
        event,
        outcome,
        diagnostic_notice(result.presented_notice),
        diagnostic_reason(result),
        result.frame_presented,
        result.state_changed,
        outcome == Outcome::contained,
        true,
    };
}

}  // namespace

PositionSharingUiDiagnosticEncodeResult
encode_position_sharing_ui_diagnostic(
    const integration::PositionSharingUiServiceResult& result) {
    if (!valid_result(result)) {
        return {};
    }
    if (result.disposition ==
        integration::PositionSharingUiDisposition::idle) {
        return {PositionSharingUiDiagnosticError::no_event};
    }

    const auto diagnostic = diagnostic_from_result(result);
    if (!coherent(diagnostic)) {
        return {};
    }
    const std::uint32_t word =
        (kPositionSharingUiDiagnosticMagic << kMagicShift) |
        (static_cast<std::uint32_t>(kPositionSharingUiDiagnosticVersion)
         << kVersionShift) |
        (enum_value(diagnostic.event) << kEventShift) |
        (enum_value(diagnostic.outcome) << kOutcomeShift) |
        (enum_value(diagnostic.notice) << kNoticeShift) |
        (enum_value(diagnostic.reason) << kReasonShift) |
        (static_cast<std::uint32_t>(diagnostic.frame_presented)
         << kFramePresentedShift) |
        (static_cast<std::uint32_t>(diagnostic.state_changed)
         << kStateChangedShift) |
        (static_cast<std::uint32_t>(diagnostic.sharing_contained)
         << kSharingContainedShift) |
        (static_cast<std::uint32_t>(diagnostic.sensitive_detail_redacted)
         << kSensitiveDetailRedactedShift);
    return {PositionSharingUiDiagnosticError::none, word};
}

PositionSharingUiDiagnosticDecodeResult
decode_position_sharing_ui_diagnostic(std::uint32_t word) {
    if (((word >> kMagicShift) & kFourBitMask) !=
            kPositionSharingUiDiagnosticMagic ||
        (word & kReservedMask) != 0) {
        return {};
    }
    const auto version =
        static_cast<std::uint8_t>((word >> kVersionShift) & kFourBitMask);
    if (version != kPositionSharingUiDiagnosticVersion) {
        return {PositionSharingUiDiagnosticError::unsupported_version};
    }

    PositionSharingUiDiagnostic diagnostic{};
    diagnostic.event = static_cast<PositionSharingUiDiagnosticEvent>(
        (word >> kEventShift) & kThreeBitMask);
    diagnostic.outcome = static_cast<PositionSharingUiDiagnosticOutcome>(
        (word >> kOutcomeShift) & kThreeBitMask);
    diagnostic.notice = static_cast<PositionSharingUiDiagnosticNotice>(
        (word >> kNoticeShift) & kThreeBitMask);
    diagnostic.reason = static_cast<PositionSharingUiDiagnosticReason>(
        (word >> kReasonShift) & kFourBitMask);
    diagnostic.frame_presented =
        ((word >> kFramePresentedShift) & 1U) != 0;
    diagnostic.state_changed =
        ((word >> kStateChangedShift) & 1U) != 0;
    diagnostic.sharing_contained =
        ((word >> kSharingContainedShift) & 1U) != 0;
    diagnostic.sensitive_detail_redacted =
        ((word >> kSensitiveDetailRedactedShift) & 1U) != 0;
    if (!coherent(diagnostic)) {
        return {};
    }
    return {PositionSharingUiDiagnosticError::none, diagnostic};
}

namespace detail {

LogLevel position_sharing_ui_diagnostic_level(
    const PositionSharingUiDiagnostic& diagnostic) {
    using Notice = PositionSharingUiDiagnosticNotice;
    using Outcome = PositionSharingUiDiagnosticOutcome;
    using Reason = PositionSharingUiDiagnosticReason;
    if (diagnostic.outcome == Outcome::contained ||
        diagnostic.reason == Reason::outbound_faulted ||
        diagnostic.reason == Reason::input_source_failed ||
        diagnostic.reason == Reason::display_failed ||
        diagnostic.reason == Reason::presentation_unavailable ||
        diagnostic.reason == Reason::invalid_initial_revision ||
        diagnostic.outcome == Outcome::failed) {
        return diagnostic.reason == Reason::display_not_ready
                   ? LogLevel::warn
                   : LogLevel::error;
    }
    if (diagnostic.outcome == Outcome::deferred ||
        diagnostic.outcome == Outcome::rejected ||
        diagnostic.notice == Notice::waiting_for_fix ||
        diagnostic.notice == Notice::deferred) {
        return LogLevel::warn;
    }
    return LogLevel::info;
}

std::array<char, kPositionSharingUiDiagnosticMessageBytes + 1>
format_position_sharing_ui_diagnostic(std::uint32_t word) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::array<char, kPositionSharingUiDiagnosticMessageBytes + 1> message{
        'O', 'T', 'P', 'D', '0', '='};
    for (std::size_t index = 0; index < 8; ++index) {
        const auto shift = static_cast<std::uint32_t>((7 - index) * 4);
        message[6 + index] = digits[(word >> shift) & 0x0FU];
    }
    message[kPositionSharingUiDiagnosticMessageBytes] = '\0';
    return message;
}

}  // namespace detail

}  // namespace opentrail::diagnostics
