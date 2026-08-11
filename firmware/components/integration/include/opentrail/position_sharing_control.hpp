#pragma once

#include <cstdint>

#include "opentrail/local_interface.hpp"
#include "opentrail/position_broadcast_scheduler.hpp"

namespace opentrail::integration {

enum class PositionSharingPresentationError : std::uint8_t {
    none = 0,
    invalid_revision,
    invalid_scheduler_status,
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

enum class PositionSharingControlError : std::uint8_t {
    none = 0,
    invalid_action,
    scheduler_rejected,
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

}  // namespace opentrail::integration
