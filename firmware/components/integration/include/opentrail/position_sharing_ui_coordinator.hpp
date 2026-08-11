#pragma once

#include <cstdint>

#include "opentrail/outbound_service_coordinator.hpp"
#include "opentrail/position_sharing_control.hpp"

namespace opentrail::integration {

enum class PositionSharingUiDisposition : std::uint8_t {
    presented = 0,
    idle,
    action_applied,
    action_deferred,
    input_rejected,
    action_rejected,
    failed,
};

enum class PositionSharingUiError : std::uint8_t {
    none = 0,
    invalid_initial_revision,
    revision_exhausted,
    presentation_unavailable,
    display_failed,
    input_failed,
    post_action_refresh_failed,
};

struct PositionSharingUiServiceResult {
    PositionSharingUiDisposition disposition{
        PositionSharingUiDisposition::failed};
    PositionSharingUiError error{PositionSharingUiError::none};
    PositionSharingPresentationError presentation_error{
        PositionSharingPresentationError::none};
    ui::PresentError present_error{ui::PresentError::none};
    ui::ActionResolutionError action_error{
        ui::ActionResolutionError::none};
    PositionSharingControlError control_error{
        PositionSharingControlError::none};
    location::PositionBroadcastScheduleError scheduler_error{
        location::PositionBroadcastScheduleError::none};
    std::uint32_t revision{0};
    bool frame_presented{false};
    bool state_changed{false};
};

struct PositionSharingUiCoordinatorStatus {
    bool faulted{false};
    bool has_presented_frame{false};
    PositionSharingUiError latched_error{PositionSharingUiError::none};
    std::uint32_t active_revision{0};
    std::uint32_t next_revision{0};
    std::uint32_t service_calls{0};
    std::uint32_t presented_frames{0};
    std::uint32_t idle_polls{0};
    std::uint32_t resolved_actions{0};
    std::uint32_t actions_applied{0};
    std::uint32_t actions_deferred{0};
    std::uint32_t input_rejections{0};
    std::uint32_t failures{0};
};

// Cooperative single-owner position UI service. The target must serialize this
// service with outbound service calls; this component does not create a task,
// lock, renderer, or physical input adapter.
class PositionSharingUiCoordinator {
public:
    PositionSharingUiCoordinator(
        OutboundServiceCoordinator& outbound,
        ui::CheckedLocalInterface& local_interface,
        std::uint32_t initial_revision = 1);

    [[nodiscard]] PositionSharingUiServiceResult service();
    [[nodiscard]] PositionSharingUiCoordinatorStatus status() const;

private:
    [[nodiscard]] bool publish(
        PositionSharingUiServiceResult& result);
    void latch(PositionSharingUiError error);

    OutboundServiceCoordinator& outbound_;
    ui::CheckedLocalInterface& local_interface_;
    PositionSharingUiCoordinatorStatus status_{};
};

}  // namespace opentrail::integration
