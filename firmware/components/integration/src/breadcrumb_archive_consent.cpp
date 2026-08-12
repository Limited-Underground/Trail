#include "opentrail/breadcrumb_archive_consent.hpp"

#include <limits>

namespace opentrail::integration {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace

BreadcrumbArchiveConsentPresentation
make_breadcrumb_archive_consent_presentation(
    BreadcrumbArchiveConsentMode mode,
    std::uint32_t revision) {
    if (revision == 0) {
        return {
            BreadcrumbArchiveConsentPresentationError::invalid_revision};
    }
    if (mode != BreadcrumbArchiveConsentMode::start &&
        mode != BreadcrumbArchiveConsentMode::stop) {
        return {BreadcrumbArchiveConsentPresentationError::invalid_mode};
    }

    ui::UiFrame frame{};
    frame.revision = revision;
    frame.screen = ui::UiScreen::archive_confirmation;
    frame.attention = ui::UiAttention::information;
    frame.action_count = 2;
    frame.actions[1] = {ui::UiAction::cancel, true};
    if (mode == BreadcrumbArchiveConsentMode::start) {
        frame.notice = ui::UiNotice::archive_start_confirmation;
        frame.actions[0] = {ui::UiAction::confirm_archive_start, true};
    } else {
        frame.notice = ui::UiNotice::archive_stop_confirmation;
        frame.actions[0] = {ui::UiAction::stop_archive, true};
    }
    return {
        BreadcrumbArchiveConsentPresentationError::none,
        frame,
    };
}

BreadcrumbArchiveConsentController::BreadcrumbArchiveConsentController(
    SerializedBreadcrumbArchiveRuntimeOwner& runtime,
    time::CheckedMonotonicClock& clock,
    std::uint64_t initial_session_id)
    : runtime_(runtime), clock_(clock) {
    status_.configuration_valid = initial_session_id != 0;
    status_.next_session_id = initial_session_id;
}

BreadcrumbArchiveConsentResult BreadcrumbArchiveConsentController::apply(
    const ui::ResolvedAction& action) {
    saturating_increment(status_.action_calls);
    BreadcrumbArchiveConsentResult result{};
    if (!status_.configuration_valid) {
        result.error =
            BreadcrumbArchiveConsentError::invalid_initial_session_id;
        saturating_increment(status_.failed);
        return result;
    }
    if (!action.ok()) {
        result.disposition = BreadcrumbArchiveConsentDisposition::rejected;
        result.error =
            BreadcrumbArchiveConsentError::invalid_resolved_action;
        saturating_increment(status_.rejected);
        return result;
    }
    result.resolved_local_action = true;

    if (action.action == ui::UiAction::cancel) {
        result.disposition = BreadcrumbArchiveConsentDisposition::cancelled;
        saturating_increment(status_.cancelled);
        return result;
    }
    if (action.action == ui::UiAction::confirm_archive_start) {
        return start();
    }
    if (action.action == ui::UiAction::stop_archive) {
        return stop();
    }

    result.disposition = BreadcrumbArchiveConsentDisposition::rejected;
    result.error = BreadcrumbArchiveConsentError::unsupported_action;
    saturating_increment(status_.rejected);
    return result;
}

BreadcrumbArchiveConsentStatus
BreadcrumbArchiveConsentController::status() const {
    return status_;
}

BreadcrumbArchiveConsentResult
BreadcrumbArchiveConsentController::start() {
    BreadcrumbArchiveConsentResult result{};
    result.resolved_local_action = true;
    if (status_.session_id_exhausted) {
        result.error = BreadcrumbArchiveConsentError::session_id_exhausted;
        saturating_increment(status_.failed);
        return result;
    }

    const auto now = clock_.now();
    result.clock_error = now.error;
    if (now.error == time::MonotonicClockError::not_ready) {
        result.disposition = BreadcrumbArchiveConsentDisposition::deferred;
        result.error = BreadcrumbArchiveConsentError::clock_not_ready;
        saturating_increment(status_.deferred);
        return result;
    }
    if (!now.ok()) {
        result.error = BreadcrumbArchiveConsentError::clock_failed;
        saturating_increment(status_.failed);
        return result;
    }

    result.session_id = status_.next_session_id;
    result.runtime = runtime_.start_capture(result.session_id, now.value_ms);
    if (result.runtime.disposition ==
        BreadcrumbArchiveRuntimeDisposition::deferred) {
        result.disposition = BreadcrumbArchiveConsentDisposition::deferred;
        result.error = BreadcrumbArchiveConsentError::runtime_deferred;
        saturating_increment(status_.deferred);
        return result;
    }
    if (!result.runtime.completed()) {
        result.error = BreadcrumbArchiveConsentError::runtime_failed;
        if (result.runtime.operation_attempted &&
            result.runtime.outcome_uncertain) {
            consume_session_id(result);
        }
        saturating_increment(status_.failed);
        return result;
    }
    if (!result.runtime.start.started()) {
        result.disposition = BreadcrumbArchiveConsentDisposition::rejected;
        result.error = BreadcrumbArchiveConsentError::start_rejected;
        saturating_increment(status_.rejected);
        return result;
    }

    consume_session_id(result);
    result.disposition = BreadcrumbArchiveConsentDisposition::started;
    saturating_increment(status_.started);
    return result;
}

BreadcrumbArchiveConsentResult
BreadcrumbArchiveConsentController::stop() {
    BreadcrumbArchiveConsentResult result{};
    result.resolved_local_action = true;
    result.runtime = runtime_.stop_capture();
    if (result.runtime.disposition ==
        BreadcrumbArchiveRuntimeDisposition::deferred) {
        result.disposition = BreadcrumbArchiveConsentDisposition::deferred;
        result.error = BreadcrumbArchiveConsentError::runtime_deferred;
        saturating_increment(status_.deferred);
        return result;
    }
    if (!result.runtime.completed()) {
        result.error = BreadcrumbArchiveConsentError::runtime_failed;
        saturating_increment(status_.failed);
        return result;
    }
    result.disposition = BreadcrumbArchiveConsentDisposition::stopped;
    saturating_increment(status_.stopped);
    return result;
}

void BreadcrumbArchiveConsentController::consume_session_id(
    BreadcrumbArchiveConsentResult& result) {
    result.session_id_consumed = true;
    if (status_.next_session_id ==
        std::numeric_limits<std::uint64_t>::max()) {
        status_.session_id_exhausted = true;
    } else {
        ++status_.next_session_id;
    }
}

}  // namespace opentrail::integration
