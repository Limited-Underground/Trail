#include "opentrail/position_sharing_control.hpp"

namespace opentrail::integration {

PositionSharingControlResult apply_position_sharing_action(
    OutboundServiceCoordinator& outbound,
    ui::UiAction action) {
    OutboundPositionCommandResult result{};
    if (action == ui::UiAction::start_position_sharing) {
        result = outbound.start_position_sharing();
    } else if (action == ui::UiAction::stop_position_sharing) {
        result = outbound.stop_position_sharing();
    } else {
        return {
            PositionSharingControlError::invalid_action,
            location::PositionBroadcastScheduleError::none,
            false,
        };
    }

    switch (result.error) {
        case OutboundPositionCommandError::none:
            return {
                PositionSharingControlError::none,
                result.scheduler_error,
                result.state_changed,
            };
        case OutboundPositionCommandError::clock_not_ready:
            return {
                PositionSharingControlError::outbound_not_ready,
                result.scheduler_error,
                false,
            };
        case OutboundPositionCommandError::scheduler_rejected:
            return {
                PositionSharingControlError::scheduler_rejected,
                result.scheduler_error,
                false,
            };
        case OutboundPositionCommandError::clock_faulted:
        default:
            return {
                PositionSharingControlError::outbound_faulted,
                result.scheduler_error,
                false,
            };
    }
}

}  // namespace opentrail::integration
