#include "opentrail/breadcrumb_archive_workflow_coordinator.hpp"

#include <cstddef>
#include <limits>

namespace opentrail::integration {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

bool same_status(const ui::UiStatusSummary& left,
                 const ui::UiStatusSummary& right) {
    return left.radio == right.radio &&
           left.position == right.position &&
           left.power == right.power &&
           left.peer_count_valid == right.peer_count_valid &&
           left.peer_count == right.peer_count &&
           left.unread_messages == right.unread_messages &&
           left.archive_queue_count_valid ==
               right.archive_queue_count_valid &&
           left.archive_queue_count == right.archive_queue_count;
}

bool same_semantics(const ui::UiFrame& left, const ui::UiFrame& right) {
    if (left.screen != right.screen ||
        left.attention != right.attention ||
        left.notice != right.notice ||
        !same_status(left.status, right.status) ||
        left.action_count != right.action_count) {
        return false;
    }
    for (std::size_t index = 0; index < ui::kMaxUiActions; ++index) {
        if (left.actions[index].action != right.actions[index].action ||
            left.actions[index].enabled != right.actions[index].enabled) {
            return false;
        }
    }
    return true;
}

}  // namespace

BreadcrumbArchiveWorkflowCoordinator::
BreadcrumbArchiveWorkflowCoordinator(
    SerializedBreadcrumbArchiveRuntimeOwner& runtime,
    time::CheckedMonotonicClock& clock,
    ui::CheckedLocalInterface& local_interface,
    std::uint64_t initial_session_id,
    std::uint32_t initial_revision)
    : runtime_(runtime),
      local_interface_(local_interface),
      consent_(runtime, clock, initial_session_id) {
    status_.next_revision = initial_revision;
    if (initial_revision == 0 ||
        initial_revision == std::numeric_limits<std::uint32_t>::max()) {
        latch(BreadcrumbArchiveWorkflowError::invalid_initial_revision);
    } else if (!consent_.status().configuration_valid) {
        latch(BreadcrumbArchiveWorkflowError::invalid_initial_session_id);
    } else {
        status_.configuration_valid = true;
    }
}

BreadcrumbArchiveWorkflowResult
BreadcrumbArchiveWorkflowCoordinator::enter(
    const ui::ResolvedAction& action) {
    BreadcrumbArchiveWorkflowResult result{};
    if (status_.faulted) {
        result.error = status_.latched_error;
        return result;
    }

    const auto local_status = local_interface_.status();
    if (status_.mode != BreadcrumbArchiveWorkflowMode::closed ||
        !action.ok() ||
        action.action != ui::UiAction::open_archive_controls ||
        !local_status.has_active_frame ||
        action.frame_revision != local_status.active_revision) {
        result.disposition =
            BreadcrumbArchiveWorkflowDisposition::input_rejected;
        result.error = BreadcrumbArchiveWorkflowError::invalid_entry_action;
        saturating_increment(status_.input_rejections);
        return result;
    }
    if (action.frame_revision >=
        std::numeric_limits<std::uint32_t>::max() - 1) {
        result.error = BreadcrumbArchiveWorkflowError::revision_exhausted;
        latch(result.error);
        saturating_increment(status_.failures);
        return result;
    }

    status_.mode = BreadcrumbArchiveWorkflowMode::controls;
    status_.has_presented_frame = false;
    status_.active_revision = action.frame_revision;
    status_.next_revision = action.frame_revision + 1;
    result.disposition = BreadcrumbArchiveWorkflowDisposition::entered;
    result.revision = action.frame_revision;
    return result;
}

BreadcrumbArchiveWorkflowResult
BreadcrumbArchiveWorkflowCoordinator::service() {
    saturating_increment(status_.service_calls);
    BreadcrumbArchiveWorkflowResult result{};
    if (status_.faulted) {
        result.error = status_.latched_error;
        return result;
    }
    if (status_.mode == BreadcrumbArchiveWorkflowMode::closed) {
        result.disposition =
            BreadcrumbArchiveWorkflowDisposition::exit_requested;
        result.revision = status_.active_revision;
        return result;
    }

    if (!status_.has_presented_frame) {
        const auto controls = current_controls();
        result.snapshot_read = controls.snapshot_read;
        result.presentation_error = controls.error;
        if (!controls.has_safe_frame) {
            if (controls.error ==
                BreadcrumbArchivePresentationError::snapshot_not_ready) {
                result.disposition =
                    BreadcrumbArchiveWorkflowDisposition::snapshot_deferred;
                return result;
            }
            result.error =
                BreadcrumbArchiveWorkflowError::presentation_unavailable;
            saturating_increment(status_.failures);
            return result;
        }
        if (publish_controls(controls, result)) {
            result.disposition =
                BreadcrumbArchiveWorkflowDisposition::presented;
        }
        return result;
    }

    // Reserve one revision after every actionable frame. If the sequence can
    // no longer show the result of an action, fail privacy-safe by stopping.
    if (status_.next_revision ==
        std::numeric_limits<std::uint32_t>::max()) {
        contain_and_latch(
            BreadcrumbArchiveWorkflowError::revision_exhausted, result);
        if (result.error !=
            BreadcrumbArchiveWorkflowError::containment_failed) {
            result.error = BreadcrumbArchiveWorkflowError::revision_exhausted;
        }
        saturating_increment(status_.failures);
        return result;
    }

    if (status_.mode == BreadcrumbArchiveWorkflowMode::controls) {
        const auto controls = current_controls();
        result.snapshot_read = controls.snapshot_read;
        result.presentation_error = controls.error;
        if (!controls.has_safe_frame) {
            if (controls.error ==
                BreadcrumbArchivePresentationError::snapshot_not_ready) {
                result.disposition =
                    BreadcrumbArchiveWorkflowDisposition::snapshot_deferred;
                return result;
            }
            result.error =
                BreadcrumbArchiveWorkflowError::presentation_unavailable;
            saturating_increment(status_.failures);
            return result;
        }
        if (semantics_changed(controls.frame)) {
            if (publish_controls(controls, result)) {
                result.disposition =
                    BreadcrumbArchiveWorkflowDisposition::refreshed;
            }
            return result;
        }
    }

    const auto action = local_interface_.poll_action();
    result.action_error = action.error;
    if (action.error == ui::ActionResolutionError::input_not_ready) {
        result.disposition = BreadcrumbArchiveWorkflowDisposition::idle;
        return result;
    }
    if (!action.ok()) {
        saturating_increment(status_.input_rejections);
        if (action.error == ui::ActionResolutionError::input_failed) {
            result.error = BreadcrumbArchiveWorkflowError::input_failed;
            result.disposition = BreadcrumbArchiveWorkflowDisposition::failed;
            saturating_increment(status_.failures);
        } else {
            result.disposition =
                BreadcrumbArchiveWorkflowDisposition::input_rejected;
        }
        return result;
    }
    saturating_increment(status_.resolved_actions);

    if (status_.mode == BreadcrumbArchiveWorkflowMode::controls) {
        if (action.action == ui::UiAction::cancel) {
            status_.mode = BreadcrumbArchiveWorkflowMode::closed;
            result.disposition =
                BreadcrumbArchiveWorkflowDisposition::exit_requested;
            result.revision = status_.active_revision;
            return result;
        }

        BreadcrumbArchiveConsentMode consent_mode{};
        BreadcrumbArchiveWorkflowMode workflow_mode{};
        if (action.action == ui::UiAction::request_archive_start) {
            consent_mode = BreadcrumbArchiveConsentMode::start;
            workflow_mode =
                BreadcrumbArchiveWorkflowMode::start_confirmation;
        } else if (action.action == ui::UiAction::request_archive_stop) {
            consent_mode = BreadcrumbArchiveConsentMode::stop;
            workflow_mode =
                BreadcrumbArchiveWorkflowMode::stop_confirmation;
        } else {
            result.disposition =
                BreadcrumbArchiveWorkflowDisposition::input_rejected;
            result.error = BreadcrumbArchiveWorkflowError::unexpected_action;
            saturating_increment(status_.input_rejections);
            return result;
        }

        const auto confirmation =
            make_breadcrumb_archive_consent_presentation(
                consent_mode, status_.next_revision);
        result.presentation_error = confirmation.presentable()
                                        ? BreadcrumbArchivePresentationError::none
                                        : BreadcrumbArchivePresentationError::invalid_revision;
        if (!confirmation.presentable()) {
            result.error =
                BreadcrumbArchiveWorkflowError::presentation_unavailable;
            saturating_increment(status_.failures);
            return result;
        }
        if (publish(confirmation.frame, result)) {
            status_.mode = workflow_mode;
            result.disposition =
                BreadcrumbArchiveWorkflowDisposition::confirmation_presented;
        }
        return result;
    }

    const bool start_confirmation =
        status_.mode == BreadcrumbArchiveWorkflowMode::start_confirmation;
    const auto expected = start_confirmation
                              ? ui::UiAction::confirm_archive_start
                              : ui::UiAction::stop_archive;
    if (action.action != expected && action.action != ui::UiAction::cancel) {
        result.disposition =
            BreadcrumbArchiveWorkflowDisposition::input_rejected;
        result.error = BreadcrumbArchiveWorkflowError::unexpected_action;
        saturating_increment(status_.input_rejections);
        return result;
    }

    if (action.action == ui::UiAction::cancel) {
        const auto controls = current_controls();
        result.snapshot_read = controls.snapshot_read;
        result.presentation_error = controls.error;
        if (!controls.has_safe_frame) {
            result.disposition = controls.error ==
                                         BreadcrumbArchivePresentationError::snapshot_not_ready
                                     ? BreadcrumbArchiveWorkflowDisposition::snapshot_deferred
                                     : BreadcrumbArchiveWorkflowDisposition::failed;
            if (result.disposition ==
                BreadcrumbArchiveWorkflowDisposition::failed) {
                result.error =
                    BreadcrumbArchiveWorkflowError::presentation_unavailable;
                saturating_increment(status_.failures);
            }
            return result;
        }
        if (publish_controls(controls, result)) {
            result.disposition =
                BreadcrumbArchiveWorkflowDisposition::cancelled;
        }
        return result;
    }

    const auto consent = consent_.apply(action);
    result.consent_disposition = consent.disposition;
    result.consent_error = consent.error;
    if (consent.disposition == BreadcrumbArchiveConsentDisposition::deferred) {
        result.disposition =
            BreadcrumbArchiveWorkflowDisposition::action_deferred;
        saturating_increment(status_.actions_deferred);
        return result;
    }

    const auto controls = current_controls();
    result.snapshot_read = controls.snapshot_read;
    result.presentation_error = controls.error;
    if (!controls.has_safe_frame || !publish_controls(controls, result)) {
        if (consent.disposition ==
            BreadcrumbArchiveConsentDisposition::started) {
            contain_and_latch(
                BreadcrumbArchiveWorkflowError::post_action_refresh_failed,
                result);
        } else {
            latch(BreadcrumbArchiveWorkflowError::post_action_refresh_failed);
        }
        if (result.error !=
            BreadcrumbArchiveWorkflowError::containment_failed) {
            result.error =
                BreadcrumbArchiveWorkflowError::post_action_refresh_failed;
        }
        result.disposition = BreadcrumbArchiveWorkflowDisposition::failed;
        saturating_increment(status_.failures);
        return result;
    }

    if (consent.disposition == BreadcrumbArchiveConsentDisposition::started ||
        consent.disposition == BreadcrumbArchiveConsentDisposition::stopped) {
        result.disposition =
            BreadcrumbArchiveWorkflowDisposition::action_applied;
        result.state_changed = true;
        saturating_increment(status_.actions_applied);
    } else if (consent.disposition ==
               BreadcrumbArchiveConsentDisposition::cancelled) {
        result.disposition = BreadcrumbArchiveWorkflowDisposition::cancelled;
    } else if (consent.disposition ==
               BreadcrumbArchiveConsentDisposition::failed) {
        result.disposition = BreadcrumbArchiveWorkflowDisposition::failed;
        result.error = BreadcrumbArchiveWorkflowError::consent_failed;
        saturating_increment(status_.failures);
    } else {
        result.disposition =
            BreadcrumbArchiveWorkflowDisposition::action_rejected;
    }
    return result;
}

BreadcrumbArchiveWorkflowStatus
BreadcrumbArchiveWorkflowCoordinator::status() const {
    return status_;
}

BreadcrumbArchiveConsentStatus
BreadcrumbArchiveWorkflowCoordinator::consent_status() const {
    return consent_.status();
}

BreadcrumbArchiveWorkflowCoordinator::ControlsPresentation
BreadcrumbArchiveWorkflowCoordinator::current_controls() {
    ControlsPresentation result{};
    result.snapshot_read = true;
    saturating_increment(status_.snapshot_reads);

    BreadcrumbArchiveRuntimeSnapshot snapshot{};
    const auto state = runtime_.snapshot(snapshot);
    bool state_known = false;
    bool active = false;
    BreadcrumbArchivePresentationResult presentation{};
    if (state == BreadcrumbArchiveSnapshotState::ready) {
        presentation = make_breadcrumb_archive_presentation(
            snapshot.session,
            snapshot.outbox,
            snapshot.retry,
            status_.next_revision);
        state_known = presentation.error ==
                      BreadcrumbArchivePresentationError::none;
        active = state_known && snapshot.session.active;
    } else if (state == BreadcrumbArchiveSnapshotState::not_ready) {
        result.error = BreadcrumbArchivePresentationError::snapshot_not_ready;
        return result;
    } else {
        location::BreadcrumbArchiveStatus safe_session{};
        location::BreadcrumbArchiveOutboxStatus safe_outbox{};
        location::BreadcrumbArchiveRetryStatus safe_retry{};
        presentation = make_breadcrumb_archive_presentation(
            safe_session,
            safe_outbox,
            safe_retry,
            status_.next_revision);
        presentation.error =
            state == BreadcrumbArchiveSnapshotState::failed
                ? BreadcrumbArchivePresentationError::snapshot_failed
                : BreadcrumbArchivePresentationError::invalid_snapshot_state;
    }

    result.error = presentation.error;
    result.has_safe_frame = presentation.presentable();
    if (!result.has_safe_frame) {
        return result;
    }

    result.frame = presentation.frame;
    result.frame.screen = ui::UiScreen::archive_controls;
    result.frame.action_count = 2;
    result.frame.actions[0] = {
        state_known && !active
            ? ui::UiAction::request_archive_start
            : ui::UiAction::request_archive_stop,
        true,
    };
    result.frame.actions[1] = {ui::UiAction::cancel, true};
    return result;
}

bool BreadcrumbArchiveWorkflowCoordinator::semantics_changed(
    const ui::UiFrame& candidate) const {
    return !same_semantics(active_frame_, candidate);
}

bool BreadcrumbArchiveWorkflowCoordinator::publish(
    const ui::UiFrame& frame,
    BreadcrumbArchiveWorkflowResult& result) {
    result.revision = status_.next_revision;
    const auto presented = local_interface_.present(frame);
    result.present_error = presented.error;
    if (!presented.ok()) {
        result.disposition =
            presented.error == ui::PresentError::sink_not_ready
                ? BreadcrumbArchiveWorkflowDisposition::display_deferred
                : BreadcrumbArchiveWorkflowDisposition::failed;
        if (result.disposition ==
            BreadcrumbArchiveWorkflowDisposition::failed) {
            result.error = BreadcrumbArchiveWorkflowError::display_failed;
            saturating_increment(status_.failures);
        }
        return false;
    }

    active_frame_ = frame;
    status_.has_presented_frame = true;
    status_.active_revision = status_.next_revision;
    ++status_.next_revision;
    saturating_increment(status_.presented_frames);
    result.frame_presented = true;
    return true;
}

bool BreadcrumbArchiveWorkflowCoordinator::publish_controls(
    const ControlsPresentation& presentation,
    BreadcrumbArchiveWorkflowResult& result) {
    if (!presentation.has_safe_frame) {
        result.error =
            BreadcrumbArchiveWorkflowError::presentation_unavailable;
        return false;
    }
    if (!publish(presentation.frame, result)) {
        return false;
    }
    status_.mode = BreadcrumbArchiveWorkflowMode::controls;
    return true;
}

void BreadcrumbArchiveWorkflowCoordinator::contain_and_latch(
    BreadcrumbArchiveWorkflowError error,
    BreadcrumbArchiveWorkflowResult& result) {
    result.containment_attempted = true;
    result.containment = runtime_.stop_capture();
    if (!result.containment.completed()) {
        result.error = BreadcrumbArchiveWorkflowError::containment_failed;
    }
    latch(error);
}

void BreadcrumbArchiveWorkflowCoordinator::latch(
    BreadcrumbArchiveWorkflowError error) {
    status_.faulted = true;
    status_.mode = BreadcrumbArchiveWorkflowMode::faulted;
    status_.latched_error = error;
}

}  // namespace opentrail::integration
