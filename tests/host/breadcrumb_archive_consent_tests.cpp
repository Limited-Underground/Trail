#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

#include "fake_breadcrumb_archive_remote.hpp"
#include "fake_local_interface.hpp"
#include "fake_monotonic_counter_source.hpp"
#include "opentrail/breadcrumb_archive_consent.hpp"

namespace {

using namespace opentrail::integration;
using namespace opentrail::location;
using namespace opentrail::location::test_support;
using namespace opentrail::time;
using namespace opentrail::time::test_support;
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

class FakeConsentLock final : public BreadcrumbArchiveSnapshotLock {
public:
    BreadcrumbArchiveSnapshotLockState next_acquire{
        BreadcrumbArchiveSnapshotLockState::acquired};
    BreadcrumbArchiveSnapshotUnlockState next_release{
        BreadcrumbArchiveSnapshotUnlockState::released};
    std::uint32_t acquire_calls{0};
    std::uint32_t release_calls{0};

    BreadcrumbArchiveSnapshotLockState acquire() override {
        ++acquire_calls;
        const auto result = next_acquire;
        next_acquire = BreadcrumbArchiveSnapshotLockState::acquired;
        return result;
    }

    BreadcrumbArchiveSnapshotUnlockState release() override {
        ++release_calls;
        const auto result = next_release;
        next_release = BreadcrumbArchiveSnapshotUnlockState::released;
        return result;
    }
};

DisplayCapabilities hold_capabilities() {
    return {128, 64, 1, 2, false, true, true};
}

DisplayCapabilities no_hold_capabilities() {
    return {128, 64, 1, 2, false, true, false};
}

struct Fixture {
    FakeMonotonicCounterSource clock_source{};
    CheckedMonotonicClock clock{clock_source};
    FakeBreadcrumbArchiveRemote remote{};
    FakeConsentLock lock{};
    SerializedBreadcrumbArchiveRuntimeOwner runtime{
        clock, remote, {1'000, 100}, {10, 80}, lock};
    BreadcrumbArchiveConsentController consent;

    explicit Fixture(std::uint64_t initial_session_id = 1)
        : consent(runtime, clock, initial_session_id) {}
};

ResolvedAction resolved(UiAction action, std::uint32_t revision = 1) {
    return {ActionResolutionError::none, action, revision};
}

void test_start_confirmation_is_canonical_and_hold_only() {
    const auto presentation = make_breadcrumb_archive_consent_presentation(
        BreadcrumbArchiveConsentMode::start, 7);
    EXPECT(presentation.presentable());
    EXPECT(presentation.frame.notice ==
           UiNotice::archive_start_confirmation);
    EXPECT(presentation.frame.actions[0].action ==
           UiAction::confirm_archive_start);

    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface local{display, input, hold_capabilities()};
    EXPECT(local.present(presentation.frame).ok());
    EXPECT(input.enqueue_action(7, 0, InputGesture::activate));
    EXPECT(input.enqueue_action(7, 0, InputGesture::hold));
    EXPECT(local.poll_action().error == ActionResolutionError::hold_required);
    EXPECT(local.poll_action().action == UiAction::confirm_archive_start);
}

void test_stop_confirmation_is_canonical_and_immediate() {
    const auto presentation = make_breadcrumb_archive_consent_presentation(
        BreadcrumbArchiveConsentMode::stop, 8);
    EXPECT(presentation.presentable());
    EXPECT(presentation.frame.notice ==
           UiNotice::archive_stop_confirmation);
    EXPECT(presentation.frame.actions[0].action == UiAction::stop_archive);

    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface local{display, input, no_hold_capabilities()};
    EXPECT(local.present(presentation.frame).ok());
    EXPECT(input.enqueue_action(8, 0));
    EXPECT(local.poll_action().action == UiAction::stop_archive);
}

void test_invalid_consent_frames_never_reach_display() {
    EXPECT(!make_breadcrumb_archive_consent_presentation(
                BreadcrumbArchiveConsentMode::start, 0).presentable());
    EXPECT(!make_breadcrumb_archive_consent_presentation(
                static_cast<BreadcrumbArchiveConsentMode>(99), 1)
                .presentable());

    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface no_hold{
        display, input, no_hold_capabilities()};
    const auto start = make_breadcrumb_archive_consent_presentation(
        BreadcrumbArchiveConsentMode::start, 1);
    EXPECT(no_hold.present(start.frame).error == PresentError::invalid_frame);
    EXPECT(display.present_count() == 0);

    FakeDisplaySink ordinary_display{};
    FakeLocalInputSource ordinary_input{};
    CheckedLocalInterface ordinary{
        ordinary_display, ordinary_input, hold_capabilities()};
    UiFrame wrong_screen{};
    wrong_screen.revision = 1;
    wrong_screen.screen = UiScreen::home;
    wrong_screen.action_count = 1;
    wrong_screen.actions[0] = {UiAction::confirm_archive_start, true};
    EXPECT(ordinary.present(wrong_screen).error == PresentError::invalid_frame);
    wrong_screen.actions[0] = {UiAction::stop_archive, true};
    EXPECT(ordinary.present(wrong_screen).error == PresentError::invalid_frame);
    EXPECT(ordinary_display.present_count() == 0);
}

void test_stale_or_unsupported_action_cannot_start_archive() {
    Fixture fixture{};
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface local{display, input, hold_capabilities()};
    const auto frame = make_breadcrumb_archive_consent_presentation(
        BreadcrumbArchiveConsentMode::start, 4);
    EXPECT(local.present(frame.frame).ok());
    EXPECT(input.enqueue_action(3, 0, InputGesture::hold));
    const auto stale = fixture.consent.apply(local.poll_action());
    EXPECT(stale.disposition == BreadcrumbArchiveConsentDisposition::rejected);
    EXPECT(fixture.clock_source.read_count() == 0);
    EXPECT(fixture.lock.acquire_calls == 0);

    const auto unsupported = fixture.consent.apply(
        resolved(UiAction::start_position_sharing));
    EXPECT(unsupported.error ==
           BreadcrumbArchiveConsentError::unsupported_action);
    EXPECT(fixture.clock_source.read_count() == 0);
}

void test_confirmed_start_uses_checked_time_and_consumes_sequence() {
    Fixture fixture{20};
    EXPECT(fixture.clock_source.enqueue_time(100));
    const auto started = fixture.consent.apply(
        resolved(UiAction::confirm_archive_start, 5));
    EXPECT(started.disposition == BreadcrumbArchiveConsentDisposition::started);
    EXPECT(started.resolved_local_action);
    EXPECT(started.session_id == 20);
    EXPECT(started.session_id_consumed);
    EXPECT(fixture.consent.status().next_session_id == 21);
    EXPECT(fixture.lock.acquire_calls == 1);

    BreadcrumbArchiveRuntimeSnapshot snapshot{};
    EXPECT(fixture.runtime.snapshot(snapshot) ==
           BreadcrumbArchiveSnapshotState::ready);
    EXPECT(snapshot.session.active);
}

void test_cancel_reads_no_clock_and_touches_no_runtime() {
    Fixture fixture{};
    const auto cancelled = fixture.consent.apply(resolved(UiAction::cancel));
    EXPECT(cancelled.disposition ==
           BreadcrumbArchiveConsentDisposition::cancelled);
    EXPECT(fixture.clock_source.read_count() == 0);
    EXPECT(fixture.lock.acquire_calls == 0);
    EXPECT(fixture.consent.status().cancelled == 1);
}

void test_stop_is_local_immediate_and_clock_independent() {
    Fixture fixture{};
    EXPECT(fixture.clock_source.enqueue_time(10));
    EXPECT(fixture.consent.apply(
        resolved(UiAction::confirm_archive_start)).disposition ==
           BreadcrumbArchiveConsentDisposition::started);
    const auto reads = fixture.clock_source.read_count();
    const auto stopped = fixture.consent.apply(
        resolved(UiAction::stop_archive, 2));
    EXPECT(stopped.disposition == BreadcrumbArchiveConsentDisposition::stopped);
    EXPECT(fixture.clock_source.read_count() == reads);

    BreadcrumbArchiveRuntimeSnapshot snapshot{};
    EXPECT(fixture.runtime.snapshot(snapshot) ==
           BreadcrumbArchiveSnapshotState::ready);
    EXPECT(!snapshot.session.active);
}

void test_clock_not_ready_or_failure_never_reaches_runtime() {
    Fixture deferred{};
    EXPECT(deferred.clock_source.enqueue_not_ready());
    const auto not_ready = deferred.consent.apply(
        resolved(UiAction::confirm_archive_start));
    EXPECT(not_ready.disposition ==
           BreadcrumbArchiveConsentDisposition::deferred);
    EXPECT(not_ready.error == BreadcrumbArchiveConsentError::clock_not_ready);
    EXPECT(deferred.lock.acquire_calls == 0);

    Fixture failed{};
    EXPECT(failed.clock_source.enqueue_failure());
    const auto clock_failed = failed.consent.apply(
        resolved(UiAction::confirm_archive_start));
    EXPECT(clock_failed.error == BreadcrumbArchiveConsentError::clock_failed);
    EXPECT(failed.lock.acquire_calls == 0);
}

void test_runtime_contention_preserves_candidate_for_explicit_retry() {
    Fixture fixture{30};
    fixture.lock.next_acquire =
        BreadcrumbArchiveSnapshotLockState::not_ready;
    EXPECT(fixture.clock_source.enqueue_time(50));
    const auto deferred = fixture.consent.apply(
        resolved(UiAction::confirm_archive_start));
    EXPECT(deferred.disposition ==
           BreadcrumbArchiveConsentDisposition::deferred);
    EXPECT(!deferred.session_id_consumed);
    EXPECT(fixture.consent.status().next_session_id == 30);

    EXPECT(fixture.clock_source.enqueue_time(51));
    const auto retried = fixture.consent.apply(
        resolved(UiAction::confirm_archive_start, 2));
    EXPECT(retried.disposition == BreadcrumbArchiveConsentDisposition::started);
    EXPECT(retried.session_id == 30);
    EXPECT(fixture.consent.status().next_session_id == 31);
}

void test_uncertain_or_exhausted_session_id_never_reuses_value() {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    Fixture uncertain{maximum};
    uncertain.lock.next_release = BreadcrumbArchiveSnapshotUnlockState::failed;
    EXPECT(uncertain.clock_source.enqueue_time(60));
    const auto failed = uncertain.consent.apply(
        resolved(UiAction::confirm_archive_start));
    EXPECT(failed.error == BreadcrumbArchiveConsentError::runtime_failed);
    EXPECT(failed.session_id_consumed);
    EXPECT(uncertain.consent.status().session_id_exhausted);

    const auto reads = uncertain.clock_source.read_count();
    const auto exhausted = uncertain.consent.apply(
        resolved(UiAction::confirm_archive_start, 2));
    EXPECT(exhausted.error ==
           BreadcrumbArchiveConsentError::session_id_exhausted);
    EXPECT(uncertain.clock_source.read_count() == reads);

    Fixture invalid{0};
    EXPECT(invalid.consent.apply(resolved(UiAction::cancel)).error ==
           BreadcrumbArchiveConsentError::invalid_initial_session_id);
}

static_assert(std::is_trivially_copyable_v<
              BreadcrumbArchiveConsentPresentation>);
static_assert(std::is_trivially_copyable_v<BreadcrumbArchiveConsentResult>);
static_assert(std::is_trivially_copyable_v<BreadcrumbArchiveConsentStatus>);
static_assert(sizeof(BreadcrumbArchiveConsentResult) <= 128);
static_assert(sizeof(BreadcrumbArchiveConsentStatus) <= 48);

}  // namespace

int main() {
    test_start_confirmation_is_canonical_and_hold_only();
    test_stop_confirmation_is_canonical_and_immediate();
    test_invalid_consent_frames_never_reach_display();
    test_stale_or_unsupported_action_cannot_start_archive();
    test_confirmed_start_uses_checked_time_and_consumes_sequence();
    test_cancel_reads_no_clock_and_touches_no_runtime();
    test_stop_is_local_immediate_and_clock_independent();
    test_clock_not_ready_or_failure_never_reaches_runtime();
    test_runtime_contention_preserves_candidate_for_explicit_retry();
    test_uncertain_or_exhausted_session_id_never_reuses_value();

    if (failures != 0) {
        std::cerr << failures
                  << " breadcrumb archive consent assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 breadcrumb archive consent scenario groups\n";
    return EXIT_SUCCESS;
}
