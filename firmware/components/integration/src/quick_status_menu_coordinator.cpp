#include "opentrail/quick_status_menu_coordinator.hpp"

#include <limits>

namespace opentrail::integration {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

QuickStatusMenuMode mode_for(QuickStatusMenuPage page) {
    return page == QuickStatusMenuPage::first
               ? QuickStatusMenuMode::first_page
               : QuickStatusMenuMode::second_page;
}

bool selection_for(
    ui::UiAction action,
    protocol::QuickStatusKind& selection) {
    switch (action) {
        case ui::UiAction::select_quick_status_ok:
            selection = protocol::QuickStatusKind::ok;
            return true;
        case ui::UiAction::select_quick_status_need_assistance:
            selection = protocol::QuickStatusKind::need_assistance;
            return true;
        case ui::UiAction::select_quick_status_anyone_online:
            selection = protocol::QuickStatusKind::anyone_online;
            return true;
        case ui::UiAction::select_quick_status_available_to_help:
            selection = protocol::QuickStatusKind::available_to_help;
            return true;
        default:
            return false;
    }
}

}  // namespace

QuickStatusMenuCoordinator::QuickStatusMenuCoordinator(
    ui::CheckedLocalInterface& local_interface)
    : local_interface_(local_interface) {}

QuickStatusMenuResult QuickStatusMenuCoordinator::activate(
    std::uint32_t revision,
    const ui::UiStatusSummary& status_summary) {
    saturating_increment(status_.activations);
    QuickStatusMenuResult result{};
    if (status_.mode == QuickStatusMenuMode::faulted) {
        result.error = status_.latched_error;
        return result;
    }
    const auto local = local_interface_.status();
    if (status_.mode != QuickStatusMenuMode::inactive || revision == 0 ||
        revision == std::numeric_limits<std::uint32_t>::max() ||
        (local.has_active_frame && revision <= local.active_revision)) {
        result.error = QuickStatusMenuError::invalid_activation;
        return result;
    }

    parent_status_ = status_summary;
    if (!present(QuickStatusMenuPage::first, revision, result)) {
        return result;
    }
    status_.mode = QuickStatusMenuMode::first_page;
    result.disposition = QuickStatusMenuDisposition::presented;
    return result;
}

QuickStatusMenuResult QuickStatusMenuCoordinator::service() {
    saturating_increment(status_.service_calls);
    QuickStatusMenuResult result{};
    if (status_.mode == QuickStatusMenuMode::faulted) {
        result.error = status_.latched_error;
        return result;
    }
    if (status_.mode == QuickStatusMenuMode::inactive) {
        result.disposition = QuickStatusMenuDisposition::idle;
        return result;
    }
    if (status_.mode == QuickStatusMenuMode::transitioning) {
        if (!present(pending_page_, pending_revision_, result)) {
            return result;
        }
        status_.mode = mode_for(pending_page_);
        pending_revision_ = 0;
        saturating_increment(status_.page_changes);
        result.disposition = QuickStatusMenuDisposition::page_changed;
        return result;
    }

    result.input_polled = true;
    const auto action = local_interface_.poll_action();
    result.action_error = action.error;
    if (action.error == ui::ActionResolutionError::input_not_ready) {
        result.disposition = QuickStatusMenuDisposition::idle;
        return result;
    }
    if (!action.ok()) {
        if (action.error == ui::ActionResolutionError::input_failed) {
            result.error = QuickStatusMenuError::input_failed;
            latch(result.error);
        } else {
            result.disposition = QuickStatusMenuDisposition::input_rejected;
            saturating_increment(status_.input_rejections);
        }
        return result;
    }

    if (status_.active_revision ==
        std::numeric_limits<std::uint32_t>::max()) {
        result.error = QuickStatusMenuError::revision_exhausted;
        latch(result.error);
        return result;
    }

    if (action.action == ui::UiAction::cancel) {
        status_.mode = QuickStatusMenuMode::inactive;
        saturating_increment(status_.exit_requests);
        result.disposition = QuickStatusMenuDisposition::exit_requested;
        result.revision = status_.active_revision;
        result.minimum_parent_revision = status_.active_revision + 1U;
        return result;
    }

    if (selection_for(action.action, result.selection)) {
        status_.mode = QuickStatusMenuMode::inactive;
        saturating_increment(status_.selection_requests);
        result.disposition = QuickStatusMenuDisposition::selection_requested;
        result.has_selection = true;
        result.revision = status_.active_revision;
        result.minimum_parent_revision = status_.active_revision + 1U;
        return result;
    }

    const bool go_next =
        status_.mode == QuickStatusMenuMode::first_page &&
        action.action == ui::UiAction::show_next_quick_status_page;
    const bool go_previous =
        status_.mode == QuickStatusMenuMode::second_page &&
        action.action == ui::UiAction::show_previous_quick_status_page;
    if (!go_next && !go_previous) {
        result.error = QuickStatusMenuError::unexpected_action;
        latch(result.error);
        return result;
    }
    if (status_.active_revision >=
        std::numeric_limits<std::uint32_t>::max() - 1U) {
        result.error = QuickStatusMenuError::revision_exhausted;
        latch(result.error);
        return result;
    }

    pending_page_ = go_next ? QuickStatusMenuPage::second
                            : QuickStatusMenuPage::first;
    pending_revision_ = status_.active_revision + 1U;
    status_.mode = QuickStatusMenuMode::transitioning;
    if (!present(pending_page_, pending_revision_, result)) {
        return result;
    }
    status_.mode = mode_for(pending_page_);
    pending_revision_ = 0;
    saturating_increment(status_.page_changes);
    result.disposition = QuickStatusMenuDisposition::page_changed;
    return result;
}

QuickStatusMenuStatus QuickStatusMenuCoordinator::status() const {
    return status_;
}

ui::UiFrame QuickStatusMenuCoordinator::frame(
    QuickStatusMenuPage page,
    std::uint32_t revision) const {
    ui::UiFrame result{};
    result.revision = revision;
    result.screen = ui::UiScreen::quick_status_menu;
    result.attention = ui::UiAttention::information;
    result.status = parent_status_;
    result.action_count = 4;
    if (page == QuickStatusMenuPage::first) {
        result.actions[0] = {ui::UiAction::select_quick_status_ok, true};
        result.actions[1] = {
            ui::UiAction::select_quick_status_need_assistance,
            true,
        };
        result.actions[2] = {
            ui::UiAction::show_next_quick_status_page,
            true,
        };
    } else {
        result.actions[0] = {
            ui::UiAction::select_quick_status_anyone_online,
            true,
        };
        result.actions[1] = {
            ui::UiAction::select_quick_status_available_to_help,
            true,
        };
        result.actions[2] = {
            ui::UiAction::show_previous_quick_status_page,
            true,
        };
    }
    result.actions[3] = {ui::UiAction::cancel, true};
    return result;
}

bool QuickStatusMenuCoordinator::present(
    QuickStatusMenuPage page,
    std::uint32_t revision,
    QuickStatusMenuResult& result) {
    result.revision = revision;
    const auto presented = local_interface_.present(frame(page, revision));
    result.present_error = presented.error;
    if (!presented.ok()) {
        if (presented.error == ui::PresentError::sink_not_ready) {
            result.disposition = QuickStatusMenuDisposition::display_deferred;
        } else {
            result.disposition = QuickStatusMenuDisposition::failed;
            result.error = QuickStatusMenuError::display_failed;
            latch(result.error);
        }
        return false;
    }
    status_.active_revision = revision;
    saturating_increment(status_.presentations);
    result.frame_presented = true;
    return true;
}

void QuickStatusMenuCoordinator::latch(QuickStatusMenuError error) {
    status_.mode = QuickStatusMenuMode::faulted;
    status_.latched_error = error;
    saturating_increment(status_.failures);
}

}  // namespace opentrail::integration
