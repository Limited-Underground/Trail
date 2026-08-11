#include "opentrail/position_sharing_ui_coordinator.hpp"

#include <limits>

namespace opentrail::integration {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace

PositionSharingUiCoordinator::PositionSharingUiCoordinator(
    OutboundServiceCoordinator& outbound,
    ui::CheckedLocalInterface& local_interface,
    std::uint32_t initial_revision)
    : outbound_(outbound), local_interface_(local_interface) {
    status_.next_revision = initial_revision;
    if (initial_revision == 0 ||
        initial_revision == std::numeric_limits<std::uint32_t>::max()) {
        latch(PositionSharingUiError::invalid_initial_revision);
    }
}

PositionSharingUiServiceResult PositionSharingUiCoordinator::service() {
    saturating_increment(status_.service_calls);
    PositionSharingUiServiceResult result{};
    if (status_.faulted) {
        result.error = status_.latched_error;
        return result;
    }

    if (!status_.has_presented_frame) {
        if (publish(result)) {
            result.disposition = PositionSharingUiDisposition::presented;
        }
        return result;
    }

    // Reserve the final revision value. An action is never accepted unless a
    // strictly newer post-action frame remains representable.
    if (status_.next_revision ==
        std::numeric_limits<std::uint32_t>::max()) {
        const auto contained = outbound_.stop_position_sharing();
        (void)contained;
        saturating_increment(status_.failures);
        latch(PositionSharingUiError::revision_exhausted);
        result.error = PositionSharingUiError::revision_exhausted;
        return result;
    }

    const auto action = local_interface_.poll_action();
    result.action_error = action.error;
    if (action.error == ui::ActionResolutionError::input_not_ready) {
        result.disposition = PositionSharingUiDisposition::idle;
        saturating_increment(status_.idle_polls);
        return result;
    }
    if (!action.ok()) {
        saturating_increment(status_.input_rejections);
        if (action.error == ui::ActionResolutionError::input_failed) {
            result.disposition = PositionSharingUiDisposition::failed;
            result.error = PositionSharingUiError::input_failed;
            saturating_increment(status_.failures);
        } else {
            result.disposition =
                PositionSharingUiDisposition::input_rejected;
        }
        return result;
    }

    saturating_increment(status_.resolved_actions);
    const auto control = apply_position_sharing_action(
        outbound_, action.action);
    result.control_error = control.error;
    result.scheduler_error = control.scheduler_error;
    result.state_changed = control.state_changed;
    if (control.error == PositionSharingControlError::outbound_not_ready) {
        result.disposition = PositionSharingUiDisposition::action_deferred;
        saturating_increment(status_.actions_deferred);
        return result;
    }

    if (control.applied()) {
        saturating_increment(status_.actions_applied);
    }

    if (!publish(result)) {
        const auto contained = outbound_.stop_position_sharing();
        (void)contained;
        latch(PositionSharingUiError::post_action_refresh_failed);
        result.disposition = PositionSharingUiDisposition::failed;
        result.error = PositionSharingUiError::post_action_refresh_failed;
        return result;
    }

    result.disposition = control.applied()
                             ? PositionSharingUiDisposition::action_applied
                             : PositionSharingUiDisposition::action_rejected;
    return result;
}

PositionSharingUiCoordinatorStatus
PositionSharingUiCoordinator::status() const {
    return status_;
}

bool PositionSharingUiCoordinator::publish(
    PositionSharingUiServiceResult& result) {
    result.revision = status_.next_revision;
    const auto presentation = make_position_sharing_presentation(
        outbound_.position_status(),
        outbound_.status(),
        status_.next_revision);
    result.presentation_error = presentation.error;
    if (!presentation.presentable()) {
        result.error = PositionSharingUiError::presentation_unavailable;
        saturating_increment(status_.failures);
        return false;
    }

    const auto presented = local_interface_.present(presentation.frame);
    result.present_error = presented.error;
    if (!presented.ok()) {
        result.error = PositionSharingUiError::display_failed;
        saturating_increment(status_.failures);
        return false;
    }

    result.frame_presented = true;
    status_.has_presented_frame = true;
    status_.active_revision = status_.next_revision;
    ++status_.next_revision;
    saturating_increment(status_.presented_frames);
    return true;
}

void PositionSharingUiCoordinator::latch(PositionSharingUiError error) {
    status_.faulted = true;
    status_.latched_error = error;
}

}  // namespace opentrail::integration
