#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

#include "fake_local_interface.hpp"
#include "opentrail/portable_ui_render_plan.hpp"
#include "opentrail/portable_ui_shell.hpp"
#include "opentrail/update_recovery_diagnostics.hpp"

namespace {

using opentrail::integration::PortableUiRequestKind;
using opentrail::integration::PortableUiArchiveState;
using opentrail::integration::PortableUiPositionState;
using opentrail::integration::PortableUiMessage;
using opentrail::integration::PortableUiMessageDeliveryState;
using opentrail::integration::PortableUiMessageDirection;
using opentrail::integration::PortableUiMessageKind;
using opentrail::integration::PortableUiMessagePriority;
using opentrail::integration::PortableUiSnapshot;
using opentrail::integration::PortableUiShell;
using opentrail::integration::PortableUiShellDisposition;
using opentrail::integration::PortableUiShellResult;
using opentrail::ui::CheckedLocalInterface;
using opentrail::ui::DisplayCapabilities;
using opentrail::ui::DisplayWriteError;
using opentrail::ui::InputGesture;
using opentrail::ui::UiAction;
using opentrail::ui::UiIndicatorState;
using opentrail::ui::UiScreen;
using opentrail::ui::UiStatusSummary;
using opentrail::ui::test_support::FakeDisplaySink;
using opentrail::ui::test_support::FakeLocalInputSource;

int failures = 0;

#define EXPECT(condition)                                                       \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n";   \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

DisplayCapabilities capabilities() {
    return {480, 480, 16, 4, true, false, true};
}

UiStatusSummary summary() {
    UiStatusSummary value{};
    value.radio = UiIndicatorState::normal;
    value.position = UiIndicatorState::warning;
    value.power = UiIndicatorState::normal;
    value.peer_count_valid = true;
    value.peer_count = 2;
    value.unread_messages = 0;
    value.archive_queue_count_valid = true;
    value.archive_queue_count = 0;
    return value;
}

void expect_round_plan(
    const opentrail::ui::UiFrame& frame,
    const opentrail::ui::UiPresentationSidecar& sidecar = {}) {
    const auto profile = opentrail::ui::kSimulatorLogicalDisplayProfile;
    const auto result = opentrail::ui::make_portable_ui_render_plan(
        frame, sidecar, profile);
    EXPECT(result.ok());
    if (!result.ok()) {
        return;
    }
    EXPECT(result.plan.width == 466);
    EXPECT(result.plan.height == 466);
    EXPECT(result.plan.shape == opentrail::ui::UiViewportShape::circle);
    const std::int64_t center = 233;
    const std::int64_t radius_squared = center * center;
    for (std::size_t index = 0; index < result.plan.primitive_count; ++index) {
        const auto& primitive = result.plan.primitives[index];
        if (primitive.kind == opentrail::ui::UiRenderPrimitiveKind::panel) {
            continue;
        }
        const std::int64_t left = primitive.bounds.x;
        const std::int64_t top = primitive.bounds.y;
        const std::int64_t right = left + primitive.bounds.width;
        const std::int64_t bottom = top + primitive.bounds.height;
        const std::int64_t xs[]{left, right};
        const std::int64_t ys[]{top, bottom};
        for (const auto x : xs) {
            for (const auto y : ys) {
                const auto dx = x - center;
                const auto dy = y - center;
                EXPECT(dx * dx + dy * dy <= radius_squared);
            }
        }
        if (primitive.kind == opentrail::ui::UiRenderPrimitiveKind::action) {
            EXPECT(primitive.bounds.width >= profile.minimum_action_extent);
            EXPECT(primitive.bounds.height >= profile.minimum_action_extent);
        }
    }
}

PortableUiSnapshot snapshot() {
    PortableUiSnapshot value{};
    value.status = summary();
    value.bridge_session_epoch = 1;
    return value;
}

opentrail::ui::UiOwnedText owned_text(const char* value) {
    opentrail::ui::UiOwnedText result{};
    while (value[result.byte_count] != '\0') {
        EXPECT(result.byte_count < opentrail::ui::kMaxUiOwnedTextBytes);
        result.bytes[result.byte_count] = value[result.byte_count];
        ++result.byte_count;
    }
    return result;
}

bool text_is(const opentrail::ui::UiOwnedText& text, std::string_view expected) {
    return text.byte_count == expected.size() &&
           std::string_view(text.bytes.data(), text.byte_count) == expected;
}

PortableUiMessage message(
    std::uint64_t sequence,
    PortableUiMessageDirection direction,
    PortableUiMessageKind kind,
    PortableUiMessagePriority priority,
    PortableUiMessageDeliveryState delivery,
    const char* text,
    bool acknowledge = false) {
    PortableUiMessage result{};
    result.sequence = sequence;
    result.direction = direction;
    result.kind = kind;
    result.priority = priority;
    result.delivery = delivery;
    result.text = owned_text(text);
    result.acknowledge_available = acknowledge;
    return result;
}

PortableUiSnapshot message_snapshot(std::uint64_t epoch = 1) {
    auto value = snapshot();
    value.bridge_session_epoch = epoch;
    value.message_count = 3;
    value.messages[0] = message(
        1, PortableUiMessageDirection::inbound, PortableUiMessageKind::chat,
        PortableUiMessagePriority::normal,
        PortableUiMessageDeliveryState::bridge_observed, "Trail clear");
    value.messages[1] = message(
        2, PortableUiMessageDirection::outbound, PortableUiMessageKind::chat,
        PortableUiMessagePriority::normal,
        PortableUiMessageDeliveryState::bridge_accepted, "Checking in");
    value.messages[2] = message(
        3, PortableUiMessageDirection::inbound, PortableUiMessageKind::alert,
        PortableUiMessagePriority::critical,
        PortableUiMessageDeliveryState::bridge_observed, "Please confirm", true);
    return value;
}

struct Harness {
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface local{display, input, capabilities()};
    PortableUiShell shell{local};

    void start(PortableUiSnapshot initial = snapshot()) {
        const auto offered = shell.prepare_activate(initial);
        EXPECT(offered.has_offer);
        EXPECT(offered.generation == 1);
        EXPECT(offered.revision == 1);
        const auto frame = shell.pending_frame();
        expect_round_plan(frame, shell.presentation_sidecar());
        EXPECT(shell.commit_present(1, 1).disposition ==
               PortableUiShellDisposition::committed);
    }

    void activate_slot(std::uint8_t slot, InputGesture gesture = InputGesture::activate) {
        const auto state = shell.status();
        EXPECT(input.enqueue_action(state.active_revision, slot, gesture));
        const auto offered = shell.prepare_input();
        EXPECT(offered.has_offer);
        expect_round_plan(shell.pending_frame(), shell.presentation_sidecar());
        EXPECT(shell.commit_present(state.generation, offered.revision).error ==
               opentrail::integration::PortableUiShellError::none);
    }
};

bool bridge_request(PortableUiRequestKind kind) {
    return kind == PortableUiRequestKind::quick_status_ok ||
           kind == PortableUiRequestKind::quick_status_need_assistance ||
           kind == PortableUiRequestKind::quick_status_anyone_online ||
           kind == PortableUiRequestKind::quick_status_available_to_help ||
           kind == PortableUiRequestKind::critical_alert ||
           kind == PortableUiRequestKind::message_template_send ||
           kind == PortableUiRequestKind::message_alert_acknowledge;
}

PortableUiShellResult complete(
    PortableUiShell& shell,
    std::uint32_t generation,
    std::uint32_t revision,
    std::uint32_t request_id,
    PortableUiRequestKind request_kind,
    bool succeeded,
    const PortableUiSnapshot& value) {
    const auto pending = shell.status();
    auto completion_snapshot = value;
    std::uint64_t applied_sequence = 0;
    if (succeeded && bridge_request(request_kind) &&
        request_kind != PortableUiRequestKind::message_template_send &&
        request_kind != PortableUiRequestKind::message_alert_acknowledge) {
        applied_sequence = completion_snapshot.message_count == 0
                               ? 1
                               : completion_snapshot.messages[
                                     completion_snapshot.message_count - 1U]
                                         .sequence + 1U;
        const auto message_kind =
            request_kind == PortableUiRequestKind::critical_alert
                ? PortableUiMessageKind::alert
                : PortableUiMessageKind::quick_status;
        const auto priority =
            request_kind == PortableUiRequestKind::critical_alert
                ? PortableUiMessagePriority::critical
                : PortableUiMessagePriority::important;
        completion_snapshot.messages[completion_snapshot.message_count++] =
            message(applied_sequence, PortableUiMessageDirection::outbound,
                    message_kind, priority,
                    PortableUiMessageDeliveryState::bridge_accepted,
                    request_kind == PortableUiRequestKind::critical_alert
                        ? "Critical alert"
                        : (request_kind == PortableUiRequestKind::quick_status_ok
                               ? "I'm OK"
                               : (request_kind ==
                                          PortableUiRequestKind::
                                              quick_status_need_assistance
                                      ? "Need assistance"
                                      : (request_kind ==
                                                 PortableUiRequestKind::
                                                     quick_status_anyone_online
                                             ? "Anyone online?"
                                             : "Available to help"))));
    }
    return shell.prepare_completion(
        generation, revision, request_id, request_kind, succeeded,
        succeeded && bridge_request(request_kind)
            ? pending.pending_request_bridge_session_epoch
            : 0,
        applied_sequence,
        pending.pending_request_template_id,
        pending.pending_request_message_sequence,
        completion_snapshot);
}

void two_phase_start_and_render_plan() {
    Harness harness{};
    const auto offered = harness.shell.prepare_activate(snapshot());
    EXPECT(offered.has_offer);
    EXPECT(!harness.local.status().has_active_frame);
    const auto pending = harness.shell.pending_frame();
    expect_round_plan(pending, harness.shell.presentation_sidecar());
    const auto plan = opentrail::ui::make_portable_ui_render_plan(
        pending, opentrail::ui::kSimulatorLogicalDisplayProfile);
    EXPECT(plan.plan.frame_revision == offered.revision);
    EXPECT(harness.shell.commit_present(offered.generation, offered.revision).disposition ==
           PortableUiShellDisposition::committed);
    EXPECT(harness.local.status().active_revision == 1);
}

void quick_status_pages_and_request_correlation() {
    Harness harness{};
    harness.start();
    harness.activate_slot(2);
    auto frame = harness.display.latest_frame();
    EXPECT(frame.screen == UiScreen::quick_status_menu);
    EXPECT(frame.actions[0].action == UiAction::select_quick_status_ok);
    harness.activate_slot(2);
    frame = harness.display.latest_frame();
    EXPECT(frame.actions[0].action == UiAction::select_quick_status_anyone_online);

    const auto state = harness.shell.status();
    EXPECT(harness.input.enqueue_action(state.active_revision, 0));
    const auto offered = harness.shell.prepare_input();
    EXPECT(harness.shell.status().pending_request_id == 0);
    const auto committed = harness.shell.commit_present(state.generation, offered.revision);
    EXPECT(committed.disposition == PortableUiShellDisposition::request_emitted);
    EXPECT(committed.request_kind == PortableUiRequestKind::quick_status_anyone_online);
    EXPECT(committed.request_id == 1);

    const auto pending = harness.shell.status();
    EXPECT(complete(harness.shell,
               pending.generation, pending.active_revision, committed.request_id,
               PortableUiRequestKind::critical_alert, true, snapshot()).error ==
           opentrail::integration::PortableUiShellError::request_mismatch);
    const auto completion = complete(harness.shell,
        pending.generation, pending.active_revision, committed.request_id,
        committed.request_kind, true, snapshot());
    EXPECT(completion.has_offer);
    EXPECT(harness.shell.commit_present(pending.generation, completion.revision).disposition ==
           PortableUiShellDisposition::committed);
    EXPECT(harness.display.latest_frame().screen == UiScreen::home);
}

void held_confirmation_and_stale_input() {
    Harness harness{};
    harness.start();
    harness.activate_slot(3);
    auto state = harness.shell.status();
    EXPECT(harness.input.enqueue_action(state.active_revision - 1U, 0, InputGesture::hold));
    auto rejected = harness.shell.prepare_input();
    EXPECT(rejected.disposition == PortableUiShellDisposition::input_rejected);
    EXPECT(rejected.action_error == opentrail::ui::ActionResolutionError::stale_frame);
    EXPECT(harness.input.enqueue_action(state.active_revision, 0));
    rejected = harness.shell.prepare_input();
    EXPECT(rejected.action_error == opentrail::ui::ActionResolutionError::hold_required);
    EXPECT(harness.input.enqueue_action(state.active_revision, 0, InputGesture::hold));
    const auto offered = harness.shell.prepare_input();
    const auto committed = harness.shell.commit_present(state.generation, offered.revision);
    EXPECT(committed.request_kind == PortableUiRequestKind::critical_alert);
}

void sink_not_ready_preserves_exact_offer() {
    Harness harness{};
    const auto offered = harness.shell.prepare_activate(snapshot());
    EXPECT(harness.display.enqueue_result(DisplayWriteError::not_ready));
    const auto deferred = harness.shell.commit_present(offered.generation, offered.revision);
    EXPECT(deferred.has_offer);
    EXPECT(harness.shell.status().has_pending_offer);
    EXPECT(harness.shell.pending_frame().revision == offered.revision);
    EXPECT(harness.shell.commit_present(offered.generation, offered.revision).disposition ==
           PortableUiShellDisposition::committed);
}

void refresh_is_generation_and_revision_bound() {
    Harness harness{};
    harness.start();
    auto updated = snapshot();
    updated.status.peer_count = 4;
    const auto state = harness.shell.status();
    EXPECT(harness.shell.prepare_refresh(
               state.generation + 1U, state.active_revision, updated).error ==
           opentrail::integration::PortableUiShellError::generation_mismatch);
    const auto offered = harness.shell.prepare_refresh(
        state.generation, state.active_revision, updated);
    EXPECT(offered.has_offer);
    EXPECT(harness.shell.commit_present(state.generation, offered.revision).disposition ==
           PortableUiShellDisposition::committed);
    EXPECT(harness.display.latest_frame().status.peer_count == 4);

    const auto after = harness.shell.status();
    const auto unchanged = harness.shell.prepare_refresh(
        after.generation, after.active_revision, updated);
    EXPECT(unchanged.disposition == PortableUiShellDisposition::idle);
    EXPECT(unchanged.revision == after.active_revision);
}

void position_actions_are_typed_and_injected() {
    Harness harness{};
    harness.start();
    harness.activate_slot(0);
    auto frame = harness.display.latest_frame();
    EXPECT(frame.notice == opentrail::ui::UiNotice::position_sharing_stopped);
    EXPECT(frame.actions[0].action == UiAction::start_position_sharing);
    EXPECT(frame.actions[1].action == UiAction::open_archive_controls);
    EXPECT(frame.actions[2].action == UiAction::cancel);

    auto state = harness.shell.status();
    EXPECT(harness.input.enqueue_action(state.active_revision, 0));
    auto offer = harness.shell.prepare_input();
    auto request = harness.shell.commit_present(state.generation, offer.revision);
    EXPECT(request.request_kind == PortableUiRequestKind::position_start);
    auto active = snapshot();
    active.position = PortableUiPositionState::active;
    state = harness.shell.status();
    offer = complete(harness.shell,
        state.generation, state.active_revision, request.request_id,
        request.request_kind, true, active);
    EXPECT(harness.shell.commit_present(state.generation, offer.revision).error ==
           opentrail::integration::PortableUiShellError::none);
    harness.activate_slot(0);
    frame = harness.display.latest_frame();
    EXPECT(frame.notice == opentrail::ui::UiNotice::position_sharing_active);
    EXPECT(frame.actions[0].action == UiAction::stop_position_sharing);
}

void archive_states_preserve_canonical_notice_and_stop_policy() {
    const PortableUiArchiveState states[] = {
        PortableUiArchiveState::stopped,
        PortableUiArchiveState::active,
        PortableUiArchiveState::queued,
        PortableUiArchiveState::upload_waiting,
        PortableUiArchiveState::queue_full,
        PortableUiArchiveState::upload_failed,
        PortableUiArchiveState::unavailable,
        PortableUiArchiveState::incoherent,
    };
    const opentrail::ui::UiNotice notices[] = {
        opentrail::ui::UiNotice::archive_stopped,
        opentrail::ui::UiNotice::archive_active,
        opentrail::ui::UiNotice::archive_queued,
        opentrail::ui::UiNotice::archive_upload_waiting,
        opentrail::ui::UiNotice::archive_queue_full,
        opentrail::ui::UiNotice::archive_upload_failed,
        opentrail::ui::UiNotice::archive_upload_failed,
        opentrail::ui::UiNotice::archive_upload_failed,
    };
    for (std::size_t index = 0; index < std::size(states); ++index) {
        Harness harness{};
        auto initial = snapshot();
        initial.archive = states[index];
        initial.status.archive_queue_count_valid = index < 6;
        const std::uint8_t counts[] = {0, 0, 3, 3, 16, 2, 0, 0};
        initial.status.archive_queue_count = counts[index];
        harness.start(initial);
        harness.activate_slot(0);
        harness.activate_slot(1);
        const auto frame = harness.display.latest_frame();
        EXPECT(frame.notice == notices[index]);
        EXPECT(frame.status.archive_queue_count_valid);
        EXPECT(frame.actions[0].action ==
               (states[index] == PortableUiArchiveState::stopped
                    ? UiAction::request_archive_start
                    : UiAction::request_archive_stop));
        EXPECT(frame.actions[1].action == UiAction::cancel);
    }
}

std::uint32_t recovery_word(
    opentrail::update::UpdateRecoveryOperatorState state) {
    opentrail::update::UpdateRecoveryStatus status{};
    status.operation = opentrail::update::UpdateRecoveryStatusOperation::boot;
    status.state = state;
    using State = opentrail::update::UpdateRecoveryOperatorState;
    using Reason = opentrail::update::UpdateRecoveryOperatorReason;
    using Action = opentrail::update::UpdateRecoveryOperatorAction;
    if (state == State::trial_active) {
        status.operation_succeeded = true;
        status.normal_operation_blocked = false;
        status.attention_required = false;
        status.reason = Reason::trial_confirmation_required;
        status.action = Action::continue_trial;
        status.confirmation_required = true;
    } else if (state == State::safe_mode) {
        status.reason = Reason::rollback_detected;
    }
    const auto encoded = opentrail::diagnostics::encode_update_recovery_diagnostic(status);
    EXPECT(encoded.encoded());
    return encoded.word;
}

void recovery_mapping_and_fault_containment() {
    auto trial = snapshot();
    trial.recovery_diagnostic_valid = true;
    trial.recovery_diagnostic_word = recovery_word(
        opentrail::update::UpdateRecoveryOperatorState::trial_active);
    Harness warning{};
    warning.start(trial);
    auto frame = warning.display.latest_frame();
    EXPECT(frame.screen == UiScreen::status);
    EXPECT(frame.notice == opentrail::ui::UiNotice::update_trial_active);
    EXPECT(frame.action_count == 1);
    EXPECT(frame.actions[0].action == UiAction::acknowledge_notice);

    auto safe = snapshot();
    safe.recovery_diagnostic_valid = true;
    safe.recovery_diagnostic_word = recovery_word(
        opentrail::update::UpdateRecoveryOperatorState::safe_mode);
    Harness blocked{};
    blocked.start(safe);
    frame = blocked.display.latest_frame();
    EXPECT(frame.screen == UiScreen::system_fault);
    EXPECT(frame.notice == opentrail::ui::UiNotice::update_safe_mode);
    EXPECT(frame.action_count == 0);
}

PortableUiShellResult emit_quick_status_request(Harness& harness) {
    harness.start();
    harness.activate_slot(2);
    const auto state = harness.shell.status();
    EXPECT(harness.input.enqueue_action(state.active_revision, 0));
    const auto offered = harness.shell.prepare_input();
    EXPECT(offered.has_offer);
    expect_round_plan(harness.shell.pending_frame(),
                      harness.shell.presentation_sidecar());
    const auto request = harness.shell.commit_present(
        state.generation, offered.revision);
    EXPECT(request.disposition == PortableUiShellDisposition::request_emitted);
    return request;
}

PortableUiShellResult emit_critical_request(Harness& harness) {
    harness.start();
    harness.activate_slot(3);
    const auto state = harness.shell.status();
    EXPECT(harness.input.enqueue_action(
        state.active_revision, 0, InputGesture::hold));
    const auto offered = harness.shell.prepare_input();
    EXPECT(offered.has_offer);
    expect_round_plan(harness.shell.pending_frame(),
                      harness.shell.presentation_sidecar());
    return harness.shell.commit_present(state.generation, offered.revision);
}

void refresh_preserves_request_lifecycle_and_fault_reconciliation() {
    Harness normal{};
    const auto request = emit_quick_status_request(normal);
    auto updated = snapshot();
    updated.status.peer_count = 7;
    auto state = normal.shell.status();
    auto offered = normal.shell.prepare_refresh(
        state.generation, state.active_revision, updated);
    EXPECT(offered.has_offer);
    EXPECT(normal.shell.commit_present(state.generation, offered.revision).disposition ==
           PortableUiShellDisposition::committed);
    state = normal.shell.status();
    EXPECT(state.mode == opentrail::integration::PortableUiShellMode::request_pending);
    EXPECT(state.pending_request_id == request.request_id);
    offered = complete(normal.shell,
        state.generation, state.active_revision, request.request_id,
        request.request_kind, true, updated);
    EXPECT(offered.has_offer);
    EXPECT(normal.shell.commit_present(state.generation, offered.revision).disposition ==
           PortableUiShellDisposition::committed);

    Harness blocked{};
    const auto blocked_request = emit_quick_status_request(blocked);
    auto failed = snapshot();
    failed.position = PortableUiPositionState::failed;
    state = blocked.shell.status();
    offered = blocked.shell.prepare_refresh(
        state.generation, state.active_revision, failed);
    EXPECT(offered.has_offer);
    EXPECT(blocked.shell.commit_present(state.generation, offered.revision).disposition ==
           PortableUiShellDisposition::committed);
    state = blocked.shell.status();
    EXPECT(state.mode == opentrail::integration::PortableUiShellMode::system_fault);
    EXPECT(state.pending_request_id == blocked_request.request_id);
    offered = complete(blocked.shell,
        state.generation, state.active_revision, blocked_request.request_id,
        blocked_request.request_kind, false, failed);
    EXPECT(offered.has_offer);
    EXPECT(blocked.shell.pending_frame().screen == UiScreen::system_fault);
    EXPECT(blocked.shell.commit_present(state.generation, offered.revision).disposition ==
           PortableUiShellDisposition::committed);
    EXPECT(blocked.shell.status().pending_request_id == 0);
}

void recovery_priority_and_acknowledgement_recurrence() {
    auto trial = snapshot();
    trial.recovery_diagnostic_valid = true;
    trial.recovery_diagnostic_word = recovery_word(
        opentrail::update::UpdateRecoveryOperatorState::trial_active);

    Harness failed_request{};
    const auto request = emit_quick_status_request(failed_request);
    auto state = failed_request.shell.status();
    auto offered = complete(failed_request.shell,
        state.generation, state.active_revision, request.request_id,
        request.request_kind, false, trial);
    EXPECT(offered.has_offer);
    EXPECT(failed_request.shell.pending_frame().notice ==
           opentrail::ui::UiNotice::update_trial_active);
    EXPECT(failed_request.shell.commit_present(state.generation, offered.revision).disposition ==
           PortableUiShellDisposition::committed);
    failed_request.activate_slot(0);
    EXPECT(failed_request.display.latest_frame().notice ==
           opentrail::ui::UiNotice::message_failed);
    auto unrelated = trial;
    unrelated.status.peer_count = 9;
    state = failed_request.shell.status();
    offered = failed_request.shell.prepare_refresh(
        state.generation, state.active_revision, unrelated);
    EXPECT(offered.has_offer);
    EXPECT(failed_request.shell.pending_frame().notice ==
           opentrail::ui::UiNotice::message_failed);
    EXPECT(failed_request.shell.commit_present(state.generation, offered.revision).disposition ==
           PortableUiShellDisposition::committed);
    failed_request.activate_slot(0);
    EXPECT(failed_request.display.latest_frame().screen == UiScreen::home);

    Harness recurrence{};
    recurrence.start(trial);
    recurrence.activate_slot(0);
    EXPECT(recurrence.display.latest_frame().screen == UiScreen::home);
    auto changed = trial;
    changed.status.peer_count = 8;
    state = recurrence.shell.status();
    offered = recurrence.shell.prepare_refresh(
        state.generation, state.active_revision, changed);
    EXPECT(offered.has_offer);
    EXPECT(recurrence.shell.pending_frame().screen == UiScreen::home);
    EXPECT(recurrence.shell.commit_present(state.generation, offered.revision).disposition ==
           PortableUiShellDisposition::committed);

    auto absent = changed;
    absent.recovery_diagnostic_valid = false;
    absent.recovery_diagnostic_word = 0;
    state = recurrence.shell.status();
    offered = recurrence.shell.prepare_refresh(
        state.generation, state.active_revision, absent);
    EXPECT(offered.has_offer);
    EXPECT(recurrence.shell.commit_present(state.generation, offered.revision).disposition ==
           PortableUiShellDisposition::committed);

    auto recurring = absent;
    recurring.recovery_diagnostic_valid = true;
    recurring.recovery_diagnostic_word = trial.recovery_diagnostic_word;
    state = recurrence.shell.status();
    offered = recurrence.shell.prepare_refresh(
        state.generation, state.active_revision, recurring);
    EXPECT(offered.has_offer);
    EXPECT(recurrence.shell.pending_frame().notice ==
           opentrail::ui::UiNotice::update_trial_active);

    Harness critical{};
    const auto critical_request = emit_critical_request(critical);
    state = critical.shell.status();
    offered = complete(critical.shell,
        state.generation, state.active_revision, critical_request.request_id,
        critical_request.request_kind, false, snapshot());
    EXPECT(offered.has_offer);
    EXPECT(critical.shell.commit_present(state.generation, offered.revision).disposition ==
           PortableUiShellDisposition::committed);
    auto peer_update = snapshot();
    peer_update.status.peer_count = 10;
    state = critical.shell.status();
    offered = critical.shell.prepare_refresh(
        state.generation, state.active_revision, peer_update);
    EXPECT(offered.has_offer);
    EXPECT(critical.shell.pending_frame().notice ==
           opentrail::ui::UiNotice::critical_alert_failed);

    Harness faulted_failure{};
    const auto faulted_request = emit_quick_status_request(faulted_failure);
    auto fault = snapshot();
    fault.position = PortableUiPositionState::failed;
    state = faulted_failure.shell.status();
    offered = complete(faulted_failure.shell,
        state.generation, state.active_revision, faulted_request.request_id,
        faulted_request.request_kind, false, fault);
    EXPECT(offered.has_offer);
    EXPECT(faulted_failure.shell.commit_present(state.generation, offered.revision).disposition ==
           PortableUiShellDisposition::committed);
    state = faulted_failure.shell.status();
    offered = faulted_failure.shell.prepare_refresh(
        state.generation, state.active_revision, snapshot());
    EXPECT(offered.has_offer);
    EXPECT(faulted_failure.shell.pending_frame().notice ==
           opentrail::ui::UiNotice::message_failed);
}

void message_inbox_read_compose_and_delivery_are_cpp_owned() {
    Harness harness{};
    auto messages = message_snapshot();
    harness.start(messages);
    EXPECT(harness.display.latest_frame().status.unread_messages == 2);

    harness.activate_slot(1);
    auto frame = harness.display.latest_frame();
    EXPECT(frame.screen == UiScreen::message_center);
    EXPECT(frame.actions[0].action == UiAction::open_inbox);
    EXPECT(frame.actions[2].action == UiAction::open_compose);

    harness.activate_slot(0);
    frame = harness.display.latest_frame();
    EXPECT(frame.screen == UiScreen::message_list);
    EXPECT(harness.shell.presentation_sidecar().messages.row_count == 2);
    EXPECT(harness.shell.presentation_sidecar().messages.rows[0].unread);
    EXPECT(harness.shell.presentation_sidecar().messages.rows[0].priority ==
           opentrail::ui::UiMessagePriority::critical);
    auto row_mismatch = frame;
    row_mismatch.action_count = 1;
    row_mismatch.actions = {};
    row_mismatch.actions[0] = {UiAction::cancel, true};
    EXPECT(opentrail::ui::valid_ui_frame(row_mismatch, capabilities()));
    EXPECT(!opentrail::ui::valid_portable_ui_presentation(
        row_mismatch, harness.shell.presentation_sidecar()));
    auto plan = opentrail::ui::make_portable_ui_render_plan(
        frame, harness.shell.presentation_sidecar(),
        opentrail::ui::kSimulatorLogicalDisplayProfile);
    EXPECT(plan.ok());
    EXPECT(plan.plan.primitives[2].style ==
           opentrail::ui::UiRenderStyle::critical);

    harness.activate_slot(0);
    frame = harness.display.latest_frame();
    EXPECT(frame.screen == UiScreen::message_detail);
    EXPECT(frame.status.unread_messages == 1);
    EXPECT(harness.shell.presentation_sidecar().messages.detail_priority ==
           opentrail::ui::UiMessagePriority::critical);
    EXPECT(frame.actions[0].action == UiAction::acknowledge_inbound_alert);

    harness.activate_slot(1);
    frame = harness.display.latest_frame();
    EXPECT(frame.screen == UiScreen::message_list);
    EXPECT(!harness.shell.presentation_sidecar().messages.rows[0].unread);
    EXPECT(frame.status.unread_messages == 1);

    harness.activate_slot(2);
    harness.activate_slot(2);
    frame = harness.display.latest_frame();
    EXPECT(frame.screen == UiScreen::message_compose);
    EXPECT(harness.shell.presentation_sidecar().owned_text_count == 2);
    EXPECT(frame.actions[2].action == UiAction::show_next_compose_page);
    harness.activate_slot(0);
    frame = harness.display.latest_frame();
    EXPECT(frame.screen == UiScreen::message_compose_confirmation);
    EXPECT(harness.shell.presentation_sidecar().messages.compose_template_id == 1);
    EXPECT(frame.actions[0].action == UiAction::send_composed_message);
    auto invalid_confirmation = harness.shell.presentation_sidecar();
    invalid_confirmation.messages.compose_template_id = 0;
    EXPECT(!opentrail::ui::valid_portable_ui_presentation(
        frame, invalid_confirmation));
    invalid_confirmation.messages.compose_template_id = 9;
    EXPECT(!opentrail::ui::valid_portable_ui_presentation(
        frame, invalid_confirmation));

    auto state = harness.shell.status();
    EXPECT(harness.input.enqueue_action(state.active_revision, 0));
    auto offered = harness.shell.prepare_input();
    auto request = harness.shell.commit_present(state.generation, offered.revision);
    EXPECT(request.disposition == PortableUiShellDisposition::request_emitted);
    EXPECT(request.request_kind == PortableUiRequestKind::message_template_send);
    EXPECT(request.request_template_id == 1);
    EXPECT(request.request_bridge_session_epoch == 1);
    state = harness.shell.status();
    EXPECT(harness.shell.prepare_completion(
               state.generation, state.active_revision, request.request_id,
               request.request_kind, true, 2, 4, request.request_template_id,
               0, messages).error ==
           opentrail::integration::PortableUiShellError::request_mismatch);

    offered = complete(harness.shell, state.generation, state.active_revision,
                       request.request_id, request.request_kind, false, messages);
    EXPECT(offered.has_offer);
    EXPECT(harness.shell.commit_present(state.generation, offered.revision).disposition ==
           PortableUiShellDisposition::committed);
    EXPECT(harness.display.latest_frame().notice ==
           opentrail::ui::UiNotice::message_failed);
    harness.activate_slot(0);
    frame = harness.display.latest_frame();
    EXPECT(frame.screen == UiScreen::message_compose_confirmation);
    EXPECT(harness.shell.presentation_sidecar().messages.compose_template_id == 1);
    EXPECT(harness.shell.presentation_sidecar().owned_texts[0].byte_count == 11);

    state = harness.shell.status();
    EXPECT(harness.input.enqueue_action(state.active_revision, 0));
    offered = harness.shell.prepare_input();
    request = harness.shell.commit_present(state.generation, offered.revision);
    auto sent = messages;
    sent.message_count = 4;
    sent.messages[3] = message(
        4, PortableUiMessageDirection::outbound, PortableUiMessageKind::chat,
        PortableUiMessagePriority::normal,
        PortableUiMessageDeliveryState::bridge_accepted, "Checking in");
    state = harness.shell.status();
    offered = harness.shell.prepare_completion(
        state.generation, state.active_revision, request.request_id,
        request.request_kind, true, 1, 4, request.request_template_id, 0, sent);
    EXPECT(offered.has_offer);
    EXPECT(harness.shell.commit_present(state.generation, offered.revision).disposition ==
           PortableUiShellDisposition::committed);
    frame = harness.display.latest_frame();
    EXPECT(frame.screen == UiScreen::message_list);
    EXPECT(harness.shell.presentation_sidecar().messages.list_kind ==
           opentrail::ui::UiMessageListKind::outbox);
    EXPECT(harness.shell.presentation_sidecar().messages.rows[0].delivery ==
           opentrail::ui::UiMessageDeliveryState::bridge_accepted);
}

void message_epoch_and_acknowledgement_races_fail_closed() {
    Harness harness{};
    auto first = message_snapshot(1);
    harness.start(first);
    harness.activate_slot(1);
    harness.activate_slot(0);
    harness.activate_slot(0);
    auto state = harness.shell.status();
    EXPECT(harness.input.enqueue_action(
        state.active_revision, 0, InputGesture::hold));
    auto offered = harness.shell.prepare_input();
    auto request = harness.shell.commit_present(state.generation, offered.revision);
    EXPECT(request.request_kind ==
           PortableUiRequestKind::message_alert_acknowledge);
    EXPECT(request.request_message_sequence == 3);

    auto second = message_snapshot(2);
    second.status.peer_count = 4;
    second.messages[2].acknowledge_available = false;
    second.message_count = 4;
    second.messages[3] = message(
        4, PortableUiMessageDirection::outbound,
        PortableUiMessageKind::acknowledgement,
        PortableUiMessagePriority::important,
        PortableUiMessageDeliveryState::bridge_accepted, "Acknowledged");
    state = harness.shell.status();
    offered = harness.shell.prepare_refresh(
        state.generation, state.active_revision, second);
    EXPECT(offered.has_offer);
    EXPECT(harness.shell.commit_present(state.generation, offered.revision).disposition ==
           PortableUiShellDisposition::committed);
    state = harness.shell.status();
    EXPECT(harness.shell.prepare_completion(
               state.generation, state.active_revision, request.request_id,
               request.request_kind, true, 2, 4, 0,
               request.request_message_sequence, second).error ==
           opentrail::integration::PortableUiShellError::request_mismatch);
    offered = harness.shell.prepare_completion(
        state.generation, state.active_revision, request.request_id,
        request.request_kind, true, 1, 4, 0,
        request.request_message_sequence, second);
    EXPECT(offered.has_offer);
    EXPECT(harness.shell.pending_frame().screen == UiScreen::message_list);
    EXPECT(harness.shell.commit_present(state.generation, offered.revision).disposition ==
           PortableUiShellDisposition::committed);
    EXPECT(harness.shell.status().selected_message_sequence == 0);
    EXPECT(harness.display.latest_frame().status.unread_messages == 2);
}

void invalid_frames_and_plans_fail_before_offer() {
    auto frame = opentrail::ui::UiFrame{};
    frame.revision = 1;
    frame.screen = static_cast<UiScreen>(255);
    EXPECT(!opentrail::ui::valid_ui_frame(frame, capabilities()));
    EXPECT(!opentrail::ui::make_portable_ui_render_plan(
                frame, opentrail::ui::kSimulatorLogicalDisplayProfile).ok());

    frame = {};
    frame.revision = 1;
    frame.action_count = 1;
    frame.actions[0] = {UiAction::show_status, true};
    frame.actions[1] = {UiAction::cancel, true};
    EXPECT(!opentrail::ui::valid_ui_frame(frame, capabilities()));

    frame.actions[1] = {};
    auto plan = opentrail::ui::make_portable_ui_render_plan(
        frame, opentrail::ui::kSimulatorLogicalDisplayProfile);
    EXPECT(plan.ok());
    plan.plan.primitives[0].kind =
        static_cast<opentrail::ui::UiRenderPrimitiveKind>(255);
    EXPECT(opentrail::ui::validate_portable_ui_render_plan(
               frame, plan.plan, opentrail::ui::kSimulatorLogicalDisplayProfile) !=
           opentrail::ui::UiRenderPlanError::none);
}

void unavailable_text_and_exact_render_schema_fail_closed() {
    auto truncated = snapshot();
    truncated.message_count = 1;
    truncated.messages[0] = message(
        1, PortableUiMessageDirection::inbound, PortableUiMessageKind::chat,
        PortableUiMessagePriority::normal,
        PortableUiMessageDeliveryState::bridge_observed, "temporary");
    truncated.messages[0].text = {};
    truncated.messages[0].text.truncated = true;
    Harness truncated_harness{};
    truncated_harness.start(truncated);
    truncated_harness.activate_slot(1);
    truncated_harness.activate_slot(0);
    auto frame = truncated_harness.display.latest_frame();
    auto sidecar = truncated_harness.shell.presentation_sidecar();
    EXPECT(sidecar.owned_texts[0].truncated);
    EXPECT(!sidecar.owned_texts[0].unavailable);
    EXPECT(text_is(sidecar.owned_texts[0],
                   "NEW: Message exceeds display limit"));

    auto unavailable = truncated;
    unavailable.messages[0].text.truncated = false;
    unavailable.messages[0].text_unavailable = true;
    Harness unavailable_harness{};
    unavailable_harness.start(unavailable);
    unavailable_harness.activate_slot(1);
    unavailable_harness.activate_slot(0);
    unavailable_harness.activate_slot(0);
    frame = unavailable_harness.display.latest_frame();
    sidecar = unavailable_harness.shell.presentation_sidecar();
    EXPECT(!sidecar.owned_texts[0].truncated);
    EXPECT(sidecar.owned_texts[0].unavailable);
    EXPECT(text_is(sidecar.owned_texts[0], "Unsupported message text"));

    auto impossible = unavailable;
    impossible.messages[0].text.truncated = true;
    Harness rejected{};
    const auto rejected_offer = rejected.shell.prepare_activate(impossible);
    EXPECT(!rejected_offer.has_offer);
    EXPECT(rejected_offer.error ==
           opentrail::integration::PortableUiShellError::invalid_state);

    sidecar.messages.detail_acknowledge_available = true;
    EXPECT(!opentrail::ui::valid_portable_ui_presentation(frame, sidecar));
    sidecar.messages.detail_acknowledge_available = false;
    auto plan = opentrail::ui::make_portable_ui_render_plan(
        frame, sidecar, opentrail::ui::kSimulatorLogicalDisplayProfile);
    EXPECT(plan.ok());
    --plan.plan.primitive_count;
    EXPECT(opentrail::ui::validate_portable_ui_render_plan(
               frame, sidecar, plan.plan,
               opentrail::ui::kSimulatorLogicalDisplayProfile) !=
           opentrail::ui::UiRenderPlanError::none);

    plan = opentrail::ui::make_portable_ui_render_plan(
        frame, sidecar, opentrail::ui::kSimulatorLogicalDisplayProfile);
    EXPECT(plan.ok());
    const auto first = plan.plan.primitives[1];
    plan.plan.primitives[1] = plan.plan.primitives[2];
    plan.plan.primitives[2] = first;
    EXPECT(opentrail::ui::validate_portable_ui_render_plan(
               frame, sidecar, plan.plan,
               opentrail::ui::kSimulatorLogicalDisplayProfile) !=
           opentrail::ui::UiRenderPlanError::none);
}

void close_restart_and_render_failure_are_fail_closed() {
    Harness harness{};
    harness.start();
    auto state = harness.shell.status();
    EXPECT(harness.shell.close_session(state.generation, state.active_revision).disposition ==
           PortableUiShellDisposition::committed);
    EXPECT(harness.shell.close_session(state.generation, state.active_revision).disposition ==
           PortableUiShellDisposition::idle);
    EXPECT(harness.shell.prepare_input().error ==
           opentrail::integration::PortableUiShellError::invalid_state);
    auto restart = harness.shell.prepare_activate(snapshot());
    EXPECT(restart.generation == 2 && restart.revision == 2);
    EXPECT(harness.shell.commit_present(restart.generation, restart.revision).disposition ==
           PortableUiShellDisposition::committed);

    state = harness.shell.status();
    EXPECT(harness.input.enqueue_action(state.active_revision, 0));
    const auto offered = harness.shell.prepare_input();
    EXPECT(harness.display.enqueue_result(DisplayWriteError::sink_failed));
    const auto failed = harness.shell.commit_present(state.generation, offered.revision);
    EXPECT(failed.error == opentrail::integration::PortableUiShellError::display_failed);
    EXPECT(harness.display.latest_frame().revision == state.active_revision);
}

void request_id_exhaustion_and_two_instances_are_isolated() {
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface local{display, input, capabilities()};
    PortableUiShell shell{local, std::numeric_limits<std::uint32_t>::max()};
    auto offered = shell.prepare_activate(snapshot());
    EXPECT(shell.commit_present(offered.generation, offered.revision).error ==
           opentrail::integration::PortableUiShellError::none);
    auto state = shell.status();
    EXPECT(input.enqueue_action(state.active_revision, 2));
    offered = shell.prepare_input();
    EXPECT(shell.commit_present(state.generation, offered.revision).error ==
           opentrail::integration::PortableUiShellError::none);
    state = shell.status();
    EXPECT(input.enqueue_action(state.active_revision, 0));
    offered = shell.prepare_input();
    const auto request = shell.commit_present(state.generation, offered.revision);
    EXPECT(request.request_id == std::numeric_limits<std::uint32_t>::max());

    Harness peer{};
    peer.start();
    EXPECT(peer.shell.status().generation == 1);
    EXPECT(peer.display.latest_frame().revision == 1);
    EXPECT(peer.shell.status().pending_request_id == 0);
}

}  // namespace

int main() {
    two_phase_start_and_render_plan();
    quick_status_pages_and_request_correlation();
    held_confirmation_and_stale_input();
    sink_not_ready_preserves_exact_offer();
    refresh_is_generation_and_revision_bound();
    position_actions_are_typed_and_injected();
    archive_states_preserve_canonical_notice_and_stop_policy();
    recovery_mapping_and_fault_containment();
    refresh_preserves_request_lifecycle_and_fault_reconciliation();
    recovery_priority_and_acknowledgement_recurrence();
    message_inbox_read_compose_and_delivery_are_cpp_owned();
    message_epoch_and_acknowledgement_races_fail_closed();
    invalid_frames_and_plans_fail_before_offer();
    unavailable_text_and_exact_render_schema_fail_closed();
    close_restart_and_render_failure_are_fail_closed();
    request_id_exhaustion_and_two_instances_are_isolated();
    if (failures != 0) {
        std::cerr << failures << " portable UI shell assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Portable UI shell tests passed\n";
    return EXIT_SUCCESS;
}
