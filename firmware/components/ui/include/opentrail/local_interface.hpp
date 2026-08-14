#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::ui {

constexpr std::size_t kMaxUiActions = 4;
constexpr std::size_t kMaxUiOwnedTexts = 4;
constexpr std::size_t kMaxUiOwnedTextBytes = 96;
constexpr std::size_t kMaxUiMessageRows = 2;
constexpr std::uint8_t kNoUiOwnedText = 0xFF;

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
    archive_controls,
    archive_confirmation,
    system_fault,
    message_center,
    message_list,
    message_detail,
    message_compose,
    message_compose_confirmation,
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
    archive_start_confirmation,
    archive_stop_confirmation,
};

// These are semantic application requests, not physical button or touch IDs.
// A target renderer chooses labels and maps its controls to the displayed slot.
enum class UiAction : std::uint8_t {
    none = 0,
    show_status,
    open_quick_status_menu,
    open_critical_confirmation,
    submit_selected_quick_status,
    select_quick_status_ok,
    select_quick_status_need_assistance,
    select_quick_status_anyone_online,
    select_quick_status_available_to_help,
    show_next_quick_status_page,
    show_previous_quick_status_page,
    confirm_critical_alert,
    cancel,
    acknowledge_notice,
    start_position_sharing,
    stop_position_sharing,
    open_archive_controls,
    request_archive_start,
    request_archive_stop,
    confirm_archive_start,
    stop_archive,
    open_messages,
    open_inbox,
    open_outbox,
    open_compose,
    open_message_row_1,
    open_message_row_2,
    show_next_message_page,
    select_message_template_1,
    select_message_template_2,
    show_next_compose_page,
    send_composed_message,
    acknowledge_inbound_alert,
};

enum class UiMessageListKind : std::uint8_t {
    none = 0,
    inbox,
    outbox,
};

enum class UiMessageDeliveryState : std::uint8_t {
    none = 0,
    queued,
    bridge_accepted,
    bridge_observed,
    bridge_acknowledgement_observed,
    failed,
};

enum class UiMessageKind : std::uint8_t {
    none = 0,
    chat,
    quick_status,
    alert,
    acknowledgement,
};

enum class UiMessagePriority : std::uint8_t {
    none = 0,
    normal,
    important,
    critical,
};

struct UiOwnedText {
    std::uint8_t byte_count{0};
    bool truncated{false};
    bool unavailable{false};
    std::array<char, kMaxUiOwnedTextBytes + 1> bytes{};
};

struct UiMessageRowPresentation {
    std::uint8_t text_index{kNoUiOwnedText};
    UiMessageDeliveryState delivery{UiMessageDeliveryState::none};
    UiMessageKind kind{UiMessageKind::none};
    UiMessagePriority priority{UiMessagePriority::none};
    bool unread{false};
};

struct UiMessagePresentation {
    UiMessageListKind list_kind{UiMessageListKind::none};
    std::uint8_t page_index{0};
    std::uint8_t page_count{0};
    std::uint8_t row_count{0};
    std::array<UiMessageRowPresentation, kMaxUiMessageRows> rows{};
    bool detail_valid{false};
    UiMessageDeliveryState detail_delivery{UiMessageDeliveryState::none};
    UiMessageKind detail_kind{UiMessageKind::none};
    UiMessagePriority detail_priority{UiMessagePriority::none};
    bool detail_unread{false};
    bool detail_acknowledge_available{false};
    std::uint8_t compose_template_id{0};
};

// Message copy and pagination remain bounded but are intentionally kept out of
// UiFrame so legacy presentation/result objects retain their embedded ABI and
// memory budget. PortableUiShell owns this sidecar for the exact offered frame.
struct UiPresentationSidecar {
    std::uint8_t owned_text_count{0};
    std::array<UiOwnedText, kMaxUiOwnedTexts> owned_texts{};
    UiMessagePresentation messages{};
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

struct UiFrame;

// Pure validation is exposed so a target composition can reject impossible
// display/input metadata before presenting a frame or polling local input.
[[nodiscard]] bool valid_display_capabilities(
    const DisplayCapabilities& capabilities);

// Public pure validation lets host and target render-plan adapters reject a
// candidate before drawing it. CheckedLocalInterface remains the authority
// that commits a validated frame revision after display success.
[[nodiscard]] bool valid_ui_frame(
    const UiFrame& frame,
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

static_assert(sizeof(UiFrame) == 24,
              "UiFrame must preserve the compact embedded presentation ABI.");

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
    [[nodiscard]] PresentError validate_candidate(const UiFrame& frame) const;
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
