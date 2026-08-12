#pragma once

#include <cstdint>

#include "opentrail/breadcrumb_archive_consent.hpp"

namespace opentrail::integration {

enum class BreadcrumbArchiveWorkflowMode : std::uint8_t {
    controls = 0,
    start_confirmation,
    stop_confirmation,
    closed,
    faulted,
};

enum class BreadcrumbArchiveWorkflowDisposition : std::uint8_t {
    presented = 0,
    refreshed,
    idle,
    confirmation_presented,
    cancelled,
    exit_requested,
    action_applied,
    action_deferred,
    action_rejected,
    input_rejected,
    entered,
    snapshot_deferred,
    display_deferred,
    failed,
};

enum class BreadcrumbArchiveWorkflowError : std::uint8_t {
    none = 0,
    invalid_initial_revision,
    invalid_initial_session_id,
    revision_exhausted,
    presentation_unavailable,
    display_failed,
    input_failed,
    invalid_entry_action,
    unexpected_action,
    consent_failed,
    post_action_refresh_failed,
    containment_failed,
};

struct BreadcrumbArchiveWorkflowResult {
    BreadcrumbArchiveWorkflowDisposition disposition{
        BreadcrumbArchiveWorkflowDisposition::failed};
    BreadcrumbArchiveWorkflowError error{
        BreadcrumbArchiveWorkflowError::none};
    BreadcrumbArchivePresentationError presentation_error{
        BreadcrumbArchivePresentationError::none};
    ui::PresentError present_error{ui::PresentError::none};
    ui::ActionResolutionError action_error{
        ui::ActionResolutionError::none};
    BreadcrumbArchiveConsentDisposition consent_disposition{
        BreadcrumbArchiveConsentDisposition::failed};
    BreadcrumbArchiveConsentError consent_error{
        BreadcrumbArchiveConsentError::none};
    BreadcrumbArchiveRuntimeResult containment{};
    std::uint32_t revision{0};
    bool snapshot_read{false};
    bool frame_presented{false};
    bool state_changed{false};
    bool containment_attempted{false};
};

struct BreadcrumbArchiveWorkflowStatus {
    bool configuration_valid{false};
    bool has_presented_frame{false};
    bool faulted{false};
    BreadcrumbArchiveWorkflowMode mode{
        BreadcrumbArchiveWorkflowMode::controls};
    BreadcrumbArchiveWorkflowError latched_error{
        BreadcrumbArchiveWorkflowError::none};
    std::uint32_t active_revision{0};
    std::uint32_t next_revision{0};
    std::uint32_t service_calls{0};
    std::uint32_t snapshot_reads{0};
    std::uint32_t presented_frames{0};
    std::uint32_t resolved_actions{0};
    std::uint32_t actions_applied{0};
    std::uint32_t actions_deferred{0};
    std::uint32_t input_rejections{0};
    std::uint32_t failures{0};
};

// Cooperative owner for the complete local archive-control sequence. The
// consent controller stays private to this object and the runtime reference is
// not exposed by it. A target shell may re-enter only with
// open_archive_controls already resolved against the exact active parent
// frame, and must replace the workflow frame after exit_requested. There is no
// radio, network, or automatic Start input.
class BreadcrumbArchiveWorkflowCoordinator {
public:
    BreadcrumbArchiveWorkflowCoordinator(
        SerializedBreadcrumbArchiveRuntimeOwner& runtime,
        time::CheckedMonotonicClock& clock,
        ui::CheckedLocalInterface& local_interface,
        std::uint64_t initial_session_id,
        std::uint64_t final_session_id,
        std::uint32_t initial_revision = 1);

    [[nodiscard]] BreadcrumbArchiveWorkflowResult enter(
        const ui::ResolvedAction& action);
    [[nodiscard]] BreadcrumbArchiveWorkflowResult service();
    [[nodiscard]] BreadcrumbArchiveWorkflowStatus status() const;
    [[nodiscard]] BreadcrumbArchiveConsentStatus consent_status() const;

private:
    struct ControlsPresentation {
        BreadcrumbArchivePresentationError error{
            BreadcrumbArchivePresentationError::incoherent_status};
        ui::UiFrame frame{};
        bool has_safe_frame{false};
        bool snapshot_read{false};
    };

    [[nodiscard]] ControlsPresentation current_controls();
    [[nodiscard]] bool semantics_changed(const ui::UiFrame& candidate) const;
    [[nodiscard]] bool publish(
        const ui::UiFrame& frame,
        BreadcrumbArchiveWorkflowResult& result);
    [[nodiscard]] bool publish_controls(
        const ControlsPresentation& presentation,
        BreadcrumbArchiveWorkflowResult& result);
    void contain_and_latch(
        BreadcrumbArchiveWorkflowError error,
        BreadcrumbArchiveWorkflowResult& result);
    void latch(BreadcrumbArchiveWorkflowError error);

    SerializedBreadcrumbArchiveRuntimeOwner& runtime_;
    ui::CheckedLocalInterface& local_interface_;
    BreadcrumbArchiveConsentController consent_;
    BreadcrumbArchiveWorkflowStatus status_{};
    ui::UiFrame active_frame_{};
};

}  // namespace opentrail::integration
