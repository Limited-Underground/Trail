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
