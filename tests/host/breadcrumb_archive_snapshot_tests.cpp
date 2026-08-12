#include <array>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "fake_local_interface.hpp"
#include "opentrail/breadcrumb_archive_presentation.hpp"

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

struct ScriptedSnapshot {
    BreadcrumbArchiveSnapshotState state{
        BreadcrumbArchiveSnapshotState::not_ready};
    BreadcrumbArchiveRuntimeSnapshot snapshot{};
};

class ScriptedSnapshotSource final : public BreadcrumbArchiveSnapshotSource {
public:
    bool push(const ScriptedSnapshot& scripted) {
        if (count_ == scripts_.size()) {
            return false;
        }
        scripts_[count_++] = scripted;
        return true;
    }

    BreadcrumbArchiveSnapshotState snapshot(
        BreadcrumbArchiveRuntimeSnapshot& output) override {
        ++reads_;
        if (head_ == count_) {
            return BreadcrumbArchiveSnapshotState::not_ready;
        }
        const auto& scripted = scripts_[head_++];
        output = scripted.snapshot;
        return scripted.state;
    }

    [[nodiscard]] std::size_t reads() const {
        return reads_;
    }

private:
    std::array<ScriptedSnapshot, 8> scripts_{};
    std::size_t head_{0};
    std::size_t count_{0};
    std::size_t reads_{0};
};

BreadcrumbArchiveRuntimeSnapshot stopped_snapshot() {
    BreadcrumbArchiveRuntimeSnapshot snapshot{};
    snapshot.session.scheduler.policy_valid = true;
    snapshot.retry.current_retry_ms = 10;
    return snapshot;
}

BreadcrumbArchiveRuntimeSnapshot active_snapshot(std::size_t queued = 0) {
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

void expect_checked_frame(
    const BreadcrumbArchivePresentationResult& result,
    UiNotice notice,
    std::uint8_t queued) {
    EXPECT(result.presentable());
    EXPECT(result.frame.notice == notice);
    EXPECT(result.frame.status.archive_queue_count_valid);
    EXPECT(result.frame.status.archive_queue_count == queued);
    EXPECT(result.frame.action_count == 0);

    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface local{
        display,
        input,
        {128, 64, 1, 2, false, true, false}};
    EXPECT(local.present(result.frame).ok());
}

void test_ready_snapshot_is_read_once_and_presented() {
    ScriptedSnapshotSource source{};
    EXPECT(source.push({
        BreadcrumbArchiveSnapshotState::ready,
        stopped_snapshot(),
    }));
    const auto result = capture_breadcrumb_archive_presentation(source, 1);
    EXPECT(result.error == BreadcrumbArchivePresentationError::none);
    EXPECT(source.reads() == 1);
    expect_checked_frame(result, UiNotice::archive_stopped, 0);
}

void test_one_tuple_preserves_queued_waiting_state() {
    auto snapshot = active_snapshot(3);
    snapshot.retry.next_attempt_scheduled = true;
    snapshot.retry.next_attempt_ms = 100;
    ScriptedSnapshotSource source{};
    EXPECT(source.push({BreadcrumbArchiveSnapshotState::ready, snapshot}));
    const auto result = capture_breadcrumb_archive_presentation(source, 2);
    EXPECT(source.reads() == 1);
    expect_checked_frame(result, UiNotice::archive_upload_waiting, 3);
}

void test_not_ready_exposes_no_partial_snapshot_or_frame() {
    auto partial = active_snapshot(7);
    ScriptedSnapshotSource source{};
    EXPECT(source.push({BreadcrumbArchiveSnapshotState::not_ready, partial}));
    const auto result = capture_breadcrumb_archive_presentation(source, 3);
    EXPECT(result.error ==
           BreadcrumbArchivePresentationError::snapshot_not_ready);
    EXPECT(!result.presentable());
    EXPECT(result.frame.status.archive_queue_count == 0);
    EXPECT(source.reads() == 1);
}

void test_failed_source_redacts_partial_output_and_warns() {
    auto partial = active_snapshot(6);
    ScriptedSnapshotSource source{};
    EXPECT(source.push({BreadcrumbArchiveSnapshotState::failed, partial}));
    const auto result = capture_breadcrumb_archive_presentation(source, 4);
    EXPECT(result.error == BreadcrumbArchivePresentationError::snapshot_failed);
    EXPECT(result.frame.screen == UiScreen::status);
    EXPECT(result.frame.attention == UiAttention::warning);
    expect_checked_frame(result, UiNotice::archive_upload_failed, 0);
}

void test_unknown_source_state_redacts_and_fails_visibly() {
    ScriptedSnapshotSource source{};
    EXPECT(source.push({
        static_cast<BreadcrumbArchiveSnapshotState>(255),
        active_snapshot(5),
    }));
    const auto result = capture_breadcrumb_archive_presentation(source, 5);
    EXPECT(result.error ==
           BreadcrumbArchivePresentationError::invalid_snapshot_state);
    expect_checked_frame(result, UiNotice::archive_upload_failed, 0);
}

void test_zero_revision_refuses_before_source_access() {
    ScriptedSnapshotSource source{};
    EXPECT(source.push({
        BreadcrumbArchiveSnapshotState::ready,
        stopped_snapshot(),
    }));
    const auto result = capture_breadcrumb_archive_presentation(source, 0);
    EXPECT(result.error == BreadcrumbArchivePresentationError::invalid_revision);
    EXPECT(!result.presentable());
    EXPECT(source.reads() == 0);
}

void test_incoherent_ready_tuple_uses_existing_safe_fallback() {
    auto incoherent = active_snapshot();
    incoherent.session.scheduler.active = false;
    ScriptedSnapshotSource source{};
    EXPECT(source.push({BreadcrumbArchiveSnapshotState::ready, incoherent}));
    const auto result = capture_breadcrumb_archive_presentation(source, 6);
    EXPECT(result.error ==
           BreadcrumbArchivePresentationError::incoherent_status);
    expect_checked_frame(result, UiNotice::archive_upload_failed, 0);
}

void test_each_capture_consumes_only_one_scripted_tuple() {
    ScriptedSnapshotSource source{};
    EXPECT(source.push({
        BreadcrumbArchiveSnapshotState::ready,
        stopped_snapshot(),
    }));
    EXPECT(source.push({
        BreadcrumbArchiveSnapshotState::ready,
        active_snapshot(),
    }));
    const auto first = capture_breadcrumb_archive_presentation(source, 7);
    const auto second = capture_breadcrumb_archive_presentation(source, 8);
    EXPECT(source.reads() == 2);
    expect_checked_frame(first, UiNotice::archive_stopped, 0);
    expect_checked_frame(second, UiNotice::archive_active, 0);
}

void test_exhausted_script_defers_without_reusing_prior_data() {
    ScriptedSnapshotSource source{};
    EXPECT(source.push({
        BreadcrumbArchiveSnapshotState::ready,
        active_snapshot(2),
    }));
    const auto first = capture_breadcrumb_archive_presentation(source, 9);
    const auto second = capture_breadcrumb_archive_presentation(source, 10);
    expect_checked_frame(first, UiNotice::archive_queued, 2);
    EXPECT(second.error ==
           BreadcrumbArchivePresentationError::snapshot_not_ready);
    EXPECT(!second.presentable());
    EXPECT(second.frame.status.archive_queue_count == 0);
}

void test_fake_source_is_fixed_capacity() {
    ScriptedSnapshotSource source{};
    for (std::size_t index = 0; index < 8; ++index) {
        EXPECT(source.push({
            BreadcrumbArchiveSnapshotState::not_ready,
            stopped_snapshot(),
        }));
    }
    EXPECT(!source.push({
        BreadcrumbArchiveSnapshotState::ready,
        stopped_snapshot(),
    }));
}

static_assert(std::is_trivially_copyable_v<BreadcrumbArchiveRuntimeSnapshot>);
static_assert(sizeof(BreadcrumbArchiveRuntimeSnapshot) <= 208);

}  // namespace

int main() {
    test_ready_snapshot_is_read_once_and_presented();
    test_one_tuple_preserves_queued_waiting_state();
    test_not_ready_exposes_no_partial_snapshot_or_frame();
    test_failed_source_redacts_partial_output_and_warns();
    test_unknown_source_state_redacts_and_fails_visibly();
    test_zero_revision_refuses_before_source_access();
    test_incoherent_ready_tuple_uses_existing_safe_fallback();
    test_each_capture_consumes_only_one_scripted_tuple();
    test_exhausted_script_defers_without_reusing_prior_data();
    test_fake_source_is_fixed_capacity();
    if (failures != 0) {
        std::cerr << failures
                  << " breadcrumb archive snapshot assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 breadcrumb archive snapshot scenario groups\n";
    return EXIT_SUCCESS;
}
