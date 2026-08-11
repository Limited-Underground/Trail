#include "opentrail/position_sharing_control.hpp"

namespace opentrail::integration {
namespace {

ui::UiFrame status_frame(std::uint32_t revision,
                         ui::UiAttention attention,
                         ui::UiNotice notice,
                         ui::UiAction action) {
    ui::UiFrame frame{};
    frame.revision = revision;
    frame.screen = ui::UiScreen::status;
    frame.attention = attention;
    frame.notice = notice;
    frame.action_count = 1;
    frame.actions[0] = {action, true};
    return frame;
}

ui::UiFrame failed_frame(std::uint32_t revision) {
    ui::UiFrame frame{};
    frame.revision = revision;
    frame.screen = ui::UiScreen::system_fault;
    frame.attention = ui::UiAttention::critical;
    frame.notice = ui::UiNotice::position_sharing_failed;
    return frame;
}

bool terminal_error(location::PositionBroadcastScheduleError error) {
    return error == location::PositionBroadcastScheduleError::invalid_policy ||
           error == location::PositionBroadcastScheduleError::clock_regression ||
           error == location::PositionBroadcastScheduleError::time_exhausted;
}

bool known_clock_error(time::MonotonicClockError error) {
    return error == time::MonotonicClockError::none ||
           error == time::MonotonicClockError::not_ready ||
           error == time::MonotonicClockError::source_failed ||
           error == time::MonotonicClockError::rollback_detected ||
           error == time::MonotonicClockError::fault_latched;
}

bool valid_outbound_status(const OutboundServiceStatus& status) {
    const auto runtime_calls =
        static_cast<std::uint64_t>(status.service_calls) +
        status.position_command_calls;
    if (!known_clock_error(status.latched_clock_error) ||
        status.serviced_cycles > status.service_calls ||
        status.clock_deferred > runtime_calls ||
        status.clock_failures > runtime_calls ||
        status.latched_refusals > status.service_calls ||
        status.position_commands_applied >
            status.position_command_calls ||
        status.position_commands_deferred >
            status.position_command_calls ||
        status.position_command_failures >
            status.position_command_calls ||
        (!status.has_time && status.last_now_ms != 0)) {
        return false;
    }
    if (!status.faulted) {
        return status.latched_clock_error == time::MonotonicClockError::none &&
               status.clock_failures == 0 && status.latched_refusals == 0;
    }
    return (status.latched_clock_error ==
                time::MonotonicClockError::source_failed ||
            status.latched_clock_error ==
                time::MonotonicClockError::rollback_detected) &&
           status.clock_failures != 0;
}

PositionSharingPresentationResult invalid_status(
    std::uint32_t revision) {
    return {
        PositionSharingPresentationError::invalid_scheduler_status,
        failed_frame(revision),
        true,
    };
}

}  // namespace

PositionSharingPresentationResult make_position_sharing_presentation(
    const location::PositionBroadcastSchedulerStatus& status,
    std::uint32_t frame_revision) {
    if (frame_revision == 0) {
        return {PositionSharingPresentationError::invalid_revision};
    }

    if (!status.policy_valid || status.clock_failed || status.time_exhausted ||
        terminal_error(status.last_error)) {
        return invalid_status(frame_revision);
    }

    if (!status.active) {
        return {
            PositionSharingPresentationError::none,
            status_frame(frame_revision,
                         ui::UiAttention::information,
                         ui::UiNotice::position_sharing_stopped,
                         ui::UiAction::start_position_sharing),
            true,
        };
    }

    switch (status.last_error) {
        case location::PositionBroadcastScheduleError::none:
            return {
                PositionSharingPresentationError::none,
                status_frame(frame_revision,
                             ui::UiAttention::information,
                             ui::UiNotice::position_sharing_active,
                             ui::UiAction::stop_position_sharing),
                true,
            };
        case location::PositionBroadcastScheduleError::no_current_fix:
            return {
                PositionSharingPresentationError::none,
                status_frame(
                    frame_revision,
                    ui::UiAttention::warning,
                    ui::UiNotice::position_sharing_waiting_for_fix,
                    ui::UiAction::stop_position_sharing),
                true,
            };
        case location::PositionBroadcastScheduleError::encode_failed:
        case location::PositionBroadcastScheduleError::sink_not_ready:
        case location::PositionBroadcastScheduleError::sink_full:
        case location::PositionBroadcastScheduleError::sink_failed:
            return {
                PositionSharingPresentationError::none,
                status_frame(frame_revision,
                             ui::UiAttention::warning,
                             ui::UiNotice::position_sharing_deferred,
                             ui::UiAction::stop_position_sharing),
                true,
            };
        case location::PositionBroadcastScheduleError::invalid_policy:
        case location::PositionBroadcastScheduleError::clock_regression:
        case location::PositionBroadcastScheduleError::time_exhausted:
        default:
            return invalid_status(frame_revision);
    }
}

PositionSharingPresentationResult make_position_sharing_presentation(
    const location::PositionBroadcastSchedulerStatus& scheduler_status,
    const OutboundServiceStatus& outbound_status,
    std::uint32_t frame_revision) {
    if (frame_revision == 0) {
        return {PositionSharingPresentationError::invalid_revision};
    }
    if (!valid_outbound_status(outbound_status) ||
        (outbound_status.faulted && scheduler_status.active)) {
        return {
            PositionSharingPresentationError::invalid_outbound_status,
            failed_frame(frame_revision),
            true,
        };
    }
    if (outbound_status.faulted) {
        return {
            PositionSharingPresentationError::outbound_faulted,
            failed_frame(frame_revision),
            true,
        };
    }
    return make_position_sharing_presentation(
        scheduler_status, frame_revision);
}

PositionSharingControlResult apply_position_sharing_action(
    location::PositionBroadcastScheduler& scheduler,
    ui::UiAction action,
    std::uint64_t now_ms) {
    const auto was_active = scheduler.status().active;
    if (action == ui::UiAction::start_position_sharing) {
        const auto scheduler_error = scheduler.start(now_ms);
        if (scheduler_error != location::PositionBroadcastScheduleError::none) {
            return {
                PositionSharingControlError::scheduler_rejected,
                scheduler_error,
                false,
            };
        }
        return {
            PositionSharingControlError::none,
            location::PositionBroadcastScheduleError::none,
            !was_active && scheduler.status().active,
        };
    }
    if (action == ui::UiAction::stop_position_sharing) {
        scheduler.stop();
        return {
            PositionSharingControlError::none,
            location::PositionBroadcastScheduleError::none,
            was_active && !scheduler.status().active,
        };
    }
    return {
        PositionSharingControlError::invalid_action,
        location::PositionBroadcastScheduleError::none,
        false,
    };
}

}  // namespace opentrail::integration
