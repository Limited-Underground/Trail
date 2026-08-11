#pragma once

#include <cstdint>

#include "opentrail/local_interface.hpp"
#include "opentrail/outbound_service_coordinator.hpp"
#include "opentrail/position_broadcast_scheduler.hpp"

namespace opentrail::integration {

enum class PositionSharingPresentationError : std::uint8_t {
    none = 0,
    invalid_revision,
    invalid_scheduler_status,
    invalid_outbound_status,
    outbound_faulted,
};

struct PositionSharingPresentationResult {
    PositionSharingPresentationError error{
        PositionSharingPresentationError::invalid_scheduler_status};
    ui::UiFrame frame{};
    bool has_safe_frame{false};

    [[nodiscard]] constexpr bool mapped() const {
        return error == PositionSharingPresentationError::none;
    }

    [[nodiscard]] constexpr bool presentable() const {
        return has_safe_frame;
    }
};

// Converts scheduler state into a fixed semantic frame. The frame contains no
// coordinates, peer identity, free text, or renderer-specific control IDs.
[[nodiscard]] PositionSharingPresentationResult
make_position_sharing_presentation(
    const location::PositionBroadcastSchedulerStatus& status,
    std::uint32_t frame_revision);

// Target-facing overload. A coherent latched outbound clock fault takes
// precedence over the scheduler's stopped state and produces a no-action
// system-fault frame rather than offering an unsafe restart.
[[nodiscard]] PositionSharingPresentationResult
make_position_sharing_presentation(
    const location::PositionBroadcastSchedulerStatus& scheduler_status,
    const OutboundServiceStatus& outbound_status,
    std::uint32_t frame_revision);

enum class PositionSharingControlError : std::uint8_t {
    none = 0,
    invalid_action,
    scheduler_rejected,
    invalid_outbound_status,
    outbound_faulted,
};

struct PositionSharingControlResult {
    PositionSharingControlError error{
        PositionSharingControlError::invalid_action};
    location::PositionBroadcastScheduleError scheduler_error{
        location::PositionBroadcastScheduleError::none};
    bool state_changed{false};

    [[nodiscard]] constexpr bool applied() const {
        return error == PositionSharingControlError::none;
    }
};

// Applies only the two semantic privacy actions. Starting arms the scheduler;
// it does not call service or submit a payload. Stopping is immediate.
[[nodiscard]] PositionSharingControlResult apply_position_sharing_action(
    location::PositionBroadcastScheduler& scheduler,
    ui::UiAction action,
    std::uint64_t now_ms);

// Target-facing overload. Start is refused whenever the outbound runtime is
// latched. Stop remains safe and idempotent, including after a fault.
[[nodiscard]] PositionSharingControlResult apply_position_sharing_action(
    location::PositionBroadcastScheduler& scheduler,
    const OutboundServiceStatus& outbound_status,
    ui::UiAction action,
    std::uint64_t now_ms);

}  // namespace opentrail::integration
