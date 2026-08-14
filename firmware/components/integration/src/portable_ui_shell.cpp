#include "opentrail/portable_ui_shell.hpp"

#include <algorithm>
#include <limits>
#include <string_view>

#include "opentrail/portable_ui_render_plan.hpp"
#include "opentrail/update_recovery_presentation.hpp"

namespace opentrail::integration {
namespace {

constexpr std::array<std::string_view, kPortableUiMessageTemplateCount>
    kMessageTemplates{{
        "Checking in",
        "Meet at camp",
        "Need supplies",
        "Delayed",
        "Reached checkpoint",
        "Returning now",
        "All clear",
        "Call me when online",
    }};

bool append_text(ui::UiOwnedText& destination, std::string_view source) {
    if (source.empty() ||
        destination.byte_count + source.size() > ui::kMaxUiOwnedTextBytes) {
        return false;
    }
    for (const auto character : source) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte > 0x7EU) {
            return false;
        }
        destination.bytes[destination.byte_count++] = character;
    }
    destination.bytes[destination.byte_count] = '\0';
    return true;
}

ui::UiOwnedText text_from(std::string_view source) {
    ui::UiOwnedText result{};
    if (!append_text(result, source)) {
        return {};
    }
    return result;
}

bool valid_owned_text(const ui::UiOwnedText& text) {
    if (text.byte_count == 0 || text.byte_count > ui::kMaxUiOwnedTextBytes ||
        text.bytes[text.byte_count] != '\0') {
        return false;
    }
    for (std::size_t index = 0; index < text.bytes.size(); ++index) {
        const auto byte = static_cast<unsigned char>(text.bytes[index]);
        if (index < text.byte_count) {
            if (byte < 0x20U || byte > 0x7EU) {
                return false;
            }
        } else if (byte != 0U) {
            return false;
        }
    }
    return true;
}

bool same_text(const ui::UiOwnedText& left, const ui::UiOwnedText& right) {
    return left.byte_count == right.byte_count &&
           left.truncated == right.truncated &&
           left.unavailable == right.unavailable && left.bytes == right.bytes;
}

bool known_direction(PortableUiMessageDirection direction) {
    return direction == PortableUiMessageDirection::inbound ||
           direction == PortableUiMessageDirection::outbound;
}

bool known_message_kind(PortableUiMessageKind kind) {
    return kind == PortableUiMessageKind::chat ||
           kind == PortableUiMessageKind::quick_status ||
           kind == PortableUiMessageKind::alert ||
           kind == PortableUiMessageKind::acknowledgement;
}

bool known_priority(PortableUiMessagePriority priority) {
    return priority == PortableUiMessagePriority::normal ||
           priority == PortableUiMessagePriority::important ||
           priority == PortableUiMessagePriority::critical;
}

bool known_delivery(PortableUiMessageDeliveryState delivery) {
    return delivery == PortableUiMessageDeliveryState::queued ||
           delivery == PortableUiMessageDeliveryState::bridge_accepted ||
           delivery == PortableUiMessageDeliveryState::bridge_observed ||
           delivery ==
               PortableUiMessageDeliveryState::bridge_acknowledgement_observed ||
           delivery == PortableUiMessageDeliveryState::failed;
}

bool bridge_request(PortableUiRequestKind kind) {
    return kind == PortableUiRequestKind::quick_status_ok ||
           kind == PortableUiRequestKind::quick_status_need_assistance ||
           kind == PortableUiRequestKind::quick_status_anyone_online ||
           kind == PortableUiRequestKind::quick_status_available_to_help ||
           kind == PortableUiRequestKind::critical_alert ||
           kind == PortableUiRequestKind::message_template_send ||
           kind == PortableUiRequestKind::message_alert_acknowledge;
}

const PortableUiMessage* message_by_sequence(
    const PortableUiSnapshot& snapshot,
    std::uint64_t sequence);

bool same_template_text(const ui::UiOwnedText& text,
                        std::uint8_t template_id) {
    const auto expected = portable_ui_message_template(template_id);
    return same_text(text, expected);
}

bool successful_bridge_evidence(
    PortableUiRequestKind kind,
    std::uint64_t applied_sequence,
    std::uint64_t sequence_watermark,
    std::uint8_t template_id,
    std::uint64_t acknowledged_sequence,
    const PortableUiSnapshot& snapshot) {
    if (applied_sequence == 0 || applied_sequence <= sequence_watermark) {
        return false;
    }
    const auto* applied = message_by_sequence(snapshot, applied_sequence);
    if (applied == nullptr ||
        applied->direction != PortableUiMessageDirection::outbound ||
        applied->delivery == PortableUiMessageDeliveryState::failed) {
        return false;
    }
    if (kind == PortableUiRequestKind::message_template_send) {
        return applied->kind == PortableUiMessageKind::chat &&
               applied->priority == PortableUiMessagePriority::normal &&
               same_template_text(applied->text, template_id);
    }
    if (kind == PortableUiRequestKind::message_alert_acknowledge) {
        const auto* acknowledged =
            message_by_sequence(snapshot, acknowledged_sequence);
        return applied->kind == PortableUiMessageKind::acknowledgement &&
               applied->priority == PortableUiMessagePriority::important &&
               same_text(applied->text, text_from("Acknowledged")) &&
               applied_sequence > acknowledged_sequence &&
               acknowledged != nullptr &&
               acknowledged->direction == PortableUiMessageDirection::inbound &&
               acknowledged->kind == PortableUiMessageKind::alert &&
               acknowledged->priority == PortableUiMessagePriority::critical &&
               !acknowledged->acknowledge_available;
    }
    if (kind == PortableUiRequestKind::critical_alert) {
        return applied->kind == PortableUiMessageKind::alert &&
               applied->priority == PortableUiMessagePriority::critical &&
               same_text(applied->text, text_from("Critical alert"));
    }
    std::string_view expected{};
    switch (kind) {
        case PortableUiRequestKind::quick_status_ok:
            expected = "I'm OK";
            break;
        case PortableUiRequestKind::quick_status_need_assistance:
            expected = "Need assistance";
            break;
        case PortableUiRequestKind::quick_status_anyone_online:
            expected = "Anyone online?";
            break;
        case PortableUiRequestKind::quick_status_available_to_help:
            expected = "Available to help";
            break;
        default:
            return false;
    }
    return applied->kind == PortableUiMessageKind::quick_status &&
           applied->priority == PortableUiMessagePriority::important &&
           same_text(applied->text, text_from(expected));
}

bool same_message(const PortableUiMessage& left,
                  const PortableUiMessage& right) {
    return left.sequence == right.sequence && left.direction == right.direction &&
           left.kind == right.kind && left.priority == right.priority &&
           left.delivery == right.delivery && same_text(left.text, right.text) &&
           left.text_unavailable == right.text_unavailable &&
           left.acknowledge_available == right.acknowledge_available;
}

bool direction_matches(const PortableUiMessage& message,
                       ui::UiMessageListKind list_kind) {
    return (list_kind == ui::UiMessageListKind::inbox &&
            message.direction == PortableUiMessageDirection::inbound) ||
           (list_kind == ui::UiMessageListKind::outbox &&
            message.direction == PortableUiMessageDirection::outbound);
}

std::uint8_t filtered_count(const PortableUiSnapshot& snapshot,
                            ui::UiMessageListKind list_kind) {
    std::uint8_t count = 0;
    for (std::size_t index = 0; index < snapshot.message_count; ++index) {
        if (direction_matches(snapshot.messages[index], list_kind)) {
            ++count;
        }
    }
    return count;
}

const PortableUiMessage* filtered_message(const PortableUiSnapshot& snapshot,
                                          ui::UiMessageListKind list_kind,
                                          std::uint8_t newest_offset) {
    std::uint8_t matched = 0;
    for (std::size_t index = snapshot.message_count; index > 0; --index) {
        const auto& message = snapshot.messages[index - 1U];
        if (!direction_matches(message, list_kind)) {
            continue;
        }
        if (matched++ == newest_offset) {
            return &message;
        }
    }
    return nullptr;
}

const PortableUiMessage* message_by_sequence(const PortableUiSnapshot& snapshot,
                                             std::uint64_t sequence) {
    for (std::size_t index = 0; index < snapshot.message_count; ++index) {
        if (snapshot.messages[index].sequence == sequence) {
            return &snapshot.messages[index];
        }
    }
    return nullptr;
}

std::string_view delivery_copy(PortableUiMessageDeliveryState state) {
    switch (state) {
        case PortableUiMessageDeliveryState::queued:
            return "Queued locally";
        case PortableUiMessageDeliveryState::bridge_accepted:
            return "Accepted by bridge";
        case PortableUiMessageDeliveryState::bridge_observed:
            return "Observed by bridge";
        case PortableUiMessageDeliveryState::bridge_acknowledgement_observed:
            return "ACK observed by bridge";
        case PortableUiMessageDeliveryState::failed:
            return "Failed locally";
    }
    return "Unavailable";
}

ui::UiOwnedText display_message_text(const PortableUiMessage& message) {
    if (message.text_unavailable) {
        auto result = text_from("Unsupported message text");
        result.unavailable = true;
        return result;
    }
    if (message.text.truncated) {
        auto result = text_from("Message exceeds display limit");
        result.truncated = true;
        return result;
    }
    return message.text;
}

ui::UiOwnedText list_row_copy(const PortableUiMessage& message,
                              bool unread,
                              bool outbox) {
    ui::UiOwnedText result{};
    const auto display_text = display_message_text(message);
    const auto prefix = outbox ? delivery_copy(message.delivery)
                               : (unread ? std::string_view{"NEW"}
                                         : std::string_view{"READ"});
    if (!append_text(result, prefix) || !append_text(result, ": ")) {
        return {};
    }
    const auto remaining = static_cast<std::size_t>(
        ui::kMaxUiOwnedTextBytes - result.byte_count);
    const auto copy_count = std::min<std::size_t>(display_text.byte_count,
                                                  remaining);
    for (std::size_t index = 0; index < copy_count; ++index) {
        result.bytes[result.byte_count++] = display_text.bytes[index];
    }
    result.bytes[result.byte_count] = '\0';
    result.truncated = display_text.truncated ||
                       copy_count != display_text.byte_count;
    result.unavailable = display_text.unavailable;
    return result;
}

PortableUiRequestKind request_for(ui::UiAction action) {
    switch (action) {
        case ui::UiAction::select_quick_status_ok:
            return PortableUiRequestKind::quick_status_ok;
        case ui::UiAction::select_quick_status_need_assistance:
            return PortableUiRequestKind::quick_status_need_assistance;
        case ui::UiAction::select_quick_status_anyone_online:
            return PortableUiRequestKind::quick_status_anyone_online;
        case ui::UiAction::select_quick_status_available_to_help:
            return PortableUiRequestKind::quick_status_available_to_help;
        case ui::UiAction::confirm_critical_alert:
            return PortableUiRequestKind::critical_alert;
        case ui::UiAction::confirm_archive_start:
            return PortableUiRequestKind::archive_start;
        case ui::UiAction::stop_archive:
            return PortableUiRequestKind::archive_stop;
        case ui::UiAction::start_position_sharing:
            return PortableUiRequestKind::position_start;
        case ui::UiAction::stop_position_sharing:
            return PortableUiRequestKind::position_stop;
        case ui::UiAction::send_composed_message:
            return PortableUiRequestKind::message_template_send;
        case ui::UiAction::acknowledge_inbound_alert:
            return PortableUiRequestKind::message_alert_acknowledge;
        default:
            return PortableUiRequestKind::none;
    }
}

ui::UiNotice failure_notice(PortableUiRequestKind kind) {
    if (kind == PortableUiRequestKind::critical_alert) {
        return ui::UiNotice::critical_alert_failed;
    }
    if (kind == PortableUiRequestKind::archive_start ||
        kind == PortableUiRequestKind::archive_stop) {
        return ui::UiNotice::archive_upload_failed;
    }
    if (kind == PortableUiRequestKind::position_start ||
        kind == PortableUiRequestKind::position_stop) {
        return ui::UiNotice::position_sharing_failed;
    }
    if (kind == PortableUiRequestKind::message_alert_acknowledge) {
        return ui::UiNotice::critical_alert_failed;
    }
    return ui::UiNotice::message_failed;
}

bool same_summary(const ui::UiStatusSummary& left,
                  const ui::UiStatusSummary& right) {
    return left.radio == right.radio && left.position == right.position &&
           left.power == right.power &&
           left.peer_count_valid == right.peer_count_valid &&
           left.peer_count == right.peer_count &&
           left.archive_queue_count_valid == right.archive_queue_count_valid &&
           left.archive_queue_count == right.archive_queue_count;
}

bool same_snapshot(const PortableUiSnapshot& left,
                   const PortableUiSnapshot& right) {
    if (!same_summary(left.status, right.status) ||
        left.position != right.position || left.archive != right.archive ||
        left.recovery_diagnostic_valid != right.recovery_diagnostic_valid ||
        left.recovery_diagnostic_word != right.recovery_diagnostic_word ||
        left.bridge_session_epoch != right.bridge_session_epoch ||
        left.message_count != right.message_count) {
        return false;
    }
    for (std::size_t index = 0; index < left.message_count; ++index) {
        if (!same_message(left.messages[index], right.messages[index])) {
            return false;
        }
    }
    return true;
}

bool known_position(PortableUiPositionState state) {
    return state == PortableUiPositionState::stopped ||
           state == PortableUiPositionState::active ||
           state == PortableUiPositionState::waiting_for_fix ||
           state == PortableUiPositionState::deferred ||
           state == PortableUiPositionState::failed;
}

bool known_archive(PortableUiArchiveState state) {
    return state == PortableUiArchiveState::stopped ||
           state == PortableUiArchiveState::active ||
           state == PortableUiArchiveState::queued ||
           state == PortableUiArchiveState::upload_waiting ||
           state == PortableUiArchiveState::queue_full ||
           state == PortableUiArchiveState::upload_failed ||
           state == PortableUiArchiveState::unavailable ||
           state == PortableUiArchiveState::incoherent;
}

bool valid_snapshot(const PortableUiSnapshot& snapshot) {
    if (!known_position(snapshot.position) || !known_archive(snapshot.archive) ||
        snapshot.status.unread_messages != 0 ||
        (!snapshot.recovery_diagnostic_valid &&
         snapshot.recovery_diagnostic_word != 0)) {
        return false;
    }
    if (snapshot.message_count > snapshot.messages.size()) {
        return false;
    }
    std::uint64_t prior_sequence = 0;
    for (std::size_t index = 0; index < snapshot.messages.size(); ++index) {
        const auto& message = snapshot.messages[index];
        if (index >= snapshot.message_count) {
            if (message.sequence != 0 || message.text.byte_count != 0 ||
                message.text.truncated || message.text.unavailable ||
                message.text_unavailable || message.acknowledge_available ||
                message.direction != PortableUiMessageDirection::inbound ||
                message.kind != PortableUiMessageKind::chat ||
                message.priority != PortableUiMessagePriority::normal ||
                message.delivery !=
                    PortableUiMessageDeliveryState::bridge_observed) {
                return false;
            }
            for (const auto byte : message.text.bytes) {
                if (byte != '\0') {
                    return false;
                }
            }
            continue;
        }
        if (message.sequence == 0 || message.sequence <= prior_sequence ||
            !known_direction(message.direction) ||
            !known_message_kind(message.kind) ||
            !known_priority(message.priority) ||
            !known_delivery(message.delivery) || message.text.unavailable ||
            (message.text.truncated && message.text_unavailable)) {
            return false;
        }
        if (message.text.truncated || message.text_unavailable) {
            if (message.text.byte_count != 0) {
                return false;
            }
            for (const auto byte : message.text.bytes) {
                if (byte != '\0') {
                    return false;
                }
            }
        } else if (!valid_owned_text(message.text)) {
            return false;
        }
        if (message.direction == PortableUiMessageDirection::inbound &&
            message.delivery != PortableUiMessageDeliveryState::bridge_observed) {
            return false;
        }
        if (message.direction == PortableUiMessageDirection::outbound) {
            const bool ordinary_delivery =
                message.delivery == PortableUiMessageDeliveryState::queued ||
                message.delivery ==
                    PortableUiMessageDeliveryState::bridge_accepted ||
                message.delivery == PortableUiMessageDeliveryState::failed;
            const bool alert_acknowledged =
                message.kind == PortableUiMessageKind::alert &&
                message.delivery == PortableUiMessageDeliveryState::
                                        bridge_acknowledgement_observed;
            if (!ordinary_delivery && !alert_acknowledged) {
                return false;
            }
        }
        if (message.acknowledge_available &&
            (message.direction != PortableUiMessageDirection::inbound ||
             message.kind != PortableUiMessageKind::alert ||
             message.priority != PortableUiMessagePriority::critical)) {
            return false;
        }
        prior_sequence = message.sequence;
    }
    const bool unavailable =
        snapshot.archive == PortableUiArchiveState::unavailable ||
        snapshot.archive == PortableUiArchiveState::incoherent;
    if (unavailable) {
        return !snapshot.status.archive_queue_count_valid &&
               snapshot.status.archive_queue_count == 0;
    }
    if (!snapshot.status.archive_queue_count_valid ||
        snapshot.status.archive_queue_count > 16) {
        return false;
    }
    if (snapshot.archive == PortableUiArchiveState::stopped) {
        return snapshot.status.archive_queue_count == 0;
    }
    if (snapshot.archive == PortableUiArchiveState::queued ||
        snapshot.archive == PortableUiArchiveState::upload_waiting ||
        snapshot.archive == PortableUiArchiveState::queue_full) {
        return snapshot.status.archive_queue_count != 0;
    }
    return true;
}

PortableUiShellMode root_mode(
    const PortableUiSnapshot& snapshot,
    bool has_acknowledged_recovery,
    std::uint32_t acknowledged_recovery_word) {
    if (snapshot.position == PortableUiPositionState::failed) {
        return PortableUiShellMode::system_fault;
    }
    if (snapshot.recovery_diagnostic_valid) {
        const auto recovery = make_update_recovery_presentation(
            snapshot.recovery_diagnostic_word, 1);
        if (!recovery.presentable() ||
            recovery.frame.screen == ui::UiScreen::system_fault) {
            return PortableUiShellMode::system_fault;
        }
        if (recovery.frame.notice != ui::UiNotice::none ||
            recovery.frame.action_count != 0) {
            if (has_acknowledged_recovery &&
                snapshot.recovery_diagnostic_word == acknowledged_recovery_word) {
                return PortableUiShellMode::home;
            }
            return PortableUiShellMode::recovery_notice;
        }
    }
    return PortableUiShellMode::home;
}

}  // namespace

PortableUiShell::PortableUiShell(
    ui::CheckedLocalInterface& local_interface,
    std::uint32_t initial_request_id)
    : local_interface_(local_interface),
      next_request_id_(initial_request_id == 0 ? 1 : initial_request_id) {}

PortableUiShellResult PortableUiShell::prepare_activate(
    const PortableUiSnapshot& snapshot) {
    if (!valid_snapshot(snapshot)) {
        return reject(PortableUiShellError::invalid_state);
    }
    if ((shell_status_.mode != PortableUiShellMode::inactive &&
         shell_status_.mode != PortableUiShellMode::closed) ||
        pending_.present) {
        return reject(PortableUiShellError::invalid_state);
    }
    if (shell_status_.generation == std::numeric_limits<std::uint32_t>::max()) {
        return reject(PortableUiShellError::revision_exhausted);
    }
    return offer(root_mode(snapshot, false, 0), snapshot, PendingEffect::activate,
                 PortableUiRequestKind::none, 0,
                 shell_status_.generation + 1U);
}

PortableUiShellResult PortableUiShell::prepare_input() {
    if (pending_.present) {
        return reject(PortableUiShellError::pending_offer);
    }
    if (shell_status_.mode == PortableUiShellMode::inactive ||
        shell_status_.mode == PortableUiShellMode::closed ||
        shell_status_.mode == PortableUiShellMode::faulted ||
        shell_status_.mode == PortableUiShellMode::system_fault ||
        shell_status_.mode == PortableUiShellMode::request_pending) {
        return reject(PortableUiShellError::invalid_state);
    }
    const auto resolved = local_interface_.poll_action();
    if (resolved.error == ui::ActionResolutionError::input_not_ready) {
        PortableUiShellResult result{};
        result.disposition = PortableUiShellDisposition::idle;
        result.action_error = resolved.error;
        result.generation = shell_status_.generation;
        result.revision = shell_status_.active_revision;
        return result;
    }
    if (!resolved.ok()) {
        PortableUiShellResult result{};
        result.action_error = resolved.error;
        result.generation = shell_status_.generation;
        result.revision = shell_status_.active_revision;
        if (resolved.error == ui::ActionResolutionError::input_failed) {
            result.error = PortableUiShellError::input_failed;
            latch(result.error);
        } else {
            result.disposition = PortableUiShellDisposition::input_rejected;
        }
        return result;
    }

    switch (resolved.action) {
        case ui::UiAction::show_status:
            return offer(PortableUiShellMode::status, snapshot_);
        case ui::UiAction::open_quick_status_menu:
            return offer(PortableUiShellMode::quick_status_first, snapshot_);
        case ui::UiAction::show_next_quick_status_page:
            return offer(PortableUiShellMode::quick_status_second, snapshot_);
        case ui::UiAction::show_previous_quick_status_page:
            return offer(PortableUiShellMode::quick_status_first, snapshot_);
        case ui::UiAction::open_critical_confirmation:
            return offer(PortableUiShellMode::critical_confirmation, snapshot_);
        case ui::UiAction::open_archive_controls:
            return offer(PortableUiShellMode::archive_controls, snapshot_);
        case ui::UiAction::open_messages:
            return offer(PortableUiShellMode::message_center, snapshot_);
        case ui::UiAction::open_inbox:
            return offer(PortableUiShellMode::message_inbox, snapshot_,
                         PendingEffect::none, PortableUiRequestKind::none,
                         0, 0, 0);
        case ui::UiAction::open_outbox:
            return offer(PortableUiShellMode::message_outbox, snapshot_,
                         PendingEffect::none, PortableUiRequestKind::none,
                         0, 0, 0);
        case ui::UiAction::open_compose:
            return offer(PortableUiShellMode::message_compose, snapshot_);
        case ui::UiAction::open_message_row_1:
        case ui::UiAction::open_message_row_2: {
            const auto list_kind =
                shell_status_.mode == PortableUiShellMode::message_inbox
                    ? ui::UiMessageListKind::inbox
                    : (shell_status_.mode == PortableUiShellMode::message_outbox
                           ? ui::UiMessageListKind::outbox
                           : ui::UiMessageListKind::none);
            const auto row = static_cast<std::uint8_t>(
                static_cast<unsigned>(resolved.action) -
                static_cast<unsigned>(ui::UiAction::open_message_row_1));
            const auto offset = static_cast<std::uint8_t>(
                shell_status_.message_page * ui::kMaxUiMessageRows + row);
            const auto* selected =
                filtered_message(snapshot_, list_kind, offset);
            if (list_kind == ui::UiMessageListKind::none || selected == nullptr) {
                return reject(PortableUiShellError::unexpected_action);
            }
            return offer(PortableUiShellMode::message_detail, snapshot_,
                         selected->direction == PortableUiMessageDirection::inbound
                             ? PendingEffect::mark_message_read
                             : PendingEffect::none,
                         PortableUiRequestKind::none, 0, 0,
                         shell_status_.message_page, 0, selected->sequence);
        }
        case ui::UiAction::show_next_message_page:
        {
            const auto list_kind =
                shell_status_.mode == PortableUiShellMode::message_inbox
                    ? ui::UiMessageListKind::inbox
                    : ui::UiMessageListKind::outbox;
            const auto count = filtered_count(snapshot_, list_kind);
            const auto page_count = static_cast<std::uint8_t>(
                std::max<std::uint16_t>(
                    1U, (static_cast<std::uint16_t>(count) +
                             ui::kMaxUiMessageRows - 1U) /
                            ui::kMaxUiMessageRows));
            return offer(shell_status_.mode, snapshot_, PendingEffect::none,
                         PortableUiRequestKind::none, 0, 0,
                         static_cast<std::uint8_t>(
                             (shell_status_.message_page + 1U) % page_count));
        }
        case ui::UiAction::select_message_template_1:
        case ui::UiAction::select_message_template_2: {
            const auto slot = static_cast<std::uint8_t>(
                static_cast<unsigned>(resolved.action) -
                static_cast<unsigned>(ui::UiAction::select_message_template_1));
            const auto template_id = static_cast<std::uint8_t>(
                shell_status_.message_page * 2U + slot + 1U);
            return offer(PortableUiShellMode::message_compose_confirmation,
                         snapshot_, PendingEffect::none,
                         PortableUiRequestKind::none, 0, 0,
                         shell_status_.message_page,
                         template_id);
        }
        case ui::UiAction::show_next_compose_page:
            return offer(PortableUiShellMode::message_compose, snapshot_,
                         PendingEffect::none, PortableUiRequestKind::none,
                         0, 0,
                         static_cast<std::uint8_t>(
                             (shell_status_.message_page + 1U) % 4U));
        case ui::UiAction::request_archive_start:
            return offer(PortableUiShellMode::archive_start_confirmation, snapshot_);
        case ui::UiAction::request_archive_stop:
            return offer(PortableUiShellMode::archive_stop_confirmation, snapshot_);
        case ui::UiAction::cancel:
            if (shell_status_.mode == PortableUiShellMode::archive_start_confirmation ||
                shell_status_.mode == PortableUiShellMode::archive_stop_confirmation) {
                return offer(PortableUiShellMode::archive_controls, snapshot_);
            }
            if (shell_status_.mode == PortableUiShellMode::message_center) {
                return offer(PortableUiShellMode::home, snapshot_);
            }
            if (shell_status_.mode == PortableUiShellMode::message_inbox ||
                shell_status_.mode == PortableUiShellMode::message_outbox ||
                shell_status_.mode == PortableUiShellMode::message_compose) {
                return offer(PortableUiShellMode::message_center, snapshot_);
            }
            if (shell_status_.mode == PortableUiShellMode::message_detail) {
                const auto* selected = message_by_sequence(
                    snapshot_, shell_status_.selected_message_sequence);
                return offer(
                    selected != nullptr &&
                            selected->direction == PortableUiMessageDirection::outbound
                        ? PortableUiShellMode::message_outbox
                        : PortableUiShellMode::message_inbox,
                    snapshot_, PendingEffect::none,
                    PortableUiRequestKind::none, 0, 0,
                    shell_status_.message_page);
            }
            if (shell_status_.mode ==
                PortableUiShellMode::message_compose_confirmation) {
                return offer(PortableUiShellMode::message_compose, snapshot_,
                             PendingEffect::none,
                             PortableUiRequestKind::none, 0, 0,
                             static_cast<std::uint8_t>(
                                 (shell_status_.selected_template_id - 1U) / 2U));
            }
            return offer(PortableUiShellMode::home, snapshot_);
        case ui::UiAction::acknowledge_notice:
            if (shell_status_.mode == PortableUiShellMode::recovery_notice) {
                return offer(
                    deferred_request_failure_kind_ == PortableUiRequestKind::none
                        ? PortableUiShellMode::home
                        : PortableUiShellMode::request_failed,
                    snapshot_, PendingEffect::acknowledge_recovery,
                    deferred_request_failure_kind_);
            }
            if (deferred_request_failure_kind_ ==
                PortableUiRequestKind::message_template_send) {
                return offer(
                    PortableUiShellMode::message_compose_confirmation,
                    snapshot_, PendingEffect::acknowledge_request_failure,
                    PortableUiRequestKind::none, 0, 0,
                    static_cast<std::uint8_t>(
                        (shell_status_.selected_template_id - 1U) / 2U),
                    shell_status_.selected_template_id);
            }
            if (deferred_request_failure_kind_ ==
                PortableUiRequestKind::message_alert_acknowledge) {
                const auto* selected = message_by_sequence(
                    snapshot_, shell_status_.selected_message_sequence);
                return offer(
                    selected == nullptr
                        ? PortableUiShellMode::message_inbox
                        : PortableUiShellMode::message_detail,
                    snapshot_, PendingEffect::acknowledge_request_failure,
                    PortableUiRequestKind::none, 0, 0,
                    shell_status_.message_page, 0,
                    selected == nullptr
                        ? 0
                        : shell_status_.selected_message_sequence);
            }
            return offer(
                PortableUiShellMode::home,
                snapshot_,
                PendingEffect::acknowledge_request_failure);
        default: {
            const auto kind = request_for(resolved.action);
            if (kind == PortableUiRequestKind::none) {
                latch(PortableUiShellError::unexpected_action);
                return reject(PortableUiShellError::unexpected_action);
            }
            if (next_request_id_ == 0) {
                latch(PortableUiShellError::revision_exhausted);
                return reject(PortableUiShellError::revision_exhausted);
            }
            if (kind == PortableUiRequestKind::message_template_send &&
                (shell_status_.selected_template_id == 0 ||
                 shell_status_.selected_template_id >
                     kPortableUiMessageTemplateCount)) {
                return reject(PortableUiShellError::unexpected_action);
            }
            if (kind == PortableUiRequestKind::message_alert_acknowledge) {
                const auto* selected = message_by_sequence(
                    snapshot_, shell_status_.selected_message_sequence);
                if (selected == nullptr || !selected->acknowledge_available) {
                    return reject(PortableUiShellError::unexpected_action);
                }
            }
            return offer(PortableUiShellMode::request_pending, snapshot_,
                         PendingEffect::emit_request, kind, next_request_id_,
                         0, shell_status_.message_page,
                         shell_status_.selected_template_id,
                         shell_status_.selected_message_sequence);
        }
    }
}

PortableUiShellResult PortableUiShell::prepare_refresh(
    std::uint32_t generation,
    std::uint32_t active_revision,
    const PortableUiSnapshot& snapshot) {
    if (generation != shell_status_.generation) {
        return reject(PortableUiShellError::generation_mismatch);
    }
    if (active_revision != shell_status_.active_revision) {
        return reject(PortableUiShellError::revision_mismatch);
    }
    if (pending_.present) {
        return reject(PortableUiShellError::pending_offer);
    }
    if (shell_status_.mode == PortableUiShellMode::inactive ||
        shell_status_.mode == PortableUiShellMode::closed ||
        shell_status_.mode == PortableUiShellMode::faulted) {
        return reject(PortableUiShellError::invalid_state);
    }
    if (!valid_snapshot(snapshot)) {
        return reject(PortableUiShellError::invalid_state);
    }
    if (same_snapshot(snapshot_, snapshot)) {
        PortableUiShellResult result{};
        result.disposition = PortableUiShellDisposition::idle;
        result.generation = generation;
        result.revision = active_revision;
        return result;
    }

    auto mode = shell_status_.mode;
    const auto root = root_mode(
        snapshot, has_acknowledged_recovery_, acknowledged_recovery_word_);
    const bool request_in_flight = shell_status_.pending_request_id != 0;
    if (request_in_flight) {
        mode = root == PortableUiShellMode::system_fault
                   ? PortableUiShellMode::system_fault
                   : PortableUiShellMode::request_pending;
    } else if (root != PortableUiShellMode::home) {
        mode = root;
    } else if (mode == PortableUiShellMode::recovery_notice ||
               mode == PortableUiShellMode::system_fault) {
        mode = deferred_request_failure_kind_ == PortableUiRequestKind::none
                   ? PortableUiShellMode::home
                   : PortableUiShellMode::request_failed;
    }
    auto message_page = shell_status_.message_page;
    auto selected_template_id = shell_status_.selected_template_id;
    auto selected_message_sequence = shell_status_.selected_message_sequence;
    if (!request_in_flight &&
        snapshot.bridge_session_epoch != snapshot_.bridge_session_epoch) {
        if (mode == PortableUiShellMode::quick_status_first ||
            mode == PortableUiShellMode::quick_status_second ||
            mode == PortableUiShellMode::critical_confirmation) {
            mode = PortableUiShellMode::home;
        } else if (mode == PortableUiShellMode::message_detail) {
            mode = PortableUiShellMode::message_center;
        } else if (mode ==
                   PortableUiShellMode::message_compose_confirmation) {
            mode = PortableUiShellMode::message_compose;
        }
        message_page = 0;
        selected_template_id = 0;
        selected_message_sequence = 0;
    }
    if (!request_in_flight && mode == PortableUiShellMode::message_detail &&
        message_by_sequence(snapshot, selected_message_sequence) == nullptr) {
        mode = PortableUiShellMode::message_center;
        message_page = 0;
        selected_message_sequence = 0;
    }
    if (!request_in_flight &&
        (mode == PortableUiShellMode::message_inbox ||
         mode == PortableUiShellMode::message_outbox)) {
        const auto list_kind = mode == PortableUiShellMode::message_inbox
                                   ? ui::UiMessageListKind::inbox
                                   : ui::UiMessageListKind::outbox;
        const auto count = filtered_count(snapshot, list_kind);
        const auto page_count = static_cast<std::uint8_t>(
            std::max<std::uint16_t>(
                1U, (static_cast<std::uint16_t>(count) +
                         ui::kMaxUiMessageRows - 1U) /
                        ui::kMaxUiMessageRows));
        if (message_page >= page_count) {
            message_page = static_cast<std::uint8_t>(page_count - 1U);
        }
    }
    return offer(mode, snapshot, PendingEffect::none,
                 request_in_flight
                     ? shell_status_.pending_request_kind
                     : (mode == PortableUiShellMode::request_failed
                            ? deferred_request_failure_kind_
                            : PortableUiRequestKind::none),
                 0, 0, message_page, selected_template_id,
                 selected_message_sequence);
}

PortableUiShellResult PortableUiShell::prepare_completion(
    std::uint32_t generation,
    std::uint32_t active_revision,
    std::uint32_t request_id,
    PortableUiRequestKind request_kind,
    bool succeeded,
    std::uint64_t applied_bridge_session_epoch,
    std::uint64_t applied_message_sequence,
    std::uint8_t request_template_id,
    std::uint64_t request_message_sequence,
    const PortableUiSnapshot& snapshot) {
    if (generation != shell_status_.generation) {
        return reject(PortableUiShellError::generation_mismatch);
    }
    if (active_revision != shell_status_.active_revision) {
        return reject(PortableUiShellError::revision_mismatch);
    }
    if (pending_.present) {
        return reject(PortableUiShellError::pending_offer);
    }
    if ((shell_status_.mode != PortableUiShellMode::request_pending &&
         shell_status_.mode != PortableUiShellMode::system_fault) ||
        request_id == 0 || request_id != shell_status_.pending_request_id ||
        request_kind == PortableUiRequestKind::none ||
        request_kind != shell_status_.pending_request_kind ||
        request_template_id != shell_status_.pending_request_template_id ||
        request_message_sequence !=
            shell_status_.pending_request_message_sequence ||
        (succeeded && bridge_request(request_kind) &&
         (applied_bridge_session_epoch == 0 ||
         applied_bridge_session_epoch !=
              shell_status_.pending_request_bridge_session_epoch)) ||
        (succeeded && !bridge_request(request_kind) &&
         (applied_bridge_session_epoch != 0 || applied_message_sequence != 0)) ||
        (!succeeded &&
         (applied_bridge_session_epoch != 0 || applied_message_sequence != 0))) {
        return reject(PortableUiShellError::request_mismatch);
    }
    if (!valid_snapshot(snapshot)) {
        return reject(PortableUiShellError::invalid_state);
    }
    if (succeeded && bridge_request(request_kind) &&
        !successful_bridge_evidence(
            request_kind, applied_message_sequence,
            shell_status_.pending_request_sequence_watermark,
            request_template_id,
            request_message_sequence, snapshot)) {
        return reject(PortableUiShellError::request_mismatch);
    }
    const auto root = root_mode(
        snapshot, has_acknowledged_recovery_, acknowledged_recovery_word_);
    auto completion_mode = root != PortableUiShellMode::home
                               ? root
                               : (succeeded ? PortableUiShellMode::home
                                            : PortableUiShellMode::request_failed);
    if (root == PortableUiShellMode::home && succeeded &&
        request_kind == PortableUiRequestKind::message_template_send) {
        completion_mode =
            snapshot.bridge_session_epoch ==
                    shell_status_.pending_request_bridge_session_epoch
                ? PortableUiShellMode::message_outbox
                : PortableUiShellMode::message_center;
    } else if (root == PortableUiShellMode::home && succeeded &&
               request_kind ==
                   PortableUiRequestKind::message_alert_acknowledge) {
        completion_mode =
            snapshot.bridge_session_epoch !=
                        shell_status_.pending_request_bridge_session_epoch ||
                    message_by_sequence(
                        snapshot,
                        shell_status_.selected_message_sequence) == nullptr
                ? PortableUiShellMode::message_inbox
                : PortableUiShellMode::message_detail;
    }
    const bool same_request_epoch =
        snapshot.bridge_session_epoch ==
        shell_status_.pending_request_bridge_session_epoch;
    return offer(
        completion_mode,
        snapshot,
        succeeded ? PendingEffect::complete_request
                  : PendingEffect::complete_request_failed,
        request_kind,
        request_id,
        0,
        (succeeded &&
         request_kind == PortableUiRequestKind::message_template_send) ||
                !same_request_epoch
            ? 0
            : shell_status_.message_page,
        (succeeded &&
         request_kind == PortableUiRequestKind::message_template_send) ||
                !same_request_epoch
            ? 0
            : shell_status_.selected_template_id,
        (succeeded &&
         request_kind == PortableUiRequestKind::message_template_send) ||
                !same_request_epoch
            ? 0
            : shell_status_.selected_message_sequence);
}

PortableUiShellResult PortableUiShell::commit_present(
    std::uint32_t generation,
    std::uint32_t offered_revision) {
    if (!pending_.present) {
        return reject(PortableUiShellError::no_pending_offer);
    }
    if (generation != pending_.generation) {
        return reject(PortableUiShellError::generation_mismatch);
    }
    if (offered_revision != pending_.frame.revision) {
        return reject(PortableUiShellError::revision_mismatch);
    }
    PortableUiShellResult result{};
    result.generation = pending_.generation;
    result.revision = offered_revision;
    const auto presented = local_interface_.present(pending_.frame);
    result.present_error = presented.error;
    if (!presented.ok()) {
        if (presented.error == ui::PresentError::sink_not_ready) {
            result.disposition = PortableUiShellDisposition::offer_ready;
            result.has_offer = true;
            return result;
        }
        result.error = PortableUiShellError::display_failed;
        pending_.present = false;
        latch(result.error);
        return result;
    }

    shell_status_.generation = pending_.generation;
    shell_status_.mode = pending_.mode;
    shell_status_.active_revision = offered_revision;
    shell_status_.archive = pending_.snapshot.archive;
    shell_status_.message_page = pending_.message_page;
    shell_status_.selected_template_id = pending_.selected_template_id;
    shell_status_.selected_message_sequence =
        pending_.selected_message_sequence;
    if (snapshot_.bridge_session_epoch !=
        pending_.snapshot.bridge_session_epoch) {
        read_markers_.fill(0);
        read_marker_count_ = 0;
    }
    snapshot_ = pending_.snapshot;
    if (has_acknowledged_recovery_ &&
        (!snapshot_.recovery_diagnostic_valid ||
         snapshot_.recovery_diagnostic_word != acknowledged_recovery_word_)) {
        has_acknowledged_recovery_ = false;
        acknowledged_recovery_word_ = 0;
    }
    result.disposition = PortableUiShellDisposition::committed;
    if (pending_.effect == PendingEffect::activate) {
        has_acknowledged_recovery_ = false;
        acknowledged_recovery_word_ = 0;
        deferred_request_failure_kind_ = PortableUiRequestKind::none;
    } else if (pending_.effect == PendingEffect::emit_request) {
        shell_status_.pending_request_id = pending_.request_id;
        shell_status_.pending_request_kind = pending_.request_kind;
        shell_status_.pending_request_bridge_session_epoch =
            bridge_request(pending_.request_kind)
                ? pending_.snapshot.bridge_session_epoch
                : 0;
        shell_status_.pending_request_template_id =
            pending_.selected_template_id;
        shell_status_.pending_request_message_sequence =
            pending_.selected_message_sequence;
        shell_status_.pending_request_sequence_watermark =
            pending_.snapshot.message_count == 0
                ? 0
                : pending_.snapshot.messages[
                      pending_.snapshot.message_count - 1U].sequence;
        next_request_id_ = pending_.request_id ==
                                   std::numeric_limits<std::uint32_t>::max()
                               ? 0U
                               : pending_.request_id + 1U;
        result.disposition = PortableUiShellDisposition::request_emitted;
        result.request_id = pending_.request_id;
        result.request_kind = pending_.request_kind;
        result.request_template_id = pending_.selected_template_id;
        result.request_message_sequence = pending_.selected_message_sequence;
        result.request_bridge_session_epoch =
            shell_status_.pending_request_bridge_session_epoch;
    } else if (pending_.effect == PendingEffect::complete_request ||
               pending_.effect == PendingEffect::complete_request_failed) {
        result.request_id = pending_.request_id;
        result.request_kind = pending_.request_kind;
        result.request_bridge_session_epoch =
            shell_status_.pending_request_bridge_session_epoch;
        result.request_template_id =
            shell_status_.pending_request_template_id;
        result.request_message_sequence =
            shell_status_.pending_request_message_sequence;
        shell_status_.pending_request_id = 0;
        shell_status_.pending_request_kind = PortableUiRequestKind::none;
        shell_status_.pending_request_bridge_session_epoch = 0;
        shell_status_.pending_request_template_id = 0;
        shell_status_.pending_request_message_sequence = 0;
        shell_status_.pending_request_sequence_watermark = 0;
        if (pending_.effect == PendingEffect::complete_request_failed) {
            deferred_request_failure_kind_ = pending_.request_kind;
        }
    } else if (pending_.effect == PendingEffect::acknowledge_recovery) {
        has_acknowledged_recovery_ =
            pending_.snapshot.recovery_diagnostic_valid;
        acknowledged_recovery_word_ =
            pending_.snapshot.recovery_diagnostic_word;
    } else if (pending_.effect == PendingEffect::acknowledge_request_failure) {
        deferred_request_failure_kind_ = PortableUiRequestKind::none;
    } else if (pending_.effect == PendingEffect::mark_message_read) {
        const auto sequence = pending_.selected_message_sequence;
        bool already_read = false;
        for (std::size_t index = 0; index < read_marker_count_; ++index) {
            already_read = already_read || read_markers_[index] == sequence;
        }
        if (!already_read && sequence != 0) {
            if (read_marker_count_ < read_markers_.size()) {
                read_markers_[read_marker_count_++] = sequence;
            } else {
                std::move(read_markers_.begin() + 1, read_markers_.end(),
                          read_markers_.begin());
                read_markers_.back() = sequence;
            }
        }
    }
    pending_.present = false;
    return result;
}

PortableUiShellResult PortableUiShell::reject_present(
    std::uint32_t generation,
    std::uint32_t offered_revision) {
    if (!pending_.present) {
        return reject(PortableUiShellError::no_pending_offer);
    }
    if (generation != pending_.generation) {
        return reject(PortableUiShellError::generation_mismatch);
    }
    if (offered_revision != pending_.frame.revision) {
        return reject(PortableUiShellError::revision_mismatch);
    }
    pending_.present = false;
    latch(PortableUiShellError::display_failed);
    PortableUiShellResult result{};
    result.disposition = PortableUiShellDisposition::render_rejected;
    result.error = PortableUiShellError::display_failed;
    result.generation = generation;
    result.revision = offered_revision;
    return result;
}

PortableUiShellResult PortableUiShell::close_session(
    std::uint32_t generation,
    std::uint32_t active_revision) {
    if (shell_status_.mode == PortableUiShellMode::closed) {
        PortableUiShellResult result{};
        result.disposition = PortableUiShellDisposition::idle;
        result.generation = shell_status_.generation;
        result.revision = shell_status_.active_revision;
        return result;
    }
    if (generation != shell_status_.generation) {
        return reject(PortableUiShellError::generation_mismatch);
    }
    if (active_revision != shell_status_.active_revision) {
        return reject(PortableUiShellError::revision_mismatch);
    }
    pending_.present = false;
    shell_status_.mode = PortableUiShellMode::closed;
    shell_status_.pending_request_id = 0;
    shell_status_.pending_request_kind = PortableUiRequestKind::none;
    shell_status_.pending_request_bridge_session_epoch = 0;
    shell_status_.pending_request_template_id = 0;
    shell_status_.pending_request_message_sequence = 0;
    shell_status_.pending_request_sequence_watermark = 0;
    shell_status_.message_page = 0;
    shell_status_.selected_template_id = 0;
    shell_status_.selected_message_sequence = 0;
    deferred_request_failure_kind_ = PortableUiRequestKind::none;
    has_acknowledged_recovery_ = false;
    acknowledged_recovery_word_ = 0;
    read_markers_.fill(0);
    read_marker_count_ = 0;
    snapshot_ = {};
    pending_ = {};
    PortableUiShellResult result{};
    result.disposition = PortableUiShellDisposition::committed;
    result.generation = generation;
    result.revision = active_revision;
    return result;
}

PortableUiShellStatus PortableUiShell::status() const {
    auto result = shell_status_;
    result.has_pending_offer = pending_.present;
    return result;
}

ui::UiFrame PortableUiShell::pending_frame() const {
    return pending_.present ? pending_.frame : ui::UiFrame{};
}

ui::UiPresentationSidecar PortableUiShell::presentation_sidecar() const {
    return pending_.sidecar;
}

ui::UiFrame PortableUiShell::frame_for(
    PortableUiShellMode mode,
    std::uint32_t revision,
    const PortableUiSnapshot& snapshot,
    PortableUiRequestKind request_kind,
    std::uint8_t message_page,
    std::uint8_t selected_template_id,
    std::uint64_t selected_message_sequence,
    ui::UiPresentationSidecar& sidecar) const {
    const auto is_read = [&](std::uint64_t sequence) {
        if (snapshot.bridge_session_epoch != snapshot_.bridge_session_epoch) {
            return false;
        }
        if (mode == PortableUiShellMode::message_detail &&
            selected_message_sequence == sequence) {
            return true;
        }
        for (std::size_t index = 0; index < read_marker_count_; ++index) {
            if (read_markers_[index] == sequence) {
                return true;
            }
        }
        return false;
    };
    auto summary = snapshot.status;
    summary.unread_messages = 0;
    for (std::size_t index = 0; index < snapshot.message_count; ++index) {
        if (snapshot.messages[index].direction ==
                PortableUiMessageDirection::inbound &&
            !is_read(snapshot.messages[index].sequence) &&
            summary.unread_messages != std::numeric_limits<std::uint8_t>::max()) {
            ++summary.unread_messages;
        }
    }
    if ((mode == PortableUiShellMode::recovery_notice ||
         mode == PortableUiShellMode::system_fault) &&
        snapshot.recovery_diagnostic_valid) {
        auto recovery = make_update_recovery_presentation(
            snapshot.recovery_diagnostic_word, revision);
        const bool matches_mode =
            mode == PortableUiShellMode::recovery_notice ||
            (recovery.presentable() &&
             recovery.frame.screen == ui::UiScreen::system_fault);
        if (recovery.presentable() && matches_mode) {
            recovery.frame.status = summary;
            return recovery.frame;
        }
    }

    ui::UiFrame frame{};
    frame.revision = revision;
    frame.status = summary;
    switch (mode) {
        case PortableUiShellMode::home:
            frame.screen = ui::UiScreen::home;
            frame.action_count = 4;
            frame.actions[0] = {ui::UiAction::show_status, true};
            frame.actions[1] = {ui::UiAction::open_messages, true};
            frame.actions[2] = {ui::UiAction::open_quick_status_menu,
                                snapshot.bridge_session_epoch != 0};
            frame.actions[3] = {ui::UiAction::open_critical_confirmation,
                                snapshot.bridge_session_epoch != 0};
            break;
        case PortableUiShellMode::status:
            frame.screen = ui::UiScreen::status;
            frame.action_count = 3;
            switch (snapshot.position) {
                case PortableUiPositionState::stopped:
                    frame.attention = ui::UiAttention::information;
                    frame.notice = ui::UiNotice::position_sharing_stopped;
                    frame.actions[0] = {ui::UiAction::start_position_sharing, true};
                    break;
                case PortableUiPositionState::active:
                    frame.attention = ui::UiAttention::information;
                    frame.notice = ui::UiNotice::position_sharing_active;
                    frame.actions[0] = {ui::UiAction::stop_position_sharing, true};
                    break;
                case PortableUiPositionState::waiting_for_fix:
                    frame.attention = ui::UiAttention::warning;
                    frame.notice = ui::UiNotice::position_sharing_waiting_for_fix;
                    frame.actions[0] = {ui::UiAction::stop_position_sharing, true};
                    break;
                case PortableUiPositionState::deferred:
                    frame.attention = ui::UiAttention::warning;
                    frame.notice = ui::UiNotice::position_sharing_deferred;
                    frame.actions[0] = {ui::UiAction::stop_position_sharing, true};
                    break;
                case PortableUiPositionState::failed:
                    frame.screen = ui::UiScreen::system_fault;
                    frame.attention = ui::UiAttention::critical;
                    frame.notice = ui::UiNotice::position_sharing_failed;
                    frame.action_count = 0;
                    return frame;
            }
            frame.actions[1] = {ui::UiAction::open_archive_controls, true};
            frame.actions[2] = {ui::UiAction::cancel, true};
            break;
        case PortableUiShellMode::quick_status_first:
        case PortableUiShellMode::quick_status_second:
            frame.screen = ui::UiScreen::quick_status_menu;
            frame.attention = ui::UiAttention::information;
            frame.action_count = 4;
            if (mode == PortableUiShellMode::quick_status_first) {
                frame.actions[0] = {ui::UiAction::select_quick_status_ok, true};
                frame.actions[1] = {ui::UiAction::select_quick_status_need_assistance, true};
                frame.actions[2] = {ui::UiAction::show_next_quick_status_page, true};
            } else {
                frame.actions[0] = {ui::UiAction::select_quick_status_anyone_online, true};
                frame.actions[1] = {ui::UiAction::select_quick_status_available_to_help, true};
                frame.actions[2] = {ui::UiAction::show_previous_quick_status_page, true};
            }
            frame.actions[3] = {ui::UiAction::cancel, true};
            break;
        case PortableUiShellMode::critical_confirmation:
            frame.screen = ui::UiScreen::critical_confirmation;
            frame.attention = ui::UiAttention::critical;
            frame.notice = ui::UiNotice::critical_alert_pending;
            frame.action_count = 2;
            frame.actions[0] = {ui::UiAction::confirm_critical_alert, true};
            frame.actions[1] = {ui::UiAction::cancel, true};
            break;
        case PortableUiShellMode::archive_controls:
            frame.screen = ui::UiScreen::archive_controls;
            frame.attention = ui::UiAttention::information;
            switch (snapshot.archive) {
                case PortableUiArchiveState::stopped:
                    frame.notice = ui::UiNotice::archive_stopped;
                    break;
                case PortableUiArchiveState::active:
                    frame.notice = ui::UiNotice::archive_active;
                    break;
                case PortableUiArchiveState::queued:
                    frame.notice = ui::UiNotice::archive_queued;
                    break;
                case PortableUiArchiveState::upload_waiting:
                    frame.notice = ui::UiNotice::archive_upload_waiting;
                    break;
                case PortableUiArchiveState::queue_full:
                    frame.attention = ui::UiAttention::warning;
                    frame.notice = ui::UiNotice::archive_queue_full;
                    break;
                case PortableUiArchiveState::upload_failed:
                case PortableUiArchiveState::unavailable:
                case PortableUiArchiveState::incoherent:
                    frame.attention = ui::UiAttention::warning;
                    frame.notice = ui::UiNotice::archive_upload_failed;
                    break;
            }
            frame.status.archive_queue_count_valid = true;
            if (!snapshot.status.archive_queue_count_valid) {
                frame.status.archive_queue_count = 0;
            }
            frame.action_count = 2;
            frame.actions[0] = {
                snapshot.archive == PortableUiArchiveState::stopped
                    ? ui::UiAction::request_archive_start
                    : ui::UiAction::request_archive_stop,
                true};
            frame.actions[1] = {ui::UiAction::cancel, true};
            break;
        case PortableUiShellMode::archive_start_confirmation:
        case PortableUiShellMode::archive_stop_confirmation:
            frame.screen = ui::UiScreen::archive_confirmation;
            frame.attention = ui::UiAttention::information;
            frame.notice = mode == PortableUiShellMode::archive_start_confirmation
                               ? ui::UiNotice::archive_start_confirmation
                               : ui::UiNotice::archive_stop_confirmation;
            frame.action_count = 2;
            frame.actions[0] = {
                mode == PortableUiShellMode::archive_start_confirmation
                    ? ui::UiAction::confirm_archive_start
                    : ui::UiAction::stop_archive,
                true};
            frame.actions[1] = {ui::UiAction::cancel, true};
            break;
        case PortableUiShellMode::message_center:
            frame.screen = ui::UiScreen::message_center;
            frame.attention = ui::UiAttention::information;
            frame.action_count = 4;
            frame.actions[0] = {ui::UiAction::open_inbox, true};
            frame.actions[1] = {ui::UiAction::open_outbox, true};
            frame.actions[2] = {ui::UiAction::open_compose,
                                snapshot.bridge_session_epoch != 0};
            frame.actions[3] = {ui::UiAction::cancel, true};
            break;
        case PortableUiShellMode::message_inbox:
        case PortableUiShellMode::message_outbox: {
            frame.screen = ui::UiScreen::message_list;
            frame.attention = ui::UiAttention::information;
            const auto list_kind =
                mode == PortableUiShellMode::message_inbox
                    ? ui::UiMessageListKind::inbox
                    : ui::UiMessageListKind::outbox;
            sidecar.messages.list_kind = list_kind;
            const auto count = filtered_count(snapshot, list_kind);
            sidecar.messages.page_count = static_cast<std::uint8_t>(
                std::max<std::uint16_t>(
                    1U, (static_cast<std::uint16_t>(count) +
                             ui::kMaxUiMessageRows - 1U) /
                            ui::kMaxUiMessageRows));
            sidecar.messages.page_index = message_page;
            const std::array<ui::UiAction, ui::kMaxUiMessageRows> row_actions{
                ui::UiAction::open_message_row_1,
                ui::UiAction::open_message_row_2};
            for (std::uint8_t row = 0; row < ui::kMaxUiMessageRows; ++row) {
                const auto offset = static_cast<std::uint8_t>(
                    message_page * ui::kMaxUiMessageRows + row);
                const auto* message = filtered_message(snapshot, list_kind, offset);
                if (message == nullptr) {
                    break;
                }
                const auto unread =
                    list_kind == ui::UiMessageListKind::inbox &&
                    !is_read(message->sequence);
                sidecar.owned_texts[row] = list_row_copy(
                    *message, unread,
                    list_kind == ui::UiMessageListKind::outbox);
                sidecar.messages.rows[row] = {
                    row,
                    list_kind == ui::UiMessageListKind::outbox
                        ? static_cast<ui::UiMessageDeliveryState>(
                              static_cast<unsigned>(message->delivery) + 1U)
                        : ui::UiMessageDeliveryState::none,
                    static_cast<ui::UiMessageKind>(
                        static_cast<unsigned>(message->kind) + 1U),
                    static_cast<ui::UiMessagePriority>(
                        static_cast<unsigned>(message->priority) + 1U),
                    unread};
                frame.actions[frame.action_count++] = {row_actions[row], true};
                ++sidecar.owned_text_count;
                ++sidecar.messages.row_count;
            }
            if (sidecar.messages.page_count > 1) {
                frame.actions[frame.action_count++] = {
                    ui::UiAction::show_next_message_page, true};
            }
            frame.actions[frame.action_count++] = {ui::UiAction::cancel, true};
            break;
        }
        case PortableUiShellMode::message_detail: {
            frame.screen = ui::UiScreen::message_detail;
            frame.attention = ui::UiAttention::information;
            const auto* message =
                message_by_sequence(snapshot, selected_message_sequence);
            if (message == nullptr) {
                break;
            }
            sidecar.messages.list_kind =
                message->direction == PortableUiMessageDirection::inbound
                    ? ui::UiMessageListKind::inbox
                    : ui::UiMessageListKind::outbox;
            sidecar.messages.detail_valid = true;
            sidecar.messages.detail_delivery =
                message->direction == PortableUiMessageDirection::outbound
                    ? static_cast<ui::UiMessageDeliveryState>(
                          static_cast<unsigned>(message->delivery) + 1U)
                    : ui::UiMessageDeliveryState::none;
            sidecar.messages.detail_kind = static_cast<ui::UiMessageKind>(
                static_cast<unsigned>(message->kind) + 1U);
            sidecar.messages.detail_priority =
                static_cast<ui::UiMessagePriority>(
                    static_cast<unsigned>(message->priority) + 1U);
            sidecar.messages.detail_unread = false;
            sidecar.messages.detail_acknowledge_available =
                message->acknowledge_available &&
                snapshot.bridge_session_epoch != 0;
            sidecar.owned_texts[0] = display_message_text(*message);
            sidecar.owned_texts[1] =
                message->direction == PortableUiMessageDirection::outbound
                    ? text_from(delivery_copy(message->delivery))
                    : text_from(message->kind == PortableUiMessageKind::alert
                                    ? "Inbound critical alert"
                                    : "Inbound message");
            sidecar.owned_text_count = 2;
            if (message->priority == PortableUiMessagePriority::critical) {
                frame.attention = ui::UiAttention::critical;
            }
            if (sidecar.messages.detail_acknowledge_available) {
                frame.actions[frame.action_count++] = {
                    ui::UiAction::acknowledge_inbound_alert, true};
            }
            frame.actions[frame.action_count++] = {ui::UiAction::cancel, true};
            break;
        }
        case PortableUiShellMode::message_compose: {
            frame.screen = ui::UiScreen::message_compose;
            frame.attention = ui::UiAttention::information;
            const auto first_template = static_cast<std::uint8_t>(
                message_page * 2U + 1U);
            const std::array<ui::UiAction, 2> select_actions{
                ui::UiAction::select_message_template_1,
                ui::UiAction::select_message_template_2};
            for (std::uint8_t slot = 0; slot < 2; ++slot) {
                sidecar.owned_texts[slot] = portable_ui_message_template(
                    static_cast<std::uint8_t>(first_template + slot));
                frame.actions[slot] = {select_actions[slot], true};
            }
            sidecar.owned_text_count = 2;
            frame.action_count = 4;
            frame.actions[2] = {ui::UiAction::show_next_compose_page, true};
            frame.actions[3] = {ui::UiAction::cancel, true};
            break;
        }
        case PortableUiShellMode::message_compose_confirmation:
            frame.screen = ui::UiScreen::message_compose_confirmation;
            frame.attention = ui::UiAttention::information;
            sidecar.messages.compose_template_id = selected_template_id;
            sidecar.owned_texts[0] =
                portable_ui_message_template(selected_template_id);
            sidecar.owned_text_count = 1;
            frame.action_count = 2;
            frame.actions[0] = {ui::UiAction::send_composed_message,
                                snapshot.bridge_session_epoch != 0};
            frame.actions[1] = {ui::UiAction::cancel, true};
            break;
        case PortableUiShellMode::request_pending:
            frame.screen = ui::UiScreen::status;
            frame.attention = request_kind == PortableUiRequestKind::critical_alert
                                  ? ui::UiAttention::critical
                                  : ui::UiAttention::information;
            frame.notice = request_kind == PortableUiRequestKind::critical_alert
                               ? ui::UiNotice::critical_alert_pending
                               : ui::UiNotice::none;
            break;
        case PortableUiShellMode::request_failed:
            frame.screen = ui::UiScreen::status;
            frame.attention = request_kind == PortableUiRequestKind::critical_alert
                                  ? ui::UiAttention::critical
                                  : ui::UiAttention::warning;
            frame.notice = failure_notice(request_kind);
            frame.action_count = 1;
            frame.actions[0] = {ui::UiAction::acknowledge_notice, true};
            break;
        case PortableUiShellMode::system_fault:
            frame.screen = ui::UiScreen::system_fault;
            frame.attention = ui::UiAttention::critical;
            frame.notice = ui::UiNotice::position_sharing_failed;
            break;
        case PortableUiShellMode::recovery_notice:
        case PortableUiShellMode::faulted:
        case PortableUiShellMode::inactive:
        case PortableUiShellMode::closed:
            break;
    }
    return frame;
}

PortableUiShellResult PortableUiShell::offer(
    PortableUiShellMode mode,
    const PortableUiSnapshot& snapshot,
    PendingEffect effect,
    PortableUiRequestKind request_kind,
    std::uint32_t request_id,
    std::uint32_t generation,
    std::uint8_t message_page,
    std::uint8_t selected_template_id,
    std::uint64_t selected_message_sequence) {
    if (pending_.present) {
        return reject(PortableUiShellError::pending_offer);
    }
    if (shell_status_.active_revision == std::numeric_limits<std::uint32_t>::max()) {
        latch(PortableUiShellError::revision_exhausted);
        return reject(PortableUiShellError::revision_exhausted);
    }
    const auto offered_generation = generation == 0
                                        ? shell_status_.generation
                                        : generation;
    const auto revision = shell_status_.active_revision + 1U;
    ui::UiPresentationSidecar sidecar{};
    auto frame = frame_for(mode, revision, snapshot, request_kind,
                           message_page, selected_template_id,
                           selected_message_sequence, sidecar);
    const auto validation = local_interface_.validate_candidate(frame);
    if (validation != ui::PresentError::none ||
        !ui::valid_portable_ui_presentation(frame, sidecar)) {
        latch(PortableUiShellError::display_failed);
        PortableUiShellResult result{};
        result.error = PortableUiShellError::display_failed;
        result.present_error = validation;
        result.generation = offered_generation;
        result.revision = revision;
        return result;
    }
    pending_ = {frame, sidecar, mode, effect, request_kind, request_id,
                offered_generation, snapshot, message_page,
                selected_template_id, selected_message_sequence, true};
    PortableUiShellResult result{};
    result.disposition = PortableUiShellDisposition::offer_ready;
    result.generation = offered_generation;
    result.revision = revision;
    result.has_offer = true;
    return result;
}

PortableUiShellResult PortableUiShell::reject(PortableUiShellError error) const {
    PortableUiShellResult result{};
    result.error = error;
    result.generation = shell_status_.generation;
    result.revision = shell_status_.active_revision;
    return result;
}

void PortableUiShell::latch(PortableUiShellError error) {
    shell_status_.mode = PortableUiShellMode::faulted;
    shell_status_.latched_error = error;
}

ui::UiOwnedText portable_ui_message_template(std::uint8_t template_id) {
    if (template_id == 0 || template_id > kMessageTemplates.size()) {
        return {};
    }
    return text_from(kMessageTemplates[template_id - 1U]);
}

}  // namespace opentrail::integration
