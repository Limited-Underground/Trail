#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

#include "fake_local_interface.hpp"
#include "opentrail/breadcrumb_archive_ui_coordinator.hpp"

namespace {

using namespace opentrail::integration;
using namespace opentrail::location;
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
    return {128, 64, 1, 2, false, true, false};
}

BreadcrumbArchiveRuntimeSnapshot stopped_snapshot() {
    BreadcrumbArchiveRuntimeSnapshot snapshot{};
    snapshot.session.scheduler.policy_valid = true;
    snapshot.retry.current_retry_ms = 10;
    return snapshot;
}

BreadcrumbArchiveRuntimeSnapshot active_snapshot(
    std::size_t queued = 0) {
    auto snapshot = stopped_snapshot();
    snapshot.session.active = true;
    snapshot.session.next_sequence = 1;
    snapshot.session.sessions_started = 1;
    snapshot.session.scheduler.active = true;
    snapshot.session.scheduler.starts = 1;
    snapshot.outbox.queued = queued;
    snapshot.outbox.has_prior_record = queued != 0;
    snapshot.outbox.last_session_id = queued != 0 ? 10 : 0;
    snapshot.outbox.last_sequence = static_cast<std::uint32_t>(queued);
    snapshot.outbox.accepted = static_cast<std::uint32_t>(queued);
    return snapshot;
}

class FakeSnapshotSource final : public BreadcrumbArchiveSnapshotSource {
public:
    static constexpr std::size_t kCapacity = 16;

    bool enqueue(BreadcrumbArchiveSnapshotState state,
                 const BreadcrumbArchiveRuntimeSnapshot& snapshot = {}) {
        if (size_ == entries_.size()) {
            return false;
        }
        const auto tail = (head_ + size_) % entries_.size();
        entries_[tail] = {state, snapshot};
        ++size_;
        return true;
    }

    BreadcrumbArchiveSnapshotState snapshot(
        BreadcrumbArchiveRuntimeSnapshot& output) override {
        ++reads_;
        if (size_ == 0) {
            return BreadcrumbArchiveSnapshotState::not_ready;
        }
        const auto entry = entries_[head_];
        head_ = (head_ + 1) % entries_.size();
        --size_;
        output = entry.snapshot;
        return entry.state;
    }

    std::uint32_t reads() const { return reads_; }

private:
    struct Entry {
        BreadcrumbArchiveSnapshotState state{
            BreadcrumbArchiveSnapshotState::not_ready};
        BreadcrumbArchiveRuntimeSnapshot snapshot{};
    };
    std::array<Entry, kCapacity> entries_{};
    std::size_t head_{0};
    std::size_t size_{0};
    std::uint32_t reads_{0};
};

struct Fixture {
    FakeSnapshotSource source{};
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface local{display, input, capabilities()};
    BreadcrumbArchiveUiCoordinator coordinator;

    explicit Fixture(std::uint32_t initial_revision = 1)
        : coordinator(source, local, initial_revision) {}
};

void test_initial_service_owns_revision_and_never_polls_input() {
    Fixture fixture{};
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, stopped_snapshot()));
    const auto result = fixture.coordinator.service();
    EXPECT(result.disposition == BreadcrumbArchiveUiDisposition::presented);
    EXPECT(result.snapshot_read);
    EXPECT(result.frame_presented);
    EXPECT(result.revision == 1);
    EXPECT(result.presented_notice == UiNotice::archive_stopped);
    EXPECT(fixture.source.reads() == 1);
    EXPECT(fixture.input.read_count() == 0);
    EXPECT(fixture.coordinator.status().next_revision == 2);
}

void test_unchanged_snapshot_skips_redraw_and_revision_churn() {
    Fixture fixture{};
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, stopped_snapshot()));
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, stopped_snapshot()));
    EXPECT(fixture.coordinator.service().frame_presented);
    const auto result = fixture.coordinator.service();
    EXPECT(result.disposition == BreadcrumbArchiveUiDisposition::unchanged);
    EXPECT(result.prior_frame_retained);
    EXPECT(!result.frame_presented);
    EXPECT(result.revision == 1);
    EXPECT(fixture.display.present_count() == 1);
    EXPECT(fixture.source.reads() == 2);
    EXPECT(fixture.coordinator.status().next_revision == 2);
}

void test_semantic_changes_refresh_with_strict_revisions() {
    Fixture fixture{};
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, stopped_snapshot()));
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, active_snapshot()));
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, active_snapshot(3)));
    EXPECT(fixture.coordinator.service().revision == 1);
    const auto active = fixture.coordinator.service();
    EXPECT(active.disposition == BreadcrumbArchiveUiDisposition::refreshed);
    EXPECT(active.revision == 2);
    EXPECT(active.presented_notice == UiNotice::archive_active);
    const auto queued = fixture.coordinator.service();
    EXPECT(queued.revision == 3);
    EXPECT(queued.presented_notice == UiNotice::archive_queued);
    EXPECT(fixture.display.latest_frame().status.archive_queue_count == 3);
    EXPECT(fixture.source.reads() == 3);
}

void test_snapshot_not_ready_retains_truth_and_reuses_revision() {
    Fixture fixture{};
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, stopped_snapshot()));
    EXPECT(fixture.source.enqueue(BreadcrumbArchiveSnapshotState::not_ready));
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, active_snapshot()));
    EXPECT(fixture.coordinator.service().frame_presented);
    const auto deferred = fixture.coordinator.service();
    EXPECT(deferred.disposition ==
           BreadcrumbArchiveUiDisposition::snapshot_deferred);
    EXPECT(deferred.prior_frame_retained);
    EXPECT(deferred.revision == 1);
    EXPECT(fixture.display.present_count() == 1);
    const auto recovered = fixture.coordinator.service();
    EXPECT(recovered.revision == 2);
    EXPECT(recovered.presented_notice == UiNotice::archive_active);
    EXPECT(fixture.source.reads() == 3);
}

void test_failed_and_unknown_sources_emit_redacted_warning() {
    Fixture failed{};
    auto partial = active_snapshot(9);
    EXPECT(failed.source.enqueue(
        BreadcrumbArchiveSnapshotState::failed, partial));
    const auto failed_result = failed.coordinator.service();
    EXPECT(failed_result.frame_presented);
    EXPECT(failed_result.presentation_error ==
           BreadcrumbArchivePresentationError::snapshot_failed);
    EXPECT(failed.display.latest_frame().notice ==
           UiNotice::archive_upload_failed);
    EXPECT(failed.display.latest_frame().status.archive_queue_count == 0);

    Fixture unknown{};
    EXPECT(unknown.source.enqueue(
        static_cast<BreadcrumbArchiveSnapshotState>(99), partial));
    const auto unknown_result = unknown.coordinator.service();
    EXPECT(unknown_result.frame_presented);
    EXPECT(unknown_result.presentation_error ==
           BreadcrumbArchivePresentationError::invalid_snapshot_state);
    EXPECT(unknown.display.latest_frame().status.archive_queue_count == 0);
}

void test_incoherent_ready_tuple_fails_visibly_without_system_fault() {
    Fixture fixture{};
    auto incoherent = active_snapshot();
    incoherent.session.scheduler.active = false;
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, incoherent));
    const auto result = fixture.coordinator.service();
    EXPECT(result.frame_presented);
    EXPECT(result.presentation_error ==
           BreadcrumbArchivePresentationError::incoherent_status);
    EXPECT(result.presented_notice == UiNotice::archive_upload_failed);
    EXPECT(fixture.display.latest_frame().screen == UiScreen::status);
    EXPECT(fixture.display.latest_frame().action_count == 0);
}

void test_display_not_ready_retries_same_revision_after_new_snapshot() {
    Fixture fixture{};
    EXPECT(fixture.display.enqueue_result(DisplayWriteError::not_ready));
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, stopped_snapshot()));
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, stopped_snapshot()));
    const auto deferred = fixture.coordinator.service();
    EXPECT(deferred.disposition ==
           BreadcrumbArchiveUiDisposition::display_deferred);
    EXPECT(deferred.present_error == PresentError::sink_not_ready);
    EXPECT(deferred.revision == 1);
    EXPECT(!fixture.coordinator.status().has_presented_frame);
    const auto retried = fixture.coordinator.service();
    EXPECT(retried.frame_presented);
    EXPECT(retried.revision == 1);
    EXPECT(fixture.source.reads() == 2);
}

void test_display_failure_retains_prior_frame_and_is_retryable() {
    Fixture fixture{};
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, stopped_snapshot()));
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, active_snapshot()));
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, active_snapshot()));
    EXPECT(fixture.coordinator.service().frame_presented);
    EXPECT(fixture.display.enqueue_result(DisplayWriteError::sink_failed));
    const auto failed = fixture.coordinator.service();
    EXPECT(failed.disposition == BreadcrumbArchiveUiDisposition::failed);
    EXPECT(failed.error == BreadcrumbArchiveUiError::display_failed);
    EXPECT(failed.prior_frame_retained);
    EXPECT(fixture.coordinator.status().active_revision == 1);
    EXPECT(fixture.coordinator.status().next_revision == 2);
    const auto recovered = fixture.coordinator.service();
    EXPECT(recovered.frame_presented);
    EXPECT(recovered.revision == 2);
    EXPECT(fixture.source.reads() == 3);
}

void test_maximum_revision_allows_unchanged_but_rejects_change() {
    const auto maximum = std::numeric_limits<std::uint32_t>::max();
    Fixture fixture{maximum};
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, stopped_snapshot()));
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, stopped_snapshot()));
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, active_snapshot()));
    EXPECT(fixture.coordinator.service().revision == maximum);
    EXPECT(fixture.coordinator.service().disposition ==
           BreadcrumbArchiveUiDisposition::unchanged);
    const auto exhausted = fixture.coordinator.service();
    EXPECT(exhausted.error == BreadcrumbArchiveUiError::revision_exhausted);
    EXPECT(exhausted.prior_frame_retained);
    EXPECT(fixture.display.present_count() == 1);
    EXPECT(fixture.source.reads() == 3);
}

void test_zero_revision_rejects_configuration_before_snapshot() {
    Fixture fixture{0};
    EXPECT(fixture.source.enqueue(
        BreadcrumbArchiveSnapshotState::ready, stopped_snapshot()));
    const auto result = fixture.coordinator.service();
    EXPECT(result.error ==
           BreadcrumbArchiveUiError::invalid_initial_revision);
    EXPECT(!result.snapshot_read);
    EXPECT(fixture.source.reads() == 0);
    EXPECT(fixture.display.present_count() == 0);
}

static_assert(std::is_trivially_copyable_v<
              BreadcrumbArchiveUiServiceResult>);
static_assert(std::is_trivially_copyable_v<
              BreadcrumbArchiveUiCoordinatorStatus>);
static_assert(sizeof(BreadcrumbArchiveUiServiceResult) <= 16);
static_assert(sizeof(BreadcrumbArchiveUiCoordinatorStatus) <= 48);

}  // namespace

int main() {
    test_initial_service_owns_revision_and_never_polls_input();
    test_unchanged_snapshot_skips_redraw_and_revision_churn();
    test_semantic_changes_refresh_with_strict_revisions();
    test_snapshot_not_ready_retains_truth_and_reuses_revision();
    test_failed_and_unknown_sources_emit_redacted_warning();
    test_incoherent_ready_tuple_fails_visibly_without_system_fault();
    test_display_not_ready_retries_same_revision_after_new_snapshot();
    test_display_failure_retains_prior_frame_and_is_retryable();
    test_maximum_revision_allows_unchanged_but_rejects_change();
    test_zero_revision_rejects_configuration_before_snapshot();

    if (failures != 0) {
        std::cerr << failures
                  << " breadcrumb archive UI coordinator assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 breadcrumb archive UI coordinator scenario groups\n";
    return EXIT_SUCCESS;
}
