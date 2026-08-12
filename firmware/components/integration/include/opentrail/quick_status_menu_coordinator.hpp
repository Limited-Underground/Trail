#pragma once

#include <cstdint>

#include "opentrail/local_interface.hpp"
#include "opentrail/quick_status_codec.hpp"

namespace opentrail::integration {

enum class QuickStatusMenuPage : std::uint8_t {
    first = 0,
    second,
};

enum class QuickStatusMenuMode : std::uint8_t {
    inactive = 0,
    first_page,
    second_page,
    transitioning,
    faulted,
};

enum class QuickStatusMenuDisposition : std::uint8_t {
    presented = 0,
    page_changed,
    selection_requested,
    exit_requested,
    idle,
    input_rejected,
    display_deferred,
    failed,
};

enum class QuickStatusMenuError : std::uint8_t {
    none = 0,
    invalid_activation,
    revision_exhausted,
    display_failed,
    input_failed,
    unexpected_action,
};

struct QuickStatusMenuResult {
    QuickStatusMenuDisposition disposition{
        QuickStatusMenuDisposition::failed};
    QuickStatusMenuError error{QuickStatusMenuError::none};
    ui::PresentError present_error{ui::PresentError::none};
    ui::ActionResolutionError action_error{
        ui::ActionResolutionError::none};
    protocol::QuickStatusKind selection{protocol::QuickStatusKind::ok};
    std::uint32_t revision{0};
    std::uint32_t minimum_parent_revision{0};
    bool input_polled{false};
    bool frame_presented{false};
    bool has_selection{false};
};

struct QuickStatusMenuStatus {
    QuickStatusMenuMode mode{QuickStatusMenuMode::inactive};
    QuickStatusMenuError latched_error{QuickStatusMenuError::none};
    std::uint32_t active_revision{0};
    std::uint32_t activations{0};
    std::uint32_t service_calls{0};
    std::uint32_t presentations{0};
    std::uint32_t page_changes{0};
    std::uint32_t selection_requests{0};
    std::uint32_t exit_requests{0};
    std::uint32_t input_rejections{0};
    std::uint32_t failures{0};
};

// Owns only local quick-status selection and page navigation. A successful
// selection is a typed request for a parent application, not queued, sent,
// received, acknowledged, or delivered radio evidence.
class QuickStatusMenuCoordinator {
public:
    explicit QuickStatusMenuCoordinator(
        ui::CheckedLocalInterface& local_interface);
    QuickStatusMenuCoordinator(const QuickStatusMenuCoordinator&) = delete;
    QuickStatusMenuCoordinator& operator=(
        const QuickStatusMenuCoordinator&) = delete;
    QuickStatusMenuCoordinator(QuickStatusMenuCoordinator&&) = delete;
    QuickStatusMenuCoordinator& operator=(QuickStatusMenuCoordinator&&) =
        delete;

    [[nodiscard]] QuickStatusMenuResult activate(
        std::uint32_t revision,
        const ui::UiStatusSummary& status_summary);
    [[nodiscard]] QuickStatusMenuResult service();
    [[nodiscard]] QuickStatusMenuStatus status() const;

private:
    [[nodiscard]] ui::UiFrame frame(
        QuickStatusMenuPage page,
        std::uint32_t revision) const;
    [[nodiscard]] bool present(
        QuickStatusMenuPage page,
        std::uint32_t revision,
        QuickStatusMenuResult& result);
    void latch(QuickStatusMenuError error);

    ui::CheckedLocalInterface& local_interface_;
    QuickStatusMenuStatus status_{};
    ui::UiStatusSummary parent_status_{};
    QuickStatusMenuPage pending_page_{QuickStatusMenuPage::first};
    std::uint32_t pending_revision_{0};
};

}  // namespace opentrail::integration
