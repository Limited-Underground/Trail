#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/local_interface.hpp"

namespace opentrail::integration {

constexpr std::size_t kPortableUiMessageCapacity = 12;
constexpr std::size_t kPortableUiReadMarkerCapacity = 12;
constexpr std::uint8_t kPortableUiMessageTemplateCount = 8;

enum class PortableUiMessageDirection : std::uint8_t {
    inbound = 0,
    outbound,
};

enum class PortableUiMessageKind : std::uint8_t {
    chat = 0,
    quick_status,
    alert,
    acknowledgement,
};

enum class PortableUiMessagePriority : std::uint8_t {
    normal = 0,
    important,
    critical,
};

enum class PortableUiMessageDeliveryState : std::uint8_t {
    queued = 0,
    bridge_accepted,
    bridge_observed,
    bridge_acknowledgement_observed,
    failed,
};

struct PortableUiMessage {
    std::uint64_t sequence{0};
    PortableUiMessageDirection direction{PortableUiMessageDirection::inbound};
    PortableUiMessageKind kind{PortableUiMessageKind::chat};
    PortableUiMessagePriority priority{PortableUiMessagePriority::normal};
    PortableUiMessageDeliveryState delivery{
        PortableUiMessageDeliveryState::bridge_observed};
    ui::UiOwnedText text{};
    bool text_unavailable{false};
    bool acknowledge_available{false};
};

enum class PortableUiShellMode : std::uint8_t {
    inactive = 0,
    closed,
    home,
    status,
    quick_status_first,
    quick_status_second,
    critical_confirmation,
    archive_controls,
    archive_start_confirmation,
    archive_stop_confirmation,
    request_pending,
    request_failed,
    recovery_notice,
    system_fault,
    message_center,
    message_inbox,
    message_outbox,
    message_detail,
    message_compose,
    message_compose_confirmation,
    faulted,
};

enum class PortableUiRequestKind : std::uint8_t {
    none = 0,
    quick_status_ok,
    quick_status_need_assistance,
    quick_status_anyone_online,
    quick_status_available_to_help,
    critical_alert,
    archive_start,
    archive_stop,
    position_start,
    position_stop,
    message_template_send,
    message_alert_acknowledge,
};

enum class PortableUiPositionState : std::uint8_t {
    stopped = 0,
    active,
    waiting_for_fix,
    deferred,
    failed,
};

enum class PortableUiArchiveState : std::uint8_t {
    stopped = 0,
    active,
    queued,
    upload_waiting,
    queue_full,
    upload_failed,
    unavailable,
    incoherent,
};

struct PortableUiSnapshot {
    ui::UiStatusSummary status{};
    PortableUiPositionState position{PortableUiPositionState::stopped};
    PortableUiArchiveState archive{PortableUiArchiveState::stopped};
    bool recovery_diagnostic_valid{false};
    std::uint32_t recovery_diagnostic_word{0};
    std::uint64_t bridge_session_epoch{0};
    std::uint8_t message_count{0};
    std::array<PortableUiMessage, kPortableUiMessageCapacity> messages{};
};

enum class PortableUiShellDisposition : std::uint8_t {
    offer_ready = 0,
    committed,
    request_emitted,
    idle,
    input_rejected,
    render_rejected,
    failed,
};

enum class PortableUiShellError : std::uint8_t {
    none = 0,
    invalid_state,
    generation_mismatch,
    revision_mismatch,
    request_mismatch,
    pending_offer,
    no_pending_offer,
    revision_exhausted,
    display_failed,
    input_failed,
    unexpected_action,
};

struct PortableUiShellResult {
    PortableUiShellDisposition disposition{PortableUiShellDisposition::failed};
    PortableUiShellError error{PortableUiShellError::none};
    ui::PresentError present_error{ui::PresentError::none};
    ui::ActionResolutionError action_error{ui::ActionResolutionError::none};
    PortableUiRequestKind request_kind{PortableUiRequestKind::none};
    std::uint32_t request_id{0};
    std::uint32_t generation{0};
    std::uint32_t revision{0};
    bool has_offer{false};
    std::uint8_t request_template_id{0};
    std::uint64_t request_message_sequence{0};
    std::uint64_t request_bridge_session_epoch{0};
};

struct PortableUiShellStatus {
    PortableUiShellMode mode{PortableUiShellMode::inactive};
    PortableUiShellError latched_error{PortableUiShellError::none};
    std::uint32_t generation{0};
    std::uint32_t active_revision{0};
    std::uint32_t pending_request_id{0};
    PortableUiRequestKind pending_request_kind{PortableUiRequestKind::none};
    std::uint64_t pending_request_bridge_session_epoch{0};
    std::uint8_t pending_request_template_id{0};
    std::uint64_t pending_request_message_sequence{0};
    std::uint64_t pending_request_sequence_watermark{0};
    PortableUiArchiveState archive{PortableUiArchiveState::stopped};
    bool has_pending_offer{false};
    std::uint8_t message_page{0};
    std::uint8_t selected_template_id{0};
    std::uint64_t selected_message_sequence{0};
};

// Canonical coordinate-free root shell. Every state transition is staged as a
// UiFrame offer. State and revision advance only after the target reports that
// it rendered that exact generation/revision and CheckedLocalInterface accepts
// the same frame. Typed requests are emitted only after that commit.
class PortableUiShell {
public:
    explicit PortableUiShell(
        ui::CheckedLocalInterface& local_interface,
        std::uint32_t initial_request_id = 1);

    [[nodiscard]] PortableUiShellResult prepare_activate(
        const PortableUiSnapshot& snapshot);
    [[nodiscard]] PortableUiShellResult prepare_input();
    [[nodiscard]] PortableUiShellResult prepare_refresh(
        std::uint32_t generation,
        std::uint32_t active_revision,
        const PortableUiSnapshot& snapshot);
    [[nodiscard]] PortableUiShellResult prepare_completion(
        std::uint32_t generation,
        std::uint32_t active_revision,
        std::uint32_t request_id,
        PortableUiRequestKind request_kind,
        bool succeeded,
        std::uint64_t applied_bridge_session_epoch,
        std::uint64_t applied_message_sequence,
        std::uint8_t request_template_id,
        std::uint64_t request_message_sequence,
        const PortableUiSnapshot& snapshot);
    [[nodiscard]] PortableUiShellResult commit_present(
        std::uint32_t generation,
        std::uint32_t offered_revision);
    [[nodiscard]] PortableUiShellResult reject_present(
        std::uint32_t generation,
        std::uint32_t offered_revision);
    [[nodiscard]] PortableUiShellResult close_session(
        std::uint32_t generation,
        std::uint32_t active_revision);
    [[nodiscard]] PortableUiShellStatus status() const;
    [[nodiscard]] ui::UiFrame pending_frame() const;
    [[nodiscard]] ui::UiPresentationSidecar presentation_sidecar() const;

private:
    enum class PendingEffect : std::uint8_t {
        none = 0,
        activate,
        emit_request,
        complete_request,
        complete_request_failed,
        acknowledge_recovery,
        acknowledge_request_failure,
        mark_message_read,
    };

    struct PendingOffer {
        ui::UiFrame frame{};
        ui::UiPresentationSidecar sidecar{};
        PortableUiShellMode mode{PortableUiShellMode::inactive};
        PendingEffect effect{PendingEffect::none};
        PortableUiRequestKind request_kind{PortableUiRequestKind::none};
        std::uint32_t request_id{0};
        std::uint32_t generation{0};
        PortableUiSnapshot snapshot{};
        std::uint8_t message_page{0};
        std::uint8_t selected_template_id{0};
        std::uint64_t selected_message_sequence{0};
        bool present{false};
    };

    [[nodiscard]] ui::UiFrame frame_for(
        PortableUiShellMode mode,
        std::uint32_t revision,
        const PortableUiSnapshot& snapshot,
        PortableUiRequestKind request_kind,
        std::uint8_t message_page,
        std::uint8_t selected_template_id,
        std::uint64_t selected_message_sequence,
        ui::UiPresentationSidecar& sidecar) const;
    [[nodiscard]] PortableUiShellResult offer(
        PortableUiShellMode mode,
        const PortableUiSnapshot& snapshot,
        PendingEffect effect = PendingEffect::none,
        PortableUiRequestKind request_kind = PortableUiRequestKind::none,
        std::uint32_t request_id = 0,
        std::uint32_t generation = 0,
        std::uint8_t message_page = 0,
        std::uint8_t selected_template_id = 0,
        std::uint64_t selected_message_sequence = 0);
    [[nodiscard]] PortableUiShellResult reject(
        PortableUiShellError error) const;
    void latch(PortableUiShellError error);

    ui::CheckedLocalInterface& local_interface_;
    PortableUiShellStatus shell_status_{};
    PortableUiSnapshot snapshot_{};
    PendingOffer pending_{};
    std::uint32_t next_request_id_{1};
    std::uint32_t acknowledged_recovery_word_{0};
    PortableUiRequestKind deferred_request_failure_kind_{
        PortableUiRequestKind::none};
    bool has_acknowledged_recovery_{false};
    std::array<std::uint64_t, kPortableUiReadMarkerCapacity> read_markers_{};
    std::uint8_t read_marker_count_{0};
};

[[nodiscard]] ui::UiOwnedText portable_ui_message_template(
    std::uint8_t template_id);

}  // namespace opentrail::integration
