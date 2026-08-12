#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>

#include "fake_local_interface.hpp"
#include "opentrail/quick_status_menu_coordinator.hpp"

namespace {

using namespace opentrail::integration;
using namespace opentrail::protocol;
using namespace opentrail::ui;
using namespace opentrail::ui::test_support;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

DisplayCapabilities capabilities() {
    return {240, 240, 16, 4, true, false, true};
}

UiStatusSummary summary() {
    UiStatusSummary value{};
    value.radio = UiIndicatorState::normal;
    value.position = UiIndicatorState::warning;
    value.power = UiIndicatorState::normal;
    value.peer_count_valid = true;
    value.peer_count = 3;
    return value;
}

struct Harness {
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface local{display, input, capabilities()};
    QuickStatusMenuCoordinator menu{local};
};

void activate(Harness& harness, std::uint32_t revision = 1) {
    const auto result = harness.menu.activate(revision, summary());
    EXPECT(result.disposition == QuickStatusMenuDisposition::presented);
    EXPECT(result.frame_presented && result.revision == revision);
}

void next_page(Harness& harness, std::uint32_t revision = 1) {
    EXPECT(harness.input.enqueue_action(revision, 2));
    const auto result = harness.menu.service();
    EXPECT(result.disposition == QuickStatusMenuDisposition::page_changed);
    EXPECT(result.frame_presented && result.revision == revision + 1U);
}

void test_activation_presents_canonical_first_page() {
    Harness harness{};
    activate(harness, 5);
    const auto frame = harness.display.latest_frame();
    EXPECT(frame.screen == UiScreen::quick_status_menu);
    EXPECT(frame.attention == UiAttention::information);
    EXPECT(frame.notice == UiNotice::none);
    EXPECT(frame.status.peer_count_valid && frame.status.peer_count == 3);
    EXPECT(frame.action_count == 4);
    EXPECT(frame.actions[0].action == UiAction::select_quick_status_ok);
    EXPECT(frame.actions[1].action ==
           UiAction::select_quick_status_need_assistance);
    EXPECT(frame.actions[2].action ==
           UiAction::show_next_quick_status_page);
    EXPECT(frame.actions[3].action == UiAction::cancel);
}

void test_first_page_returns_both_typed_selections() {
    for (const auto item : {
             std::pair<std::uint8_t, QuickStatusKind>{0, QuickStatusKind::ok},
             {1, QuickStatusKind::need_assistance},
         }) {
        Harness harness{};
        activate(harness);
        EXPECT(harness.input.enqueue_action(1, item.first));
        const auto result = harness.menu.service();
        EXPECT(result.disposition ==
               QuickStatusMenuDisposition::selection_requested);
        EXPECT(result.has_selection && result.selection == item.second);
        EXPECT(result.minimum_parent_revision == 2);
        EXPECT(harness.menu.status().mode == QuickStatusMenuMode::inactive);
    }
}

void test_second_page_returns_both_typed_selections() {
    for (const auto item : {
             std::pair<std::uint8_t, QuickStatusKind>{
                 0,
                 QuickStatusKind::anyone_online,
             },
             {1, QuickStatusKind::available_to_help},
         }) {
        Harness harness{};
        activate(harness);
        next_page(harness);
        EXPECT(harness.input.enqueue_action(2, item.first));
        const auto result = harness.menu.service();
        EXPECT(result.disposition ==
               QuickStatusMenuDisposition::selection_requested);
        EXPECT(result.has_selection && result.selection == item.second);
        EXPECT(result.minimum_parent_revision == 3);
    }
}

void test_forward_and_previous_navigation_own_revisions() {
    Harness harness{};
    activate(harness, 10);
    next_page(harness, 10);
    auto frame = harness.display.latest_frame();
    EXPECT(frame.actions[0].action ==
           UiAction::select_quick_status_anyone_online);
    EXPECT(frame.actions[1].action ==
           UiAction::select_quick_status_available_to_help);
    EXPECT(frame.actions[2].action ==
           UiAction::show_previous_quick_status_page);
    EXPECT(harness.input.enqueue_action(11, 2));
    const auto previous = harness.menu.service();
    EXPECT(previous.disposition == QuickStatusMenuDisposition::page_changed);
    EXPECT(previous.revision == 12);
    EXPECT(harness.display.latest_frame().actions[0].action ==
           UiAction::select_quick_status_ok);
}

void test_cancel_from_either_page_returns_parent_floor() {
    Harness first{};
    activate(first, 4);
    EXPECT(first.input.enqueue_action(4, 3));
    const auto first_exit = first.menu.service();
    EXPECT(first_exit.disposition == QuickStatusMenuDisposition::exit_requested);
    EXPECT(first_exit.minimum_parent_revision == 5);

    Harness second{};
    activate(second, 8);
    next_page(second, 8);
    EXPECT(second.input.enqueue_action(9, 3));
    const auto second_exit = second.menu.service();
    EXPECT(second_exit.disposition ==
           QuickStatusMenuDisposition::exit_requested);
    EXPECT(second_exit.minimum_parent_revision == 10);
}

void test_stale_and_invalid_slots_are_rejected_without_selection() {
    Harness harness{};
    activate(harness, 7);
    EXPECT(harness.input.enqueue_action(6, 0));
    EXPECT(harness.input.enqueue_action(7, 7));
    auto result = harness.menu.service();
    EXPECT(result.disposition == QuickStatusMenuDisposition::input_rejected);
    EXPECT(!result.has_selection);
    result = harness.menu.service();
    EXPECT(result.disposition == QuickStatusMenuDisposition::input_rejected);
    EXPECT(harness.menu.status().mode == QuickStatusMenuMode::first_page);
}

void test_initial_display_deferral_retries_same_revision() {
    Harness harness{};
    EXPECT(harness.display.enqueue_result(DisplayWriteError::not_ready));
    const auto deferred = harness.menu.activate(1, summary());
    EXPECT(deferred.disposition ==
           QuickStatusMenuDisposition::display_deferred);
    EXPECT(harness.menu.status().mode == QuickStatusMenuMode::inactive);
    EXPECT(harness.menu.activate(1, summary()).disposition ==
           QuickStatusMenuDisposition::presented);
}

void test_transition_deferral_retries_without_second_input_poll() {
    Harness harness{};
    activate(harness);
    EXPECT(harness.display.enqueue_result(DisplayWriteError::not_ready));
    EXPECT(harness.input.enqueue_action(1, 2));
    const auto deferred = harness.menu.service();
    EXPECT(deferred.disposition ==
           QuickStatusMenuDisposition::display_deferred);
    EXPECT(harness.menu.status().mode == QuickStatusMenuMode::transitioning);
    const auto reads = harness.input.read_count();
    const auto retried = harness.menu.service();
    EXPECT(retried.disposition == QuickStatusMenuDisposition::page_changed);
    EXPECT(retried.revision == 2);
    EXPECT(harness.input.read_count() == reads);
}

void test_input_and_display_failures_latch_locally() {
    Harness input_failure{};
    activate(input_failure);
    EXPECT(input_failure.input.enqueue_failure());
    const auto failed_input = input_failure.menu.service();
    EXPECT(failed_input.error == QuickStatusMenuError::input_failed);
    EXPECT(input_failure.menu.status().mode == QuickStatusMenuMode::faulted);

    Harness display_failure{};
    EXPECT(display_failure.display.enqueue_result(
        DisplayWriteError::sink_failed));
    const auto failed_display = display_failure.menu.activate(1, summary());
    EXPECT(failed_display.error == QuickStatusMenuError::display_failed);
    EXPECT(display_failure.menu.status().mode == QuickStatusMenuMode::faulted);
}

void test_invalid_activation_summary_and_revision_exhaustion_fail_closed() {
    Harness zero{};
    EXPECT(zero.menu.activate(0, summary()).error ==
           QuickStatusMenuError::invalid_activation);

    Harness invalid_summary{};
    auto bad = summary();
    bad.peer_count_valid = false;
    const auto rejected = invalid_summary.menu.activate(1, bad);
    EXPECT(rejected.error == QuickStatusMenuError::display_failed);

    Harness exhausted{};
    const auto near_max = std::numeric_limits<std::uint32_t>::max() - 1U;
    activate(exhausted, near_max);
    EXPECT(exhausted.input.enqueue_action(near_max, 2));
    const auto result = exhausted.menu.service();
    EXPECT(result.error == QuickStatusMenuError::revision_exhausted);
    EXPECT(!result.has_selection);
    EXPECT(exhausted.menu.status().mode == QuickStatusMenuMode::faulted);
}

static_assert(std::is_trivially_copyable_v<QuickStatusMenuResult>);
static_assert(std::is_trivially_copyable_v<QuickStatusMenuStatus>);
static_assert(!std::is_copy_constructible_v<QuickStatusMenuCoordinator>);
static_assert(!std::is_move_constructible_v<QuickStatusMenuCoordinator>);
static_assert(sizeof(QuickStatusMenuStatus) <= 64);

}  // namespace

int main() {
    test_activation_presents_canonical_first_page();
    test_first_page_returns_both_typed_selections();
    test_second_page_returns_both_typed_selections();
    test_forward_and_previous_navigation_own_revisions();
    test_cancel_from_either_page_returns_parent_floor();
    test_stale_and_invalid_slots_are_rejected_without_selection();
    test_initial_display_deferral_retries_same_revision();
    test_transition_deferral_retries_without_second_input_poll();
    test_input_and_display_failures_latch_locally();
    test_invalid_activation_summary_and_revision_exhaustion_fail_closed();

    if (failures != 0) {
        std::cerr << failures
                  << " quick-status menu assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 quick-status menu scenario groups\n";
    return EXIT_SUCCESS;
}
