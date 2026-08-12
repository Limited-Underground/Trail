#pragma once

#include <cstdint>

#include "opentrail/quick_status_menu_coordinator.hpp"

namespace opentrail::integration {

enum class QuickStatusParentPageMode : std::uint8_t {
    inactive = 0,
    parent,
    menu,
    restoring_parent,
    faulted,
};

enum class QuickStatusParentPageDisposition : std::uint8_t {
    presented = 0,
    opened,
    forwarded,
    restored,
    selection_requested,
    exit_requested,
    idle,
    input_rejected,
    display_deferred,
    failed,
};

enum class QuickStatusParentPageError : std::uint8_t {
    none = 0,
    invalid_activation,
    revision_exhausted,
    display_failed,
    input_failed,
    unexpected_action,
    menu_failed,
};

struct QuickStatusParentPageResult {
    QuickStatusParentPageDisposition disposition{
        QuickStatusParentPageDisposition::failed};
    QuickStatusParentPageError error{QuickStatusParentPageError::none};
    ui::PresentError present_error{ui::PresentError::none};
    ui::ActionResolutionError action_error{
        ui::ActionResolutionError::none};
    QuickStatusMenuResult menu{};
    protocol::QuickStatusKind selection{protocol::QuickStatusKind::ok};
    std::uint32_t revision{0};
    bool input_polled{false};
    bool frame_presented{false};
    bool menu_called{false};
    bool has_selection{false};
};

struct QuickStatusParentPageStatus {
    QuickStatusParentPageMode mode{QuickStatusParentPageMode::inactive};
    QuickStatusParentPageError latched_error{
        QuickStatusParentPageError::none};
    std::uint32_t active_revision{0};
    std::uint32_t activations{0};
    std::uint32_t service_calls{0};
    std::uint32_t parent_presentations{0};
    std::uint32_t menu_entries{0};
    std::uint32_t menu_exits{0};
    std::uint32_t selection_requests{0};
    std::uint32_t exit_requests{0};
    std::uint32_t input_rejections{0};
    std::uint32_t failures{0};
};

// Owns one narrow status page around the local quick-status menu. It surfaces
// a typed selection only after the parent has been restored successfully.
class QuickStatusParentPageCoordinator {
public:
    explicit QuickStatusParentPageCoordinator(
        ui::CheckedLocalInterface& local_interface);
    QuickStatusParentPageCoordinator(
        const QuickStatusParentPageCoordinator&) = delete;
    QuickStatusParentPageCoordinator& operator=(
        const QuickStatusParentPageCoordinator&) = delete;
    QuickStatusParentPageCoordinator(
        QuickStatusParentPageCoordinator&&) = delete;
    QuickStatusParentPageCoordinator& operator=(
        QuickStatusParentPageCoordinator&&) = delete;

    [[nodiscard]] QuickStatusParentPageResult activate(
        std::uint32_t revision,
        const ui::UiStatusSummary& status_summary);
    [[nodiscard]] QuickStatusParentPageResult service();
    [[nodiscard]] QuickStatusParentPageStatus status() const;
    [[nodiscard]] QuickStatusMenuStatus menu_status() const;

private:
    [[nodiscard]] ui::UiFrame parent_frame(std::uint32_t revision) const;
    [[nodiscard]] bool present_parent(
        std::uint32_t revision,
        QuickStatusParentPageResult& result);
    void finish_restore(QuickStatusParentPageResult& result);
    void latch(QuickStatusParentPageError error);

    ui::CheckedLocalInterface& local_interface_;
    QuickStatusMenuCoordinator menu_;
    QuickStatusParentPageStatus status_{};
    ui::UiStatusSummary parent_status_{};
    std::uint32_t pending_parent_revision_{0};
    protocol::QuickStatusKind pending_selection_{
        protocol::QuickStatusKind::ok};
    bool pending_has_selection_{false};
};

}  // namespace opentrail::integration
