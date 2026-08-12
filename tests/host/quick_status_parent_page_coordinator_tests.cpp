#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "fake_local_interface.hpp"
#include "opentrail/quick_status_parent_page_coordinator.hpp"

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
    value.unread_messages = 2;
    return value;
}

struct Harness {
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface local{display, input, capabilities()};
    QuickStatusParentPageCoordinator page{local};
};

void activate(Harness& harness, std::uint32_t revision = 1) {
    const auto result = harness.page.activate(revision, summary());
    EXPECT(result.disposition ==
           QuickStatusParentPageDisposition::presented);
    EXPECT(result.frame_presented && result.revision == revision);
}

void open_menu(Harness& harness, std::uint32_t parent_revision = 1) {
    EXPECT(harness.input.enqueue_action(parent_revision, 0));
    const auto opened = harness.page.service();
    EXPECT(opened.disposition == QuickStatusParentPageDisposition::opened);
    EXPECT(opened.menu.disposition == QuickStatusMenuDisposition::presented);
    EXPECT(harness.display.latest_frame().screen ==
           UiScreen::quick_status_menu);
}

void test_activation_presents_only_quick_status_and_back() {
    Harness harness{};
    activate(harness, 5);
    const auto frame = harness.display.latest_frame();
    EXPECT(frame.screen == UiScreen::status);
    EXPECT(frame.attention == UiAttention::information);
    EXPECT(frame.status.peer_count == 3);
    EXPECT(frame.status.unread_messages == 2);
    EXPECT(frame.action_count == 2);
    EXPECT(frame.actions[0].action == UiAction::open_quick_status_menu);
    EXPECT(frame.actions[1].action == UiAction::cancel);
}

void test_parent_idle_polls_once_without_selection() {
    Harness harness{};
    activate(harness);
    const auto result = harness.page.service();
    EXPECT(result.disposition == QuickStatusParentPageDisposition::idle);
    EXPECT(result.input_polled && !result.has_selection);
    EXPECT(harness.input.read_count() == 1);
}

void test_open_enters_exact_newer_first_menu_page() {
    Harness harness{};
    activate(harness, 10);
    open_menu(harness, 10);
    const auto frame = harness.display.latest_frame();
    EXPECT(frame.revision == 11);
    EXPECT(frame.actions[0].action == UiAction::select_quick_status_ok);
    EXPECT(harness.page.status().mode == QuickStatusParentPageMode::menu);
}

void test_nested_cancel_restores_parent_without_selection() {
    Harness harness{};
    activate(harness);
    open_menu(harness);
    EXPECT(harness.input.enqueue_action(2, 3));
    const auto restored = harness.page.service();
    EXPECT(restored.disposition == QuickStatusParentPageDisposition::restored);
    EXPECT(!restored.has_selection);
    EXPECT(restored.frame_presented && restored.revision == 3);
    EXPECT(harness.display.latest_frame().screen == UiScreen::status);
}

void test_first_page_selection_surfaces_only_after_parent_restore() {
    Harness harness{};
    activate(harness);
    open_menu(harness);
    EXPECT(harness.input.enqueue_action(2, 1));
    const auto selected = harness.page.service();
    EXPECT(selected.disposition ==
           QuickStatusParentPageDisposition::selection_requested);
    EXPECT(selected.has_selection);
    EXPECT(selected.selection == QuickStatusKind::need_assistance);
    EXPECT(selected.frame_presented && selected.revision == 3);
    EXPECT(harness.display.latest_frame().screen == UiScreen::status);
}

void test_second_page_selection_returns_exact_typed_request() {
    Harness harness{};
    activate(harness, 20);
    open_menu(harness, 20);
    EXPECT(harness.input.enqueue_action(21, 2));
    auto forwarded = harness.page.service();
    EXPECT(forwarded.disposition ==
           QuickStatusParentPageDisposition::forwarded);
    EXPECT(harness.display.latest_frame().revision == 22);
    EXPECT(harness.input.enqueue_action(22, 0));
    const auto selected = harness.page.service();
    EXPECT(selected.disposition ==
           QuickStatusParentPageDisposition::selection_requested);
    EXPECT(selected.selection == QuickStatusKind::anyone_online);
    EXPECT(selected.revision == 23);
}

void test_deferred_selection_restore_does_not_surface_early() {
    Harness harness{};
    activate(harness);
    open_menu(harness);
    EXPECT(harness.display.enqueue_result(DisplayWriteError::not_ready));
    EXPECT(harness.input.enqueue_action(2, 0));
    const auto deferred = harness.page.service();
    EXPECT(deferred.disposition ==
           QuickStatusParentPageDisposition::display_deferred);
    EXPECT(!deferred.has_selection);
    EXPECT(harness.page.status().mode ==
           QuickStatusParentPageMode::restoring_parent);
    const auto reads = harness.input.read_count();
    const auto selected = harness.page.service();
    EXPECT(selected.disposition ==
           QuickStatusParentPageDisposition::selection_requested);
    EXPECT(selected.selection == QuickStatusKind::ok);
    EXPECT(harness.input.read_count() == reads);
}

void test_menu_open_display_deferral_keeps_parent_retryable() {
    Harness harness{};
    activate(harness);
    EXPECT(harness.display.enqueue_result(DisplayWriteError::not_ready));
    EXPECT(harness.input.enqueue_action(1, 0));
    const auto deferred = harness.page.service();
    EXPECT(deferred.disposition ==
           QuickStatusParentPageDisposition::display_deferred);
    EXPECT(harness.page.status().mode == QuickStatusParentPageMode::parent);
    EXPECT(harness.input.enqueue_action(1, 0));
    EXPECT(harness.page.service().disposition ==
           QuickStatusParentPageDisposition::opened);
}

void test_parent_back_exits_and_allows_newer_reactivation() {
    Harness harness{};
    activate(harness, 7);
    EXPECT(harness.input.enqueue_action(7, 1));
    const auto exited = harness.page.service();
    EXPECT(exited.disposition ==
           QuickStatusParentPageDisposition::exit_requested);
    EXPECT(!exited.has_selection && exited.revision == 7);
    EXPECT(harness.page.status().mode == QuickStatusParentPageMode::inactive);
    EXPECT(harness.page.activate(8, summary()).disposition ==
           QuickStatusParentPageDisposition::presented);
}

void test_invalid_stale_and_failed_input_never_select() {
    Harness invalid{};
    EXPECT(invalid.page.activate(0, summary()).error ==
           QuickStatusParentPageError::invalid_activation);

    Harness stale{};
    activate(stale, 4);
    EXPECT(stale.input.enqueue_action(3, 0));
    const auto rejected = stale.page.service();
    EXPECT(rejected.disposition ==
           QuickStatusParentPageDisposition::input_rejected);
    EXPECT(!rejected.has_selection);

    Harness failed{};
    activate(failed);
    EXPECT(failed.input.enqueue_failure());
    const auto failure = failed.page.service();
    EXPECT(failure.error == QuickStatusParentPageError::input_failed);
    EXPECT(!failure.has_selection);
    EXPECT(failed.page.status().mode == QuickStatusParentPageMode::faulted);
}

static_assert(std::is_trivially_copyable_v<QuickStatusParentPageResult>);
static_assert(std::is_trivially_copyable_v<QuickStatusParentPageStatus>);
static_assert(!std::is_copy_constructible_v<
              QuickStatusParentPageCoordinator>);
static_assert(!std::is_move_constructible_v<
              QuickStatusParentPageCoordinator>);
static_assert(sizeof(QuickStatusParentPageStatus) <= 64);

}  // namespace

int main() {
    test_activation_presents_only_quick_status_and_back();
    test_parent_idle_polls_once_without_selection();
    test_open_enters_exact_newer_first_menu_page();
    test_nested_cancel_restores_parent_without_selection();
    test_first_page_selection_surfaces_only_after_parent_restore();
    test_second_page_selection_returns_exact_typed_request();
    test_deferred_selection_restore_does_not_surface_early();
    test_menu_open_display_deferral_keeps_parent_retryable();
    test_parent_back_exits_and_allows_newer_reactivation();
    test_invalid_stale_and_failed_input_never_select();

    if (failures != 0) {
        std::cerr << failures
                  << " quick-status parent-page assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 quick-status parent-page scenario groups\n";
    return EXIT_SUCCESS;
}
