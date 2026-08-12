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

DisplayCapabilities capabilities() {
    return {128, 64, 1, 2, false, true, false};
}

BreadcrumbArchiveStatus stopped_session() {
    BreadcrumbArchiveStatus status{};
    status.scheduler.policy_valid = true;
    return status;
}

BreadcrumbArchiveStatus active_session() {
    auto status = stopped_session();
    status.active = true;
    status.next_sequence = 1;
    status.sessions_started = 1;
    status.scheduler.active = true;
    status.scheduler.starts = 1;
    return status;
}

BreadcrumbArchiveOutboxStatus outbox(std::size_t queued = 0) {
    BreadcrumbArchiveOutboxStatus status{};
    status.queued = queued;
    status.has_prior_record = queued != 0;
    status.last_session_id = queued != 0 ? 10 : 0;
    status.last_sequence = static_cast<std::uint32_t>(queued);
    status.accepted = static_cast<std::uint32_t>(queued);
    return status;
}

BreadcrumbArchiveRetryStatus retry() {
    BreadcrumbArchiveRetryStatus status{};
    status.current_retry_ms = 10;
    return status;
}

void expect_presentable(
    const BreadcrumbArchivePresentationResult& result,
    UiNotice notice,
    UiAttention attention,
    std::uint8_t queued) {
    EXPECT(result.error == BreadcrumbArchivePresentationError::none);
    EXPECT(result.presentable());
    EXPECT(result.frame.screen == UiScreen::status);
    EXPECT(result.frame.notice == notice);
    EXPECT(result.frame.attention == attention);
    EXPECT(result.frame.status.archive_queue_count_valid);
    EXPECT(result.frame.status.archive_queue_count == queued);
    EXPECT(result.frame.action_count == 0);

    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface local{display, input, capabilities()};
    EXPECT(local.present(result.frame).ok());
}

void test_stopped_archive_is_visible_and_quiet() {
    const auto result = make_breadcrumb_archive_presentation(
        stopped_session(), outbox(), retry(), 1);
    expect_presentable(
        result, UiNotice::archive_stopped, UiAttention::none, 0);
}

void test_active_empty_archive_is_informational() {
    const auto result = make_breadcrumb_archive_presentation(
        active_session(), outbox(), retry(), 2);
    expect_presentable(
        result, UiNotice::archive_active, UiAttention::information, 0);
}

void test_queued_record_count_is_coarse_and_action_free() {
    const auto result = make_breadcrumb_archive_presentation(
        active_session(), outbox(3), retry(), 3);
    expect_presentable(
        result, UiNotice::archive_queued, UiAttention::information, 3);
}

void test_scheduled_retry_is_distinct_from_unscheduled_queue() {
    auto retrying = retry();
    retrying.next_attempt_scheduled = true;
    retrying.next_attempt_ms = 100;
    const auto result = make_breadcrumb_archive_presentation(
        active_session(), outbox(2), retrying, 4);
    expect_presentable(
        result,
        UiNotice::archive_upload_waiting,
        UiAttention::information,
        2);
}

void test_full_queue_overrides_retry_waiting() {
    auto full = outbox(kBreadcrumbArchiveOutboxCapacity);
    full.last_error = BreadcrumbArchiveOutboxError::full;
    auto retrying = retry();
    retrying.next_attempt_scheduled = true;
    retrying.next_attempt_ms = 200;
    const auto result = make_breadcrumb_archive_presentation(
        active_session(), full, retrying, 5);
    expect_presentable(
        result,
        UiNotice::archive_queue_full,
        UiAttention::warning,
        static_cast<std::uint8_t>(kBreadcrumbArchiveOutboxCapacity));
}

void test_retry_latch_is_optional_warning_without_execution_action() {
    auto failed = retry();
    failed.failed_latched = true;
    failed.latched_error = BreadcrumbArchiveRetryError::remote_rejected;
    failed.failed = 1;
    const auto result = make_breadcrumb_archive_presentation(
        stopped_session(), outbox(1), failed, 6);
    expect_presentable(
        result,
        UiNotice::archive_upload_failed,
        UiAttention::warning,
        1);
    EXPECT(result.frame.screen != UiScreen::system_fault);
}

void test_incoherent_session_fails_visibly_without_base_fault_claim() {
    auto incoherent = active_session();
    incoherent.scheduler.active = false;
    const auto result = make_breadcrumb_archive_presentation(
        incoherent, outbox(), retry(), 7);
    EXPECT(result.error ==
           BreadcrumbArchivePresentationError::incoherent_status);
    EXPECT(result.presentable());
    EXPECT(result.frame.notice == UiNotice::archive_upload_failed);
    EXPECT(result.frame.screen == UiScreen::status);
    EXPECT(result.frame.action_count == 0);
}

void test_incoherent_retry_schedule_fails_visibly() {
    auto incoherent = retry();
    incoherent.next_attempt_scheduled = true;
    incoherent.next_attempt_ms = 0;
    const auto result = make_breadcrumb_archive_presentation(
        active_session(), outbox(1), incoherent, 8);
    EXPECT(result.error ==
           BreadcrumbArchivePresentationError::incoherent_status);
    EXPECT(result.presentable());
    EXPECT(result.frame.notice == UiNotice::archive_upload_failed);
    EXPECT(result.frame.status.archive_queue_count == 1);
}

void test_oversized_queue_fails_with_redacted_zero_count() {
    auto impossible = outbox(kBreadcrumbArchiveOutboxCapacity + 1);
    const auto result = make_breadcrumb_archive_presentation(
        active_session(), impossible, retry(), 9);
    EXPECT(result.error ==
           BreadcrumbArchivePresentationError::incoherent_status);
    EXPECT(result.presentable());
    EXPECT(result.frame.notice == UiNotice::archive_upload_failed);
    EXPECT(result.frame.status.archive_queue_count == 0);
}

void test_zero_revision_cannot_create_a_presentable_frame() {
    const auto result = make_breadcrumb_archive_presentation(
        stopped_session(), outbox(), retry(), 0);
    EXPECT(result.error ==
           BreadcrumbArchivePresentationError::invalid_revision);
    EXPECT(!result.presentable());
    EXPECT(result.frame.revision == 0);
}

static_assert(std::is_trivially_copyable_v<
              BreadcrumbArchivePresentationResult>);
static_assert(sizeof(BreadcrumbArchivePresentationResult) <= 80);

}  // namespace

int main() {
    test_stopped_archive_is_visible_and_quiet();
    test_active_empty_archive_is_informational();
    test_queued_record_count_is_coarse_and_action_free();
    test_scheduled_retry_is_distinct_from_unscheduled_queue();
    test_full_queue_overrides_retry_waiting();
    test_retry_latch_is_optional_warning_without_execution_action();
    test_incoherent_session_fails_visibly_without_base_fault_claim();
    test_incoherent_retry_schedule_fails_visibly();
    test_oversized_queue_fails_with_redacted_zero_count();
    test_zero_revision_cannot_create_a_presentable_frame();
    if (failures != 0) {
        std::cerr << failures
                  << " breadcrumb archive presentation assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 breadcrumb archive presentation scenario groups\n";
    return EXIT_SUCCESS;
}
