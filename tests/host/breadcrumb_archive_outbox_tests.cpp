#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_breadcrumb_archive_remote.hpp"
#include "opentrail/breadcrumb_archive_outbox.hpp"

namespace {

using namespace opentrail::location;
using namespace opentrail::location::test_support;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

LocationSnapshot current(std::int32_t latitude_e7 = 449775000) {
    LocationSnapshot snapshot{};
    snapshot.state = FixState::valid;
    snapshot.error = FixError::none;
    snapshot.fix.latitude_e7 = latitude_e7;
    snapshot.fix.longitude_e7 = -677500000;
    snapshot.fix.horizontal_accuracy_valid = true;
    snapshot.fix.horizontal_accuracy_cm = 250;
    snapshot.age_ms = 25;
    return snapshot;
}

std::array<std::uint8_t, kBreadcrumbArchiveRecordBytes> encoded(
    std::uint64_t session_id,
    std::uint32_t sequence,
    std::uint64_t captured_at_ms = 100,
    std::int32_t latitude_e7 = 449775000) {
    BreadcrumbArchiveRecord record{};
    record.session_id = session_id;
    record.sequence = sequence;
    record.captured_at_ms = captured_at_ms;
    EXPECT(encode_position(
               current(latitude_e7),
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

PositionBroadcastSchedulePolicy policy() {
    return {100, 20};
}

void test_fifo_peek_and_exact_commit() {
    BreadcrumbArchiveOutbox outbox{};
    const auto first = encoded(10, 1);
    const auto second = encoded(10, 2, 200);
    EXPECT(outbox.enqueue(view(first)).accepted());
    EXPECT(outbox.enqueue(view(second)).accepted());
    EXPECT(outbox.status().queued == 2);
    EXPECT(outbox.peek().has_record);
    EXPECT(outbox.peek().session_id == 10);
    EXPECT(outbox.peek().sequence == 1);
    EXPECT(outbox.peek().record == first);
    EXPECT(outbox.commit_front(10, 2).error ==
           BreadcrumbArchiveOutboxError::commit_mismatch);
    EXPECT(outbox.status().queued == 2);
    EXPECT(outbox.commit_front(10, 1).accepted());
    EXPECT(outbox.peek().record == second);
    EXPECT(outbox.status().committed == 1);
}

void test_invalid_records_never_mutate_queue() {
    BreadcrumbArchiveOutbox outbox{};
    const auto good = encoded(1, 1);
    EXPECT(outbox.enqueue({good.data(), good.size() - 1}).error ==
           BreadcrumbArchiveOutboxError::invalid_record);
    auto corrupt = good;
    corrupt[32] ^= 0x01U;
    EXPECT(outbox.enqueue(view(corrupt)).error ==
           BreadcrumbArchiveOutboxError::invalid_record);
    auto noncanonical = good;
    noncanonical[20] = 1;
    EXPECT(outbox.enqueue(view(noncanonical)).error ==
           BreadcrumbArchiveOutboxError::invalid_record);
    EXPECT(outbox.status().queued == 0);
    EXPECT(outbox.status().rejected_invalid == 3);
}

void test_order_and_duplicate_fail_closed() {
    BreadcrumbArchiveOutbox outbox{};
    EXPECT(outbox.enqueue(view(encoded(10, 2))).error ==
           BreadcrumbArchiveOutboxError::out_of_order);
    const auto first = encoded(10, 1, 100);
    EXPECT(outbox.enqueue(view(first)).accepted());
    EXPECT(outbox.enqueue(view(first)).error ==
           BreadcrumbArchiveOutboxError::duplicate_record);
    const auto conflict = encoded(10, 1, 101);
    EXPECT(outbox.enqueue(view(conflict)).error ==
           BreadcrumbArchiveOutboxError::duplicate_record);
    EXPECT(outbox.enqueue(view(encoded(10, 3))).error ==
           BreadcrumbArchiveOutboxError::out_of_order);
    EXPECT(outbox.enqueue(view(encoded(9, 1))).error ==
           BreadcrumbArchiveOutboxError::out_of_order);
    EXPECT(outbox.enqueue(view(encoded(11, 2))).error ==
           BreadcrumbArchiveOutboxError::out_of_order);
    EXPECT(outbox.enqueue(view(encoded(10, 2))).accepted());
    EXPECT(outbox.enqueue(view(encoded(11, 1))).accepted());
    const auto status = outbox.status();
    EXPECT(status.queued == 3);
    EXPECT(status.rejected_duplicate == 2);
    EXPECT(status.rejected_order == 4);
}

void test_full_queue_never_overwrites_and_retry_can_follow_commit() {
    BreadcrumbArchiveOutbox outbox{};
    for (std::uint32_t sequence = 1;
         sequence <= kBreadcrumbArchiveOutboxCapacity;
         ++sequence) {
        EXPECT(outbox.enqueue(view(encoded(1, sequence))).accepted());
    }
    const auto next = encoded(
        1, static_cast<std::uint32_t>(kBreadcrumbArchiveOutboxCapacity + 1));
    EXPECT(outbox.enqueue(view(next)).error ==
           BreadcrumbArchiveOutboxError::full);
    EXPECT(outbox.status().queued == kBreadcrumbArchiveOutboxCapacity);
    EXPECT(outbox.peek().sequence == 1);
    EXPECT(outbox.commit_front(1, 1).accepted());
    EXPECT(outbox.enqueue(view(next)).accepted());
    EXPECT(outbox.status().queued == kBreadcrumbArchiveOutboxCapacity);
    EXPECT(outbox.peek().sequence == 2);
}

void test_archive_session_composes_into_outbox_without_radio() {
    BreadcrumbArchiveOutbox outbox{};
    BreadcrumbArchiveSession session{outbox, policy()};
    EXPECT(session.start(700, 0).started());
    EXPECT(session.service(current(), 0).submitted());
    EXPECT(outbox.status().queued == 1);
    const auto front = outbox.peek();
    EXPECT(front.session_id == 700);
    EXPECT(front.sequence == 1);
    BreadcrumbArchiveRecord decoded{};
    EXPECT(decode_breadcrumb_archive_record(
               {front.record.data(), front.record.size()}, decoded)
               .succeeded());
    EXPECT(decoded.captured_at_ms == 0);
    session.stop();
    EXPECT(outbox.status().queued == 1);
}

void test_durable_ack_alone_commits_fifo_head() {
    BreadcrumbArchiveOutbox outbox{};
    const auto first = encoded(3, 1);
    const auto second = encoded(3, 2);
    EXPECT(outbox.enqueue(view(first)).accepted());
    EXPECT(outbox.enqueue(view(second)).accepted());
    FakeBreadcrumbArchiveRemote remote{};
    BreadcrumbArchiveUploader uploader{outbox, remote};
    const auto result = uploader.service(500);
    EXPECT(result.committed());
    EXPECT(result.session_id == 3 && result.sequence == 1);
    EXPECT(!result.queue_retained);
    EXPECT(remote.attempts() == 1);
    EXPECT(remote.record_at(0) != nullptr && *remote.record_at(0) == first);
    EXPECT(remote.attempted_at(0) == 500);
    EXPECT(outbox.status().queued == 1);
    EXPECT(outbox.peek().record == second);
}

void test_not_ready_retries_identical_head_without_loss() {
    BreadcrumbArchiveOutbox outbox{};
    const auto record = encoded(4, 1);
    EXPECT(outbox.enqueue(view(record)).accepted());
    FakeBreadcrumbArchiveRemote remote{};
    EXPECT(remote.push_result(BreadcrumbArchiveRemoteResult::not_ready));
    EXPECT(remote.push_result(BreadcrumbArchiveRemoteResult::durable_ack));
    BreadcrumbArchiveUploader uploader{outbox, remote};
    const auto deferred = uploader.service(100);
    EXPECT(deferred.disposition ==
           BreadcrumbArchiveUploadDisposition::deferred);
    EXPECT(deferred.queue_retained && outbox.status().queued == 1);
    const auto committed = uploader.service(120);
    EXPECT(committed.committed() && outbox.status().queued == 0);
    EXPECT(remote.attempts() == 2);
    EXPECT(*remote.record_at(0) == record);
    EXPECT(*remote.record_at(1) == record);
    EXPECT(uploader.status().deferred == 1);
    EXPECT(uploader.status().committed == 1);
}

void test_remote_rejection_and_failure_retain_head_visibly() {
    BreadcrumbArchiveOutbox outbox{};
    EXPECT(outbox.enqueue(view(encoded(5, 1))).accepted());
    FakeBreadcrumbArchiveRemote remote{};
    EXPECT(remote.push_result(BreadcrumbArchiveRemoteResult::rejected));
    EXPECT(remote.push_result(BreadcrumbArchiveRemoteResult::failed));
    BreadcrumbArchiveUploader uploader{outbox, remote};
    const auto rejected = uploader.service(1);
    EXPECT(rejected.disposition ==
           BreadcrumbArchiveUploadDisposition::rejected);
    EXPECT(rejected.error == BreadcrumbArchiveUploadError::remote_rejected);
    EXPECT(rejected.queue_retained && outbox.status().queued == 1);
    const auto failed = uploader.service(2);
    EXPECT(failed.disposition == BreadcrumbArchiveUploadDisposition::failed);
    EXPECT(failed.error == BreadcrumbArchiveUploadError::remote_failed);
    EXPECT(failed.queue_retained && outbox.status().queued == 1);
    EXPECT(!uploader.status().failed_latched);
    EXPECT(uploader.status().rejected == 1);
    EXPECT(uploader.status().failed == 1);
}

class MutatingAckRemote final : public BreadcrumbArchiveRemoteTransport {
public:
    explicit MutatingAckRemote(BreadcrumbArchiveOutbox& outbox)
        : outbox_(outbox) {}

    BreadcrumbArchiveRemoteResult upload(
        opentrail::radio::ByteView record,
        std::uint64_t now_ms) override {
        static_cast<void>(now_ms);
        ++attempts;
        BreadcrumbArchiveRecord decoded{};
        EXPECT(decode_breadcrumb_archive_record(record, decoded).succeeded());
        EXPECT(outbox_.commit_front(decoded.session_id, decoded.sequence)
                   .accepted());
        return BreadcrumbArchiveRemoteResult::durable_ack;
    }

    std::uint32_t attempts{0};

private:
    BreadcrumbArchiveOutbox& outbox_;
};

void test_post_ack_commit_mismatch_latches_uploader_closed() {
    BreadcrumbArchiveOutbox outbox{};
    EXPECT(outbox.enqueue(view(encoded(6, 1))).accepted());
    MutatingAckRemote remote{outbox};
    BreadcrumbArchiveUploader uploader{outbox, remote};
    const auto mismatch = uploader.service(10);
    EXPECT(mismatch.disposition ==
           BreadcrumbArchiveUploadDisposition::failed);
    EXPECT(mismatch.error ==
           BreadcrumbArchiveUploadError::queue_commit_mismatch);
    EXPECT(uploader.status().failed_latched);
    EXPECT(remote.attempts == 1);
    const auto latched = uploader.service(11);
    EXPECT(latched.error == BreadcrumbArchiveUploadError::latched_failure);
    EXPECT(remote.attempts == 1);
}

void test_explicit_discard_wipes_queue_but_retains_order_history() {
    BreadcrumbArchiveOutbox outbox{};
    const auto first = encoded(9, 1);
    const auto second = encoded(9, 2);
    EXPECT(outbox.enqueue(view(first)).accepted());
    EXPECT(outbox.enqueue(view(second)).accepted());
    EXPECT(outbox.discard_all() == 2);
    EXPECT(outbox.status().queued == 0);
    EXPECT(!outbox.peek().has_record);
    EXPECT(outbox.status().discarded == 2);
    EXPECT(outbox.enqueue(view(second)).error ==
           BreadcrumbArchiveOutboxError::duplicate_record);
    EXPECT(outbox.enqueue(view(encoded(9, 3))).accepted());
    EXPECT(outbox.discard_all() == 1);

    FakeBreadcrumbArchiveRemote remote{};
    BreadcrumbArchiveUploader uploader{outbox, remote};
    EXPECT(uploader.service(0).disposition ==
           BreadcrumbArchiveUploadDisposition::idle);
    EXPECT(remote.attempts() == 0);
}

}  // namespace

int main() {
    test_fifo_peek_and_exact_commit();
    test_invalid_records_never_mutate_queue();
    test_order_and_duplicate_fail_closed();
    test_full_queue_never_overwrites_and_retry_can_follow_commit();
    test_archive_session_composes_into_outbox_without_radio();
    test_durable_ack_alone_commits_fifo_head();
    test_not_ready_retries_identical_head_without_loss();
    test_remote_rejection_and_failure_retain_head_visibly();
    test_post_ack_commit_mismatch_latches_uploader_closed();
    test_explicit_discard_wipes_queue_but_retains_order_history();
    if (failures != 0) {
        std::cerr << failures
                  << " breadcrumb archive outbox assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 breadcrumb archive outbox scenario groups\n";
    return EXIT_SUCCESS;
}
