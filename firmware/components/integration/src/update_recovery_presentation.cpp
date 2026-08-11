#include "opentrail/update_recovery_presentation.hpp"

#include "opentrail/update_recovery_diagnostics.hpp"

namespace opentrail::integration {
namespace {

ui::UiFrame base_frame(std::uint32_t revision) {
    ui::UiFrame frame{};
    frame.revision = revision;
    frame.screen = ui::UiScreen::status;
    return frame;
}

void set_acknowledge_notice(ui::UiFrame& frame) {
    frame.action_count = 1;
    frame.actions[0] = {ui::UiAction::acknowledge_notice, true};
}

ui::UiFrame service_fallback(std::uint32_t revision) {
    auto frame = base_frame(revision);
    frame.screen = ui::UiScreen::system_fault;
    frame.attention = ui::UiAttention::critical;
    frame.notice = ui::UiNotice::update_service_required;
    return frame;
}

}  // namespace

UpdateRecoveryPresentationResult make_update_recovery_presentation(
    std::uint32_t diagnostic_word,
    std::uint32_t frame_revision) {
    if (frame_revision == 0) {
        return {UpdateRecoveryPresentationError::invalid_revision};
    }

    const auto decoded =
        diagnostics::decode_update_recovery_diagnostic(diagnostic_word);
    if (!decoded.decoded()) {
        return {
            UpdateRecoveryPresentationError::invalid_diagnostic,
            service_fallback(frame_revision),
            true,
        };
    }

    auto frame = base_frame(frame_revision);
    using State = update::UpdateRecoveryOperatorState;
    switch (decoded.diagnostic.state) {
        case State::operational:
        case State::persistence_committed:
            break;
        case State::trial_active:
            frame.attention = ui::UiAttention::warning;
            frame.notice = ui::UiNotice::update_trial_active;
            set_acknowledge_notice(frame);
            break;
        case State::transition_rejected:
            frame.attention = ui::UiAttention::warning;
            frame.notice = ui::UiNotice::update_transition_rejected;
            set_acknowledge_notice(frame);
            break;
        case State::rollback_required:
            frame.screen = ui::UiScreen::system_fault;
            frame.attention = ui::UiAttention::critical;
            frame.notice = ui::UiNotice::update_reboot_required;
            break;
        case State::cleanup_required:
            frame.attention = ui::UiAttention::warning;
            frame.notice = ui::UiNotice::update_cleanup_required;
            set_acknowledge_notice(frame);
            break;
        case State::safe_mode:
            frame.screen = ui::UiScreen::system_fault;
            frame.attention = ui::UiAttention::critical;
            frame.notice = ui::UiNotice::update_safe_mode;
            break;
        case State::service_required:
            frame.screen = ui::UiScreen::system_fault;
            frame.attention = ui::UiAttention::critical;
            frame.notice = ui::UiNotice::update_service_required;
            break;
        case State::reboot_reconcile_required:
            frame.screen = ui::UiScreen::system_fault;
            frame.attention = ui::UiAttention::critical;
            frame.notice = ui::UiNotice::update_reconciliation_required;
            break;
        default:
            return {
                UpdateRecoveryPresentationError::invalid_diagnostic,
                service_fallback(frame_revision),
                true,
            };
    }
    return {UpdateRecoveryPresentationError::none, frame, true};
}

}  // namespace opentrail::integration
