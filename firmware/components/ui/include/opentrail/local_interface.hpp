#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::ui {

constexpr std::size_t kMaxUiActions = 4;

enum class DisplayWriteError : std::uint8_t {
    none = 0,
    not_ready,
    sink_failed,
};

enum class InputReadError : std::uint8_t {
    none = 0,
    not_ready,
    source_failed,
};

enum class InputGesture : std::uint8_t {
    activate = 0,
    hold,
};

enum class UiScreen : std::uint8_t {
    home = 0,
    status,
    quick_status_menu,
    critical_confirmation,
    system_fault,
};

enum class UiAttention : std::uint8_t {
    none = 0,
    information,
    warning,
    critical,
};

enum class UiIndicatorState : std::uint8_t {
    unknown = 0,
    unavailable,
    normal,
    warning,
    critical,
};

enum class UiNotice : std::uint8_t {
    none = 0,
    radio_unavailable,
    position_unavailable,
    power_low,
    power_critical,
    message_failed,
    critical_alert_pending,
    critical_alert_failed,
    update_trial_active,
    update_transition_rejected,
    update_reboot_required,
    update_cleanup_required,
    update_safe_mode,
    update_service_required,
    update_reconciliation_required,
    position_sharing_stopped,
    position_sharing_active,
    position_sharing_waiting_for_fix,
    position_sharing_deferred,
    position_sharing_failed,
    archive_stopped,
    archive_active,
    archive_queued,
    archive_upload_waiting,
    archive_queue_full,
    archive_upload_failed,
};

// These are semantic application requests, not physical button or touch IDs.
// A target renderer chooses labels and maps its controls to the displayed slot.
enum class UiAction : std::uint8_t {
    none = 0,
    show_status,
    open_quick_status_menu,
    open_critical_confirmation,
    submit_selected_quick_status,
    confirm_critical_alert,
    cancel,
    acknowledge_notice,
    start_position_sharing,
    stop_position_sharing,
};

struct DisplayCapabilities {
    std::uint16_t width_px{0};
    std::uint16_t height_px{0};
    std::uint8_t color_depth_bits{0};
    std::uint8_t max_action_slots{0};
    bool has_touch{false};
    bool has_buttons{false};
    bool supports_hold{false};
};

// Pure validation is exposed so a target composition can reject impossible
// display/input metadata before presenting a frame or polling local input.
[[nodiscard]] bool valid_display_capabilities(
    const DisplayCapabilities& capabilities);

struct UiStatusSummary {
    UiIndicatorState radio{UiIndicatorState::unknown};
    UiIndicatorState position{UiIndicatorState::unknown};
    UiIndicatorState power{UiIndicatorState::unknown};
    bool peer_count_valid{false};
    std::uint8_t peer_count{0};
    std::uint8_t unread_messages{0};
    bool archive_queue_count_valid{false};
    std::uint8_t archive_queue_count{0};
};

struct UiActionBinding {
    UiAction action{UiAction::none};
    bool enabled{false};
};

struct UiFrame {
    std::uint32_t revision{0};
    UiScreen screen{UiScreen::home};
    UiAttention attention{UiAttention::none};
    UiNotice notice{UiNotice::none};
    UiStatusSummary status{};
    std::uint8_t action_count{0};
    std::array<UiActionBinding, kMaxUiActions> actions{};
};

class DisplaySink {
public:
    virtual ~DisplaySink() = default;

    [[nodiscard]] virtual DisplayWriteError present(const UiFrame& frame) = 0;
};

struct LocalInputEvent {
    InputReadError error{InputReadError::not_ready};
    std::uint32_t frame_revision{0};
    std::uint8_t action_slot{0};
    InputGesture gesture{InputGesture::activate};
};

class LocalInputSource {
public:
    virtual ~LocalInputSource() = default;

    [[nodiscard]] virtual LocalInputEvent read() = 0;
};

enum class PresentError : std::uint8_t {
    none = 0,
    invalid_capabilities,
    invalid_frame,
    revision_not_increasing,
    sink_not_ready,
    sink_failed,
};

struct PresentResult {
    PresentError error{PresentError::invalid_frame};
    std::uint32_t revision{0};

    [[nodiscard]] bool ok() const {
        return error == PresentError::none;
    }
};

enum class ActionResolutionError : std::uint8_t {
    none = 0,
    input_not_ready,
    input_failed,
    no_active_frame,
    stale_frame,
    invalid_slot,
    disabled_action,
    invalid_gesture,
    hold_required,
};

struct ResolvedAction {
    ActionResolutionError error{ActionResolutionError::input_not_ready};
    UiAction action{UiAction::none};
    std::uint32_t frame_revision{0};

    [[nodiscard]] bool ok() const {
        return error == ActionResolutionError::none;
    }
};

struct LocalInterfaceStatus {
    bool capabilities_valid{false};
    bool has_active_frame{false};
    std::uint32_t active_revision{0};
    std::uint32_t present_attempts{0};
    std::uint32_t presented_frames{0};
    std::uint32_t rejected_frames{0};
    std::uint32_t input_attempts{0};
    std::uint32_t resolved_actions{0};
    std::uint32_t rejected_inputs{0};
};

// Validates semantic frames before display I/O and resolves normalized local
// input only against the exact frame that was successfully presented. Revision
// zero is reserved; revisions strictly increase within one boot composition.
class CheckedLocalInterface {
public:
    CheckedLocalInterface(DisplaySink& display,
                          LocalInputSource& input,
                          DisplayCapabilities capabilities);

    [[nodiscard]] PresentResult present(const UiFrame& frame);
    [[nodiscard]] ResolvedAction poll_action();
    [[nodiscard]] LocalInterfaceStatus status() const;

private:
    DisplaySink& display_;
    LocalInputSource& input_;
    DisplayCapabilities capabilities_{};
    UiFrame active_frame_{};
    LocalInterfaceStatus status_{};
};

}  // namespace opentrail::ui
