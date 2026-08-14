#include "opentrail/local_interface.hpp"

#include <limits>

namespace opentrail::ui {
bool valid_display_capabilities(const DisplayCapabilities& capabilities) {
    return capabilities.width_px != 0 && capabilities.height_px != 0 &&
           capabilities.color_depth_bits != 0 &&
           capabilities.color_depth_bits <= 32 &&
           capabilities.max_action_slots != 0 &&
           capabilities.max_action_slots <= kMaxUiActions &&
           (capabilities.has_touch || capabilities.has_buttons);
}

namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

bool valid_screen(UiScreen screen) {
    return screen == UiScreen::home || screen == UiScreen::status ||
           screen == UiScreen::quick_status_menu ||
           screen == UiScreen::critical_confirmation ||
           screen == UiScreen::archive_controls ||
           screen == UiScreen::archive_confirmation ||
           screen == UiScreen::system_fault ||
           screen == UiScreen::message_center ||
           screen == UiScreen::message_list ||
           screen == UiScreen::message_detail ||
           screen == UiScreen::message_compose ||
           screen == UiScreen::message_compose_confirmation;
}

bool valid_attention(UiAttention attention) {
    return attention == UiAttention::none ||
           attention == UiAttention::information ||
           attention == UiAttention::warning ||
           attention == UiAttention::critical;
}

bool valid_indicator(UiIndicatorState indicator) {
    return indicator == UiIndicatorState::unknown ||
           indicator == UiIndicatorState::unavailable ||
           indicator == UiIndicatorState::normal ||
           indicator == UiIndicatorState::warning ||
           indicator == UiIndicatorState::critical;
}

bool valid_notice(UiNotice notice) {
    return notice == UiNotice::none || notice == UiNotice::radio_unavailable ||
           notice == UiNotice::position_unavailable ||
           notice == UiNotice::power_low || notice == UiNotice::power_critical ||
           notice == UiNotice::message_failed ||
           notice == UiNotice::critical_alert_pending ||
           notice == UiNotice::critical_alert_failed ||
           notice == UiNotice::update_trial_active ||
           notice == UiNotice::update_transition_rejected ||
           notice == UiNotice::update_reboot_required ||
           notice == UiNotice::update_cleanup_required ||
           notice == UiNotice::update_safe_mode ||
           notice == UiNotice::update_service_required ||
           notice == UiNotice::update_reconciliation_required ||
           notice == UiNotice::position_sharing_stopped ||
           notice == UiNotice::position_sharing_active ||
           notice == UiNotice::position_sharing_waiting_for_fix ||
           notice == UiNotice::position_sharing_deferred ||
           notice == UiNotice::position_sharing_failed ||
           notice == UiNotice::archive_stopped ||
           notice == UiNotice::archive_active ||
           notice == UiNotice::archive_queued ||
           notice == UiNotice::archive_upload_waiting ||
           notice == UiNotice::archive_queue_full ||
           notice == UiNotice::archive_upload_failed ||
           notice == UiNotice::archive_start_confirmation ||
           notice == UiNotice::archive_stop_confirmation;
}

bool valid_action(UiAction action) {
    return action == UiAction::show_status ||
           action == UiAction::open_quick_status_menu ||
           action == UiAction::open_critical_confirmation ||
           action == UiAction::select_quick_status_ok ||
           action == UiAction::select_quick_status_need_assistance ||
           action == UiAction::select_quick_status_anyone_online ||
           action == UiAction::select_quick_status_available_to_help ||
           action == UiAction::show_next_quick_status_page ||
           action == UiAction::show_previous_quick_status_page ||
           action == UiAction::confirm_critical_alert ||
           action == UiAction::cancel ||
           action == UiAction::acknowledge_notice ||
           action == UiAction::start_position_sharing ||
           action == UiAction::stop_position_sharing ||
           action == UiAction::open_archive_controls ||
           action == UiAction::request_archive_start ||
           action == UiAction::request_archive_stop ||
           action == UiAction::confirm_archive_start ||
           action == UiAction::stop_archive ||
           action == UiAction::open_messages ||
           action == UiAction::open_inbox ||
           action == UiAction::open_outbox ||
           action == UiAction::open_compose ||
           action == UiAction::open_message_row_1 ||
           action == UiAction::open_message_row_2 ||
           action == UiAction::show_next_message_page ||
           action == UiAction::select_message_template_1 ||
           action == UiAction::select_message_template_2 ||
           action == UiAction::show_next_compose_page ||
           action == UiAction::send_composed_message ||
           action == UiAction::acknowledge_inbound_alert;
}

bool quick_status_action(UiAction action) {
    return action == UiAction::select_quick_status_ok ||
           action == UiAction::select_quick_status_need_assistance ||
           action == UiAction::select_quick_status_anyone_online ||
           action == UiAction::select_quick_status_available_to_help ||
           action == UiAction::show_next_quick_status_page ||
           action == UiAction::show_previous_quick_status_page;
}

bool valid_gesture(InputGesture gesture) {
    return gesture == InputGesture::activate || gesture == InputGesture::hold;
}

bool actions_are_unique(const UiFrame& frame) {
    for (std::size_t left = 0; left < frame.action_count; ++left) {
        for (std::size_t right = left + 1; right < frame.action_count; ++right) {
            if (frame.actions[left].action == frame.actions[right].action) {
                return false;
            }
        }
    }
    return true;
}

bool valid_frame_impl(const UiFrame& frame, const DisplayCapabilities& capabilities) {
    if (frame.revision == 0 || !valid_screen(frame.screen) ||
        !valid_attention(frame.attention) || !valid_notice(frame.notice) ||
        !valid_indicator(frame.status.radio) ||
        !valid_indicator(frame.status.position) ||
        !valid_indicator(frame.status.power) ||
        (!frame.status.peer_count_valid && frame.status.peer_count != 0) ||
        (!frame.status.archive_queue_count_valid &&
         frame.status.archive_queue_count != 0) ||
        frame.status.archive_queue_count > 16 ||
        frame.action_count > capabilities.max_action_slots ||
        frame.action_count > frame.actions.size()) {
        return false;
    }

    for (std::size_t index = 0; index < frame.actions.size(); ++index) {
        const auto& binding = frame.actions[index];
        if (index < frame.action_count) {
            if (!valid_action(binding.action)) {
                return false;
            }
        } else if (binding.action != UiAction::none || binding.enabled) {
            return false;
        }
    }
    if (!actions_are_unique(frame)) {
        return false;
    }

    if (frame.screen == UiScreen::quick_status_menu) {
        const bool first_page =
            frame.actions[0].action == UiAction::select_quick_status_ok &&
            frame.actions[1].action ==
                UiAction::select_quick_status_need_assistance &&
            frame.actions[2].action ==
                UiAction::show_next_quick_status_page;
        const bool second_page =
            frame.actions[0].action ==
                UiAction::select_quick_status_anyone_online &&
            frame.actions[1].action ==
                UiAction::select_quick_status_available_to_help &&
            frame.actions[2].action ==
                UiAction::show_previous_quick_status_page;
        return frame.attention == UiAttention::information &&
               frame.notice == UiNotice::none &&
               frame.action_count == 4 &&
               frame.actions[0].enabled &&
               frame.actions[1].enabled &&
               frame.actions[2].enabled &&
               frame.actions[3].action == UiAction::cancel &&
               frame.actions[3].enabled &&
               (first_page || second_page);
    }

    if (frame.screen == UiScreen::critical_confirmation) {
        return capabilities.supports_hold &&
               frame.attention == UiAttention::critical &&
               frame.action_count == 2 &&
               frame.actions[0].action == UiAction::confirm_critical_alert &&
               frame.actions[0].enabled &&
               frame.actions[1].action == UiAction::cancel &&
               frame.actions[1].enabled;
    }

    if (frame.screen == UiScreen::archive_confirmation) {
        const bool canonical_start =
            capabilities.supports_hold &&
            frame.notice == UiNotice::archive_start_confirmation &&
            frame.actions[0].action == UiAction::confirm_archive_start;
        const bool canonical_stop =
            frame.notice == UiNotice::archive_stop_confirmation &&
            frame.actions[0].action == UiAction::stop_archive;
        return frame.attention == UiAttention::information &&
               frame.action_count == 2 &&
               frame.actions[0].enabled &&
               frame.actions[1].action == UiAction::cancel &&
               frame.actions[1].enabled &&
               (canonical_start || canonical_stop);
    }

    if (frame.screen == UiScreen::archive_controls) {
        const bool start_available =
            frame.notice == UiNotice::archive_stopped &&
            frame.actions[0].action == UiAction::request_archive_start;
        const bool stop_available =
            (frame.notice == UiNotice::archive_active ||
             frame.notice == UiNotice::archive_queued ||
             frame.notice == UiNotice::archive_upload_waiting ||
             frame.notice == UiNotice::archive_queue_full ||
             frame.notice == UiNotice::archive_upload_failed) &&
            frame.actions[0].action == UiAction::request_archive_stop;
        return frame.action_count == 2 && frame.actions[0].enabled &&
               frame.status.archive_queue_count_valid &&
               frame.actions[1].action == UiAction::cancel &&
               frame.actions[1].enabled &&
               (start_available || stop_available);
    }

    if (frame.screen == UiScreen::message_center) {
        return frame.attention == UiAttention::information &&
               frame.notice == UiNotice::none &&
               frame.action_count == 4 &&
               frame.actions[0].action == UiAction::open_inbox &&
               frame.actions[0].enabled &&
               frame.actions[1].action == UiAction::open_outbox &&
               frame.actions[1].enabled &&
               frame.actions[2].action == UiAction::open_compose &&
               frame.actions[3].action == UiAction::cancel &&
               frame.actions[3].enabled;
    }

    if (frame.screen == UiScreen::message_list) {
        if (frame.attention != UiAttention::information ||
            frame.notice != UiNotice::none || frame.action_count == 0 ||
            frame.actions[frame.action_count - 1U].action != UiAction::cancel ||
            !frame.actions[frame.action_count - 1U].enabled) {
            return false;
        }
        const std::array<UiAction, kMaxUiMessageRows> rows{
            UiAction::open_message_row_1, UiAction::open_message_row_2};
        std::size_t row_index = 0;
        for (std::size_t index = 0; index + 1U < frame.action_count; ++index) {
            const auto action = frame.actions[index].action;
            if (action == UiAction::show_next_message_page) {
                if (index + 2U != frame.action_count) {
                    return false;
                }
            } else if (row_index >= rows.size() || action != rows[row_index++]) {
                return false;
            }
            if (!frame.actions[index].enabled) {
                return false;
            }
        }
        return true;
    }

    if (frame.screen == UiScreen::message_detail) {
        const bool acknowledge = frame.action_count == 2;
        return (!acknowledge || capabilities.supports_hold) &&
               frame.notice == UiNotice::none &&
               (frame.attention == UiAttention::information ||
                frame.attention == UiAttention::critical) &&
               frame.action_count == (acknowledge ? 2 : 1) &&
               (!acknowledge ||
                (frame.actions[0].action ==
                     UiAction::acknowledge_inbound_alert &&
                 frame.actions[0].enabled)) &&
               frame.actions[frame.action_count - 1U].action == UiAction::cancel &&
               frame.actions[frame.action_count - 1U].enabled;
    }

    if (frame.screen == UiScreen::message_compose) {
        return frame.attention == UiAttention::information &&
               frame.notice == UiNotice::none &&
               frame.action_count == 4 &&
               frame.actions[0].action == UiAction::select_message_template_1 &&
               frame.actions[0].enabled &&
               frame.actions[1].action == UiAction::select_message_template_2 &&
               frame.actions[1].enabled &&
               frame.actions[2].action == UiAction::show_next_compose_page &&
               frame.actions[2].enabled &&
               frame.actions[3].action == UiAction::cancel &&
               frame.actions[3].enabled;
    }

    if (frame.screen == UiScreen::message_compose_confirmation) {
        return frame.attention == UiAttention::information &&
               frame.notice == UiNotice::none &&
               frame.action_count == 2 &&
               frame.actions[0].action == UiAction::send_composed_message &&
               frame.actions[1].action == UiAction::cancel &&
               frame.actions[1].enabled;
    }

    for (std::size_t index = 0; index < frame.action_count; ++index) {
        if (quick_status_action(frame.actions[index].action)) {
            return false;
        }
        if (frame.actions[index].action == UiAction::confirm_critical_alert ||
            frame.actions[index].action == UiAction::request_archive_start ||
            frame.actions[index].action == UiAction::request_archive_stop ||
            frame.actions[index].action == UiAction::confirm_archive_start ||
            frame.actions[index].action == UiAction::stop_archive) {
            return false;
        }
        if (frame.screen == UiScreen::system_fault &&
            (frame.actions[index].action == UiAction::open_critical_confirmation ||
             frame.actions[index].action == UiAction::submit_selected_quick_status ||
             frame.actions[index].action == UiAction::start_position_sharing ||
             frame.actions[index].action == UiAction::open_archive_controls ||
             frame.actions[index].action == UiAction::confirm_archive_start)) {
            return false;
        }
    }
    return true;
}

PresentError map_display_error(DisplayWriteError error) {
    switch (error) {
        case DisplayWriteError::none:
            return PresentError::none;
        case DisplayWriteError::not_ready:
            return PresentError::sink_not_ready;
        case DisplayWriteError::sink_failed:
            return PresentError::sink_failed;
        default:
            return PresentError::sink_failed;
    }
}

ResolvedAction rejected_action(ActionResolutionError error) {
    ResolvedAction result{};
    result.error = error;
    return result;
}

}  // namespace

bool valid_ui_frame(
    const UiFrame& frame,
    const DisplayCapabilities& capabilities) {
    return valid_display_capabilities(capabilities) &&
           valid_frame_impl(frame, capabilities);
}

CheckedLocalInterface::CheckedLocalInterface(DisplaySink& display,
                                             LocalInputSource& input,
                                             DisplayCapabilities capabilities)
    : display_(display), input_(input), capabilities_(capabilities) {
    status_.capabilities_valid = valid_display_capabilities(capabilities_);
}

PresentResult CheckedLocalInterface::present(const UiFrame& frame) {
    saturating_increment(status_.present_attempts);
    const auto validation = validate_candidate(frame);
    if (validation != PresentError::none) {
        saturating_increment(status_.rejected_frames);
        return {validation, frame.revision};
    }

    const auto display_error = map_display_error(display_.present(frame));
    if (display_error != PresentError::none) {
        saturating_increment(status_.rejected_frames);
        return {display_error, frame.revision};
    }

    active_frame_ = frame;
    status_.has_active_frame = true;
    status_.active_revision = frame.revision;
    saturating_increment(status_.presented_frames);
    return {PresentError::none, frame.revision};
}

PresentError CheckedLocalInterface::validate_candidate(
    const UiFrame& frame) const {
    if (!status_.capabilities_valid) {
        return PresentError::invalid_capabilities;
    }
    if (!valid_ui_frame(frame, capabilities_)) {
        return PresentError::invalid_frame;
    }
    if (status_.has_active_frame && frame.revision <= status_.active_revision) {
        return PresentError::revision_not_increasing;
    }
    return PresentError::none;
}

ResolvedAction CheckedLocalInterface::poll_action() {
    saturating_increment(status_.input_attempts);
    const auto event = input_.read();
    switch (event.error) {
        case InputReadError::not_ready:
            saturating_increment(status_.rejected_inputs);
            return rejected_action(ActionResolutionError::input_not_ready);
        case InputReadError::source_failed:
            saturating_increment(status_.rejected_inputs);
            return rejected_action(ActionResolutionError::input_failed);
        case InputReadError::none:
            break;
        default:
            saturating_increment(status_.rejected_inputs);
            return rejected_action(ActionResolutionError::input_failed);
    }

    if (!status_.has_active_frame) {
        saturating_increment(status_.rejected_inputs);
        return rejected_action(ActionResolutionError::no_active_frame);
    }
    if (!valid_gesture(event.gesture)) {
        saturating_increment(status_.rejected_inputs);
        return rejected_action(ActionResolutionError::invalid_gesture);
    }
    if (event.frame_revision != status_.active_revision) {
        saturating_increment(status_.rejected_inputs);
        return rejected_action(ActionResolutionError::stale_frame);
    }
    if (event.action_slot >= active_frame_.action_count) {
        saturating_increment(status_.rejected_inputs);
        return rejected_action(ActionResolutionError::invalid_slot);
    }

    const auto binding = active_frame_.actions[event.action_slot];
    if (!binding.enabled) {
        saturating_increment(status_.rejected_inputs);
        return rejected_action(ActionResolutionError::disabled_action);
    }
    if (binding.action == UiAction::confirm_critical_alert ||
        binding.action == UiAction::confirm_archive_start ||
        binding.action == UiAction::acknowledge_inbound_alert) {
        if (event.gesture != InputGesture::hold) {
            saturating_increment(status_.rejected_inputs);
            return rejected_action(ActionResolutionError::hold_required);
        }
    } else if (event.gesture != InputGesture::activate) {
        saturating_increment(status_.rejected_inputs);
        return rejected_action(ActionResolutionError::invalid_gesture);
    }

    saturating_increment(status_.resolved_actions);
    return {ActionResolutionError::none, binding.action, event.frame_revision};
}

LocalInterfaceStatus CheckedLocalInterface::status() const {
    return status_;
}

}  // namespace opentrail::ui
