#include "opentrail/quick_status_parent_page_coordinator.hpp"

#include <limits>

namespace opentrail::integration {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace

QuickStatusParentPageCoordinator::QuickStatusParentPageCoordinator(
    ui::CheckedLocalInterface& local_interface)
    : local_interface_(local_interface), menu_(local_interface) {}

QuickStatusParentPageResult QuickStatusParentPageCoordinator::activate(
    std::uint32_t revision,
    const ui::UiStatusSummary& status_summary) {
    saturating_increment(status_.activations);
    QuickStatusParentPageResult result{};
    if (status_.mode == QuickStatusParentPageMode::faulted) {
        result.error = status_.latched_error;
        return result;
    }
    const auto local = local_interface_.status();
    if (status_.mode != QuickStatusParentPageMode::inactive ||
        revision == 0 ||
        revision >= std::numeric_limits<std::uint32_t>::max() - 3U ||
        (local.has_active_frame && revision <= local.active_revision)) {
        result.error = QuickStatusParentPageError::invalid_activation;
        return result;
    }

    parent_status_ = status_summary;
    pending_parent_revision_ = 0;
    pending_has_selection_ = false;
    if (!present_parent(revision, result)) {
        return result;
    }
    status_.mode = QuickStatusParentPageMode::parent;
    result.disposition = QuickStatusParentPageDisposition::presented;
    return result;
}

QuickStatusParentPageResult QuickStatusParentPageCoordinator::service() {
    saturating_increment(status_.service_calls);
    QuickStatusParentPageResult result{};
    if (status_.mode == QuickStatusParentPageMode::faulted) {
        result.error = status_.latched_error;
        return result;
    }
    if (status_.mode == QuickStatusParentPageMode::inactive) {
        result.disposition = QuickStatusParentPageDisposition::idle;
        return result;
    }
    if (status_.mode == QuickStatusParentPageMode::restoring_parent) {
        if (!present_parent(pending_parent_revision_, result)) {
            return result;
        }
        finish_restore(result);
        return result;
    }
    if (status_.mode == QuickStatusParentPageMode::menu) {
        result.menu_called = true;
        result.menu = menu_.service();
        if (result.menu.disposition ==
                QuickStatusMenuDisposition::selection_requested ||
            result.menu.disposition ==
                QuickStatusMenuDisposition::exit_requested) {
            pending_parent_revision_ =
                result.menu.minimum_parent_revision;
            pending_has_selection_ = result.menu.has_selection;
            pending_selection_ = result.menu.selection;
            status_.mode = QuickStatusParentPageMode::restoring_parent;
            if (!present_parent(pending_parent_revision_, result)) {
                return result;
            }
            finish_restore(result);
            return result;
        }
        if (result.menu.error != QuickStatusMenuError::none ||
            menu_.status().mode == QuickStatusMenuMode::faulted) {
            result.error = QuickStatusParentPageError::menu_failed;
            latch(result.error);
            return result;
        }
        result.disposition = QuickStatusParentPageDisposition::forwarded;
        return result;
    }

    result.input_polled = true;
    const auto action = local_interface_.poll_action();
    result.action_error = action.error;
    if (action.error == ui::ActionResolutionError::input_not_ready) {
        result.disposition = QuickStatusParentPageDisposition::idle;
        return result;
    }
    if (!action.ok()) {
        if (action.error == ui::ActionResolutionError::input_failed) {
            result.error = QuickStatusParentPageError::input_failed;
            latch(result.error);
        } else {
            result.disposition =
                QuickStatusParentPageDisposition::input_rejected;
            saturating_increment(status_.input_rejections);
        }
        return result;
    }

    if (action.action == ui::UiAction::cancel) {
        status_.mode = QuickStatusParentPageMode::inactive;
        saturating_increment(status_.exit_requests);
        result.disposition = QuickStatusParentPageDisposition::exit_requested;
        result.revision = action.frame_revision;
        return result;
    }
    if (action.action != ui::UiAction::open_quick_status_menu) {
        result.error = QuickStatusParentPageError::unexpected_action;
        latch(result.error);
        return result;
    }
    if (status_.active_revision >=
        std::numeric_limits<std::uint32_t>::max() - 3U) {
        result.error = QuickStatusParentPageError::revision_exhausted;
        latch(result.error);
        return result;
    }

    result.menu = menu_.activate(
        status_.active_revision + 1U,
        parent_status_);
    if (result.menu.disposition ==
        QuickStatusMenuDisposition::display_deferred) {
        result.disposition =
            QuickStatusParentPageDisposition::display_deferred;
        result.present_error = result.menu.present_error;
        return result;
    }
    if (result.menu.disposition != QuickStatusMenuDisposition::presented) {
        result.error = QuickStatusParentPageError::menu_failed;
        latch(result.error);
        return result;
    }
    status_.mode = QuickStatusParentPageMode::menu;
    saturating_increment(status_.menu_entries);
    result.disposition = QuickStatusParentPageDisposition::opened;
    result.frame_presented = result.menu.frame_presented;
    result.revision = result.menu.revision;
    return result;
}

QuickStatusParentPageStatus QuickStatusParentPageCoordinator::status() const {
    return status_;
}

QuickStatusMenuStatus QuickStatusParentPageCoordinator::menu_status() const {
    return menu_.status();
}

ui::UiFrame QuickStatusParentPageCoordinator::parent_frame(
    std::uint32_t revision) const {
    ui::UiFrame frame{};
    frame.revision = revision;
    frame.screen = ui::UiScreen::status;
    frame.attention = ui::UiAttention::information;
    frame.status = parent_status_;
    frame.action_count = 2;
    frame.actions[0] = {ui::UiAction::open_quick_status_menu, true};
    frame.actions[1] = {ui::UiAction::cancel, true};
    return frame;
}

bool QuickStatusParentPageCoordinator::present_parent(
    std::uint32_t revision,
    QuickStatusParentPageResult& result) {
    result.revision = revision;
    const auto presented = local_interface_.present(parent_frame(revision));
    result.present_error = presented.error;
    if (!presented.ok()) {
        if (presented.error == ui::PresentError::sink_not_ready) {
            result.disposition =
                QuickStatusParentPageDisposition::display_deferred;
        } else {
            result.disposition = QuickStatusParentPageDisposition::failed;
            result.error = QuickStatusParentPageError::display_failed;
            latch(result.error);
        }
        return false;
    }
    status_.active_revision = revision;
    saturating_increment(status_.parent_presentations);
    result.frame_presented = true;
    return true;
}

void QuickStatusParentPageCoordinator::finish_restore(
    QuickStatusParentPageResult& result) {
    pending_parent_revision_ = 0;
    status_.mode = QuickStatusParentPageMode::parent;
    saturating_increment(status_.menu_exits);
    if (pending_has_selection_) {
        result.disposition =
            QuickStatusParentPageDisposition::selection_requested;
        result.selection = pending_selection_;
        result.has_selection = true;
        pending_has_selection_ = false;
        saturating_increment(status_.selection_requests);
    } else {
        result.disposition = QuickStatusParentPageDisposition::restored;
    }
}

void QuickStatusParentPageCoordinator::latch(
    QuickStatusParentPageError error) {
    status_.mode = QuickStatusParentPageMode::faulted;
    status_.latched_error = error;
    saturating_increment(status_.failures);
}

}  // namespace opentrail::integration
