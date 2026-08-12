#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "fake_breadcrumb_archive_remote.hpp"
#include "fake_monotonic_counter_source.hpp"
#include "opentrail/breadcrumb_archive_retry.hpp"

namespace {

using namespace opentrail::location;
using namespace opentrail::location::test_support;
using namespace opentrail::time;
using namespace opentrail::time::test_support;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

std::array<std::uint8_t, kBreadcrumbArchiveRecordBytes> encoded(
    std::uint64_t session_id,
    std::uint32_t sequence) {
    BreadcrumbArchiveRecord record{};
    record.session_id = session_id;
    record.sequence = sequence;
    record.captured_at_ms = sequence * 100;

    LocationSnapshot snapshot{};
    snapshot.state = FixState::valid;
    snapshot.error = FixError::none;
    snapshot.fix.latitude_e7 = 449775000;
    snapshot.fix.longitude_e7 = -677500000;
    snapshot.fix.horizontal_accuracy_valid = true;
    snapshot.fix.horizontal_accuracy_cm = 250;
    snapshot.age_ms = 25;
    EXPECT(encode_position(
               snapshot,
               {record.position_payload.data(), record.position_payload.size()})
               .encoded());

    std::array<std::uint8_t, kBreadcrumbArchiveRecordBytes> result{};
    EXPECT(encode_breadcrumb_archive_record(
               record, {result.data(), result.size()})
               .succeeded());
    return result;
}

opentrail::radio::ByteView view(
    const std::array<std::uint8_t, kBreadcrumbArchiveRecordBytes>& record) {
    return {record.data(), record.size()};
}

struct Fixture {
    FakeMonotonicCounterSource source{};
    CheckedMonotonicClock clock{source};
    BreadcrumbArchiveOutbox outbox{};
    FakeBreadcrumbArchiveRemote remote{};
    BreadcrumbArchiveUploader uploader{outbox, remote};
};

void test_invalid_policy_latches_without_clock_or_upload() {
    Fixture fixture{};
    const auto record = encoded(1, 1);
    EXPECT(fixture.outbox.enqueue(view(record)).accepted());
    BreadcrumbArchiveRetryCoordinator coordinator{
        fixture.clock, fixture.outbox, fixture.uploader, {0, 10}};
    const auto result = coordinator.service();
    EXPECT(result.disposition == BreadcrumbArchiveRetryDisposition::failed);
    EXPECT(result.error == BreadcrumbArchiveRetryError::latched_failure);
    EXPECT(coordinator.status().latched_error ==
           BreadcrumbArchiveRetryError::invalid_policy);
    EXPECT(fixture.source.read_count() == 0);
    EXPECT(fixture.remote.attempts() == 0);
    EXPECT(fixture.outbox.status().queued == 1);
}

void test_empty_queue_is_idle_without_clock_read() {
    Fixture fixture{};
    BreadcrumbArchiveRetryCoordinator coordinator{
        fixture.clock, fixture.outbox, fixture.uploader, {10, 40}};
    const auto result = coordinator.service();
    EXPECT(result.disposition == BreadcrumbArchiveRetryDisposition::idle);
    EXPECT(fixture.source.read_count() == 0);
    EXPECT(fixture.remote.attempts() == 0);
    EXPECT(coordinator.status().idle == 1);
}

void test_first_attempt_uses_checked_time_and_commits() {
    Fixture fixture{};
    const auto record = encoded(2, 1);
    EXPECT(fixture.outbox.enqueue(view(record)).accepted());
    EXPECT(fixture.source.enqueue_time(123));
    BreadcrumbArchiveRetryCoordinator coordinator{
        fixture.clock, fixture.outbox, fixture.uploader, {10, 40}};
    const auto result = coordinator.service();
    EXPECT(result.disposition == BreadcrumbArchiveRetryDisposition::attempted);
    EXPECT(result.upload.committed());
    EXPECT(fixture.remote.attempts() == 1);
    EXPECT(fixture.remote.attempted_at(0) == 123);
    EXPECT(fixture.outbox.status().queued == 0);
    EXPECT(!coordinator.status().next_attempt_scheduled);
}

void test_not_ready_waits_until_exact_deadline_without_hammering() {
    Fixture fixture{};
    const auto record = encoded(3, 1);
    EXPECT(fixture.outbox.enqueue(view(record)).accepted());
    EXPECT(fixture.remote.push_result(BreadcrumbArchiveRemoteResult::not_ready));
    EXPECT(fixture.remote.push_result(BreadcrumbArchiveRemoteResult::durable_ack));
    EXPECT(fixture.source.enqueue_time(100));
    EXPECT(fixture.source.enqueue_time(109));
    EXPECT(fixture.source.enqueue_time(110));
    BreadcrumbArchiveRetryCoordinator coordinator{
        fixture.clock, fixture.outbox, fixture.uploader, {10, 40}};

    const auto deferred = coordinator.service();
    EXPECT(deferred.next_attempt_scheduled && deferred.next_attempt_ms == 110);
    const auto waiting = coordinator.service();
    EXPECT(waiting.disposition == BreadcrumbArchiveRetryDisposition::waiting);
    EXPECT(fixture.remote.attempts() == 1);
    const auto committed = coordinator.service();
    EXPECT(committed.upload.committed());
    EXPECT(fixture.remote.attempts() == 2);
    EXPECT(*fixture.remote.record_at(0) == record);
    EXPECT(*fixture.remote.record_at(1) == record);
}

void test_transient_failures_double_and_cap_retry_delay() {
    Fixture fixture{};
    EXPECT(fixture.outbox.enqueue(view(encoded(4, 1))).accepted());
    for (int index = 0; index < 4; ++index) {
        EXPECT(fixture.remote.push_result(BreadcrumbArchiveRemoteResult::failed));
    }
    for (const auto now : {100ULL, 110ULL, 130ULL, 170ULL}) {
        EXPECT(fixture.source.enqueue_time(now));
    }
    BreadcrumbArchiveRetryCoordinator coordinator{
        fixture.clock, fixture.outbox, fixture.uploader, {10, 40}};
    EXPECT(coordinator.service().next_attempt_ms == 110);
    EXPECT(coordinator.service().next_attempt_ms == 130);
    EXPECT(coordinator.service().next_attempt_ms == 170);
    EXPECT(coordinator.service().next_attempt_ms == 210);
    EXPECT(coordinator.status().current_retry_ms == 40);
    EXPECT(fixture.remote.attempts() == 4);
    EXPECT(fixture.outbox.status().queued == 1);
}

void test_commit_resets_backoff_for_next_fifo_head() {
    Fixture fixture{};
    EXPECT(fixture.outbox.enqueue(view(encoded(5, 1))).accepted());
    EXPECT(fixture.outbox.enqueue(view(encoded(5, 2))).accepted());
    EXPECT(fixture.remote.push_result(BreadcrumbArchiveRemoteResult::not_ready));
    EXPECT(fixture.remote.push_result(BreadcrumbArchiveRemoteResult::durable_ack));
    EXPECT(fixture.remote.push_result(BreadcrumbArchiveRemoteResult::failed));
    for (const auto now : {10ULL, 20ULL, 20ULL}) {
        EXPECT(fixture.source.enqueue_time(now));
    }
    BreadcrumbArchiveRetryCoordinator coordinator{
        fixture.clock, fixture.outbox, fixture.uploader, {10, 40}};
    EXPECT(coordinator.service().next_attempt_ms == 20);
    EXPECT(coordinator.service().upload.committed());
    const auto second = coordinator.service();
    EXPECT(second.next_attempt_ms == 30);
    EXPECT(coordinator.status().current_retry_ms == 20);
    EXPECT(fixture.outbox.peek().sequence == 2);
}

void test_clock_not_ready_defers_without_upload_then_recovers() {
    Fixture fixture{};
    EXPECT(fixture.outbox.enqueue(view(encoded(6, 1))).accepted());
    EXPECT(fixture.source.enqueue_not_ready());
    EXPECT(fixture.source.enqueue_time(50));
    BreadcrumbArchiveRetryCoordinator coordinator{
        fixture.clock, fixture.outbox, fixture.uploader, {10, 40}};
    const auto deferred = coordinator.service();
    EXPECT(deferred.disposition ==
           BreadcrumbArchiveRetryDisposition::clock_deferred);
    EXPECT(deferred.error == BreadcrumbArchiveRetryError::clock_not_ready);
    EXPECT(fixture.remote.attempts() == 0);
    EXPECT(coordinator.service().upload.committed());
    EXPECT(fixture.remote.attempts() == 1);
}

void test_clock_failure_and_rollback_latch_closed() {
    Fixture failed{};
    EXPECT(failed.outbox.enqueue(view(encoded(7, 1))).accepted());
    EXPECT(failed.source.enqueue_failure());
    BreadcrumbArchiveRetryCoordinator failed_coordinator{
        failed.clock, failed.outbox, failed.uploader, {10, 40}};
    EXPECT(failed_coordinator.service().error ==
           BreadcrumbArchiveRetryError::clock_failed);
    EXPECT(failed_coordinator.status().failed_latched);
    EXPECT(failed.remote.attempts() == 0);

    Fixture rollback{};
    EXPECT(rollback.outbox.enqueue(view(encoded(8, 1))).accepted());
    EXPECT(rollback.remote.push_result(BreadcrumbArchiveRemoteResult::not_ready));
    EXPECT(rollback.source.enqueue_time(100));
    EXPECT(rollback.source.enqueue_time(99));
    BreadcrumbArchiveRetryCoordinator rollback_coordinator{
        rollback.clock, rollback.outbox, rollback.uploader, {10, 40}};
    EXPECT(rollback_coordinator.service().next_attempt_ms == 110);
    EXPECT(rollback_coordinator.service().error ==
           BreadcrumbArchiveRetryError::clock_failed);
    EXPECT(rollback.remote.attempts() == 1);
    EXPECT(rollback.outbox.status().queued == 1);
}

void test_remote_rejection_latches_without_repeated_upload() {
    Fixture fixture{};
    EXPECT(fixture.outbox.enqueue(view(encoded(9, 1))).accepted());
    EXPECT(fixture.remote.push_result(BreadcrumbArchiveRemoteResult::rejected));
    EXPECT(fixture.source.enqueue_time(100));
    BreadcrumbArchiveRetryCoordinator coordinator{
        fixture.clock, fixture.outbox, fixture.uploader, {10, 40}};
    const auto rejected = coordinator.service();
    EXPECT(rejected.error == BreadcrumbArchiveRetryError::remote_rejected);
    EXPECT(rejected.queue_retained);
    EXPECT(coordinator.status().failed_latched);
    EXPECT(coordinator.service().error ==
           BreadcrumbArchiveRetryError::latched_failure);
    EXPECT(fixture.remote.attempts() == 1);
}

void test_deadline_overflow_latches_and_retains_record() {
    Fixture fixture{};
    EXPECT(fixture.outbox.enqueue(view(encoded(10, 1))).accepted());
    EXPECT(fixture.remote.push_result(BreadcrumbArchiveRemoteResult::not_ready));
    EXPECT(fixture.source.enqueue_time(
        std::numeric_limits<std::uint64_t>::max() - 5));
    BreadcrumbArchiveRetryCoordinator coordinator{
        fixture.clock, fixture.outbox, fixture.uploader, {10, 40}};
    const auto result = coordinator.service();
    EXPECT(result.error == BreadcrumbArchiveRetryError::deadline_overflow);
    EXPECT(result.queue_retained);
    EXPECT(coordinator.status().failed_latched);
    EXPECT(fixture.outbox.status().queued == 1);
}

}  // namespace

int main() {
    test_invalid_policy_latches_without_clock_or_upload();
    test_empty_queue_is_idle_without_clock_read();
    test_first_attempt_uses_checked_time_and_commits();
    test_not_ready_waits_until_exact_deadline_without_hammering();
    test_transient_failures_double_and_cap_retry_delay();
    test_commit_resets_backoff_for_next_fifo_head();
    test_clock_not_ready_defers_without_upload_then_recovers();
    test_clock_failure_and_rollback_latch_closed();
    test_remote_rejection_latches_without_repeated_upload();
    test_deadline_overflow_latches_and_retains_record();
    if (failures != 0) {
        std::cerr << failures
                  << " breadcrumb archive retry assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 breadcrumb archive retry scenario groups\n";
    return EXIT_SUCCESS;
}
