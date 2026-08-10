#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_local_interface.hpp"
#include "opentrail/local_interface.hpp"

namespace {

using opentrail::ui::ActionResolutionError;
using opentrail::ui::CheckedLocalInterface;
using opentrail::ui::DisplayCapabilities;
using opentrail::ui::DisplayWriteError;
using opentrail::ui::InputGesture;
using opentrail::ui::InputReadError;
using opentrail::ui::LocalInputEvent;
using opentrail::ui::PresentError;
using opentrail::ui::UiAction;
using opentrail::ui::UiAttention;
using opentrail::ui::UiFrame;
using opentrail::ui::UiIndicatorState;
using opentrail::ui::UiNotice;
using opentrail::ui::UiScreen;
using opentrail::ui::test_support::FakeDisplaySink;
using opentrail::ui::test_support::FakeLocalInputSource;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

DisplayCapabilities touch_capabilities() {
    return {240, 240, 16, 4, true, false, true};
}

UiFrame home_frame(std::uint32_t revision) {
    UiFrame frame{};
    frame.revision = revision;
    frame.screen = UiScreen::home;
    frame.attention = UiAttention::none;
    frame.notice = UiNotice::none;
    frame.status.radio = UiIndicatorState::normal;
    frame.status.position = UiIndicatorState::normal;
    frame.status.power = UiIndicatorState::normal;
    frame.status.peer_count_valid = true;
    frame.status.peer_count = 4;
    frame.status.unread_messages = 1;
    frame.action_count = 3;
    frame.actions[0] = {UiAction::show_status, true};
    frame.actions[1] = {UiAction::open_quick_status_menu, true};
    frame.actions[2] = {UiAction::open_critical_confirmation, true};
    return frame;
}

UiFrame critical_frame(std::uint32_t revision) {
    auto frame = home_frame(revision);
    frame.screen = UiScreen::critical_confirmation;
    frame.attention = UiAttention::critical;
    frame.notice = UiNotice::critical_alert_pending;
    frame.action_count = 2;
    frame.actions[0] = {UiAction::confirm_critical_alert, true};
    frame.actions[1] = {UiAction::cancel, true};
    frame.actions[2] = {};
    return frame;
}

void test_invalid_capabilities_never_touch_display() {
    FakeDisplaySink display;
    FakeLocalInputSource input;
    const auto frame = home_frame(1);

    auto capabilities = touch_capabilities();
    capabilities.width_px = 0;
    CheckedLocalInterface zero_width(display, input, capabilities);
    EXPECT(zero_width.present(frame).error == PresentError::invalid_capabilities);

    capabilities = touch_capabilities();
    capabilities.color_depth_bits = 33;
    CheckedLocalInterface bad_depth(display, input, capabilities);
    EXPECT(bad_depth.present(frame).error == PresentError::invalid_capabilities);

    capabilities = touch_capabilities();
    capabilities.max_action_slots = 0;
    CheckedLocalInterface no_slots(display, input, capabilities);
    EXPECT(no_slots.present(frame).error == PresentError::invalid_capabilities);

    capabilities = touch_capabilities();
    capabilities.has_touch = false;
    capabilities.has_buttons = false;
    CheckedLocalInterface no_input(display, input, capabilities);
    EXPECT(no_input.present(frame).error == PresentError::invalid_capabilities);
    EXPECT(display.present_count() == 0);
}

void test_valid_semantic_frame_is_presented_atomically() {
    FakeDisplaySink display;
    FakeLocalInputSource input;
    CheckedLocalInterface interface(display, input, touch_capabilities());
    const auto frame = home_frame(7);

    const auto result = interface.present(frame);
    EXPECT(result.ok());
    EXPECT(result.revision == 7);
    EXPECT(display.has_presented_frame());
    EXPECT(display.latest_frame().revision == 7);
    EXPECT(display.latest_frame().status.peer_count == 4);
    EXPECT(display.latest_frame().actions[2].action ==
           UiAction::open_critical_confirmation);
    const auto status = interface.status();
    EXPECT(status.capabilities_valid);
    EXPECT(status.has_active_frame);
    EXPECT(status.active_revision == 7);
    EXPECT(status.presented_frames == 1);
}

void test_display_not_ready_can_retry_same_revision() {
    FakeDisplaySink display;
    FakeLocalInputSource input;
    EXPECT(display.enqueue_result(DisplayWriteError::not_ready));
    EXPECT(display.enqueue_result(DisplayWriteError::none));
    EXPECT(display.enqueue_result(DisplayWriteError::sink_failed));
    EXPECT(display.enqueue_result(static_cast<DisplayWriteError>(255)));
    CheckedLocalInterface interface(display, input, touch_capabilities());

    EXPECT(interface.present(home_frame(1)).error == PresentError::sink_not_ready);
    EXPECT(!interface.status().has_active_frame);
    EXPECT(interface.present(home_frame(1)).ok());
    EXPECT(interface.present(home_frame(2)).error == PresentError::sink_failed);
    EXPECT(interface.present(home_frame(3)).error == PresentError::sink_failed);
    EXPECT(interface.status().active_revision == 1);
    EXPECT(display.latest_frame().revision == 1);
    EXPECT(display.success_count() == 1);
}

void test_frame_validation_and_revision_are_fail_closed() {
    FakeDisplaySink display;
    FakeLocalInputSource input;
    CheckedLocalInterface interface(display, input, touch_capabilities());
    EXPECT(interface.present(home_frame(10)).ok());
    EXPECT(interface.present(home_frame(10)).error ==
           PresentError::revision_not_increasing);
    EXPECT(interface.present(home_frame(9)).error ==
           PresentError::revision_not_increasing);

    auto unknown_screen = home_frame(11);
    unknown_screen.screen = static_cast<UiScreen>(255);
    EXPECT(interface.present(unknown_screen).error == PresentError::invalid_frame);

    auto unknown_action = home_frame(11);
    unknown_action.actions[0].action = static_cast<UiAction>(255);
    EXPECT(interface.present(unknown_action).error == PresentError::invalid_frame);

    auto too_many = home_frame(11);
    too_many.action_count = 5;
    EXPECT(interface.present(too_many).error == PresentError::invalid_frame);

    auto hidden_binding = home_frame(11);
    hidden_binding.actions[3] = {UiAction::cancel, true};
    EXPECT(interface.present(hidden_binding).error == PresentError::invalid_frame);

    auto duplicate = home_frame(11);
    duplicate.actions[1] = duplicate.actions[0];
    EXPECT(interface.present(duplicate).error == PresentError::invalid_frame);
    EXPECT(display.present_count() == 1);
    EXPECT(interface.status().active_revision == 10);
}

void test_capability_limits_and_hold_support_are_enforced() {
    FakeDisplaySink display;
    FakeLocalInputSource input;
    auto capabilities = touch_capabilities();
    capabilities.max_action_slots = 2;
    CheckedLocalInterface two_slots(display, input, capabilities);
    EXPECT(two_slots.present(home_frame(1)).error == PresentError::invalid_frame);

    capabilities = touch_capabilities();
    capabilities.supports_hold = false;
    CheckedLocalInterface no_hold(display, input, capabilities);
    EXPECT(no_hold.present(critical_frame(1)).error == PresentError::invalid_frame);
    EXPECT(display.present_count() == 0);
}

void test_critical_confirmation_has_canonical_shape() {
    FakeDisplaySink display;
    FakeLocalInputSource input;
    CheckedLocalInterface interface(display, input, touch_capabilities());
    EXPECT(interface.present(critical_frame(1)).ok());

    auto wrong_attention = critical_frame(2);
    wrong_attention.attention = UiAttention::warning;
    EXPECT(interface.present(wrong_attention).error == PresentError::invalid_frame);

    auto wrong_order = critical_frame(2);
    wrong_order.actions[0] = {UiAction::cancel, true};
    wrong_order.actions[1] = {UiAction::confirm_critical_alert, true};
    EXPECT(interface.present(wrong_order).error == PresentError::invalid_frame);

    auto confirm_on_home = home_frame(2);
    confirm_on_home.actions[0] = {UiAction::confirm_critical_alert, true};
    EXPECT(interface.present(confirm_on_home).error == PresentError::invalid_frame);
}

void test_normal_action_resolves_against_exact_frame() {
    FakeDisplaySink display;
    FakeLocalInputSource input;
    CheckedLocalInterface interface(display, input, touch_capabilities());
    EXPECT(interface.present(home_frame(7)).ok());
    EXPECT(input.enqueue_action(7, 1));

    const auto action = interface.poll_action();
    EXPECT(action.ok());
    EXPECT(action.action == UiAction::open_quick_status_menu);
    EXPECT(action.frame_revision == 7);
    EXPECT(interface.status().resolved_actions == 1);
}

void test_stale_invalid_and_disabled_slots_do_not_resolve() {
    FakeDisplaySink display;
    FakeLocalInputSource input;
    auto frame = home_frame(3);
    frame.actions[1].enabled = false;
    CheckedLocalInterface interface(display, input, touch_capabilities());
    EXPECT(interface.present(frame).ok());
    EXPECT(input.enqueue_action(2, 0));
    EXPECT(input.enqueue_action(3, 3));
    EXPECT(input.enqueue_action(3, 1));

    EXPECT(interface.poll_action().error == ActionResolutionError::stale_frame);
    EXPECT(interface.poll_action().error == ActionResolutionError::invalid_slot);
    EXPECT(interface.poll_action().error == ActionResolutionError::disabled_action);
    EXPECT(interface.status().resolved_actions == 0);
    EXPECT(interface.status().rejected_inputs == 3);
}

void test_critical_confirmation_requires_hold() {
    FakeDisplaySink display;
    FakeLocalInputSource input;
    CheckedLocalInterface interface(display, input, touch_capabilities());
    EXPECT(interface.present(critical_frame(4)).ok());
    EXPECT(input.enqueue_action(4, 0, InputGesture::activate));
    EXPECT(input.enqueue_action(4, 0, InputGesture::hold));
    EXPECT(input.enqueue_action(4, 1, InputGesture::hold));

    EXPECT(interface.poll_action().error == ActionResolutionError::hold_required);
    const auto confirmed = interface.poll_action();
    EXPECT(confirmed.ok());
    EXPECT(confirmed.action == UiAction::confirm_critical_alert);
    EXPECT(interface.poll_action().error == ActionResolutionError::invalid_gesture);
}

void test_input_failures_and_unknown_gesture_are_explicit() {
    FakeDisplaySink display;
    FakeLocalInputSource input;
    CheckedLocalInterface interface(display, input, touch_capabilities());
    EXPECT(input.enqueue_action(1, 0));
    EXPECT(input.enqueue_not_ready());
    EXPECT(input.enqueue_failure());
    LocalInputEvent unknown_error{};
    unknown_error.error = static_cast<InputReadError>(255);
    EXPECT(input.enqueue(unknown_error));

    EXPECT(interface.poll_action().error == ActionResolutionError::no_active_frame);
    EXPECT(interface.poll_action().error == ActionResolutionError::input_not_ready);
    EXPECT(interface.poll_action().error == ActionResolutionError::input_failed);
    EXPECT(interface.poll_action().error == ActionResolutionError::input_failed);

    EXPECT(interface.present(home_frame(1)).ok());
    LocalInputEvent unknown_gesture{};
    unknown_gesture.error = InputReadError::none;
    unknown_gesture.frame_revision = 1;
    unknown_gesture.action_slot = 0;
    unknown_gesture.gesture = static_cast<InputGesture>(255);
    EXPECT(input.enqueue(unknown_gesture));
    EXPECT(interface.poll_action().error == ActionResolutionError::invalid_gesture);
}

void test_system_fault_screen_cannot_offer_send_actions() {
    FakeDisplaySink display;
    FakeLocalInputSource input;
    CheckedLocalInterface interface(display, input, touch_capabilities());
    auto frame = home_frame(1);
    frame.screen = UiScreen::system_fault;
    frame.attention = UiAttention::critical;
    frame.notice = UiNotice::message_failed;
    EXPECT(interface.present(frame).error == PresentError::invalid_frame);

    frame.action_count = 1;
    frame.actions[0] = {UiAction::acknowledge_notice, true};
    frame.actions[1] = {};
    frame.actions[2] = {};
    EXPECT(interface.present(frame).ok());
}

void test_fakes_are_bounded_and_ordered() {
    FakeLocalInputSource input;
    for (std::size_t index = 0; index < input.kCapacity; ++index) {
        EXPECT(input.enqueue_action(static_cast<std::uint32_t>(index + 1),
                                    static_cast<std::uint8_t>(index % 4)));
    }
    EXPECT(!input.enqueue_action(99, 0));
    for (std::size_t index = 0; index < input.kCapacity; ++index) {
        const auto event = input.read();
        EXPECT(event.error == InputReadError::none);
        EXPECT(event.frame_revision == index + 1);
    }
    EXPECT(input.read().error == InputReadError::not_ready);
    EXPECT(input.read_count() == input.kCapacity + 1);

    FakeDisplaySink display;
    for (std::size_t index = 0; index < display.kCapacity; ++index) {
        EXPECT(display.enqueue_result(DisplayWriteError::not_ready));
    }
    EXPECT(!display.enqueue_result(DisplayWriteError::sink_failed));
    for (std::size_t index = 0; index < display.kCapacity; ++index) {
        EXPECT(display.present(home_frame(static_cast<std::uint32_t>(index + 1))) ==
               DisplayWriteError::not_ready);
    }
    EXPECT(display.queued_result_count() == 0);
    EXPECT(display.present(home_frame(99)) == DisplayWriteError::none);
    EXPECT(display.latest_frame().revision == 99);
}

}  // namespace

int main() {
    test_invalid_capabilities_never_touch_display();
    test_valid_semantic_frame_is_presented_atomically();
    test_display_not_ready_can_retry_same_revision();
    test_frame_validation_and_revision_are_fail_closed();
    test_capability_limits_and_hold_support_are_enforced();
    test_critical_confirmation_has_canonical_shape();
    test_normal_action_resolves_against_exact_frame();
    test_stale_invalid_and_disabled_slots_do_not_resolve();
    test_critical_confirmation_requires_hold();
    test_input_failures_and_unknown_gesture_are_explicit();
    test_system_fault_screen_cannot_offer_send_actions();
    test_fakes_are_bounded_and_ordered();

    if (failures != 0) {
        std::cerr << failures << " local-interface assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 12 local display/input boundary scenario groups\n";
    return EXIT_SUCCESS;
}
