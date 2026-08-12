#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "fake_breadcrumb_archive_transport.hpp"
#include "opentrail/breadcrumb_archive.hpp"

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

BreadcrumbArchiveRecord decoded_at(
    const FakeBreadcrumbArchiveTransport& transport,
    std::size_t index) {
    BreadcrumbArchiveRecord record{};
    const auto* bytes = transport.at(index);
    EXPECT(bytes != nullptr);
    if (bytes != nullptr) {
        EXPECT(decode_breadcrumb_archive_record(
                   {bytes->data(), bytes->size()}, record).succeeded());
    }
    return record;
}

BreadcrumbArchiveRecord record() {
    BreadcrumbArchiveRecord result{};
    result.session_id = 0x0102030405060708ULL;
    result.sequence = 0x11223344U;
    result.captured_at_ms = 0x0102030405060708ULL;
    std::array<std::uint8_t, kPositionPayloadBytes> payload{};
    EXPECT(encode_position(
               current(), {payload.data(), payload.size()}).encoded());
    result.position_payload = payload;
    return result;
}

void test_codec_exact_shape_and_round_trip() {
    const auto expected = record();
    std::array<std::uint8_t, kBreadcrumbArchiveRecordBytes> encoded{};
    const auto result = encode_breadcrumb_archive_record(
        expected, {encoded.data(), encoded.size()});
    EXPECT(result.succeeded());
    EXPECT(result.bytes == encoded.size());
    EXPECT(encoded[0] == 'O' && encoded[1] == 'T' &&
           encoded[2] == 'B' && encoded[3] == 'A');
    EXPECT(encoded[4] == 0 && encoded[5] == 1);
    EXPECT(encoded[6] == 32 && encoded[7] == 0);
    EXPECT(encoded[8] == 0x08 && encoded[15] == 0x01);
    EXPECT(encoded[16] == 0x44 && encoded[19] == 0x11);
    EXPECT(encoded[20] == 0 && encoded[23] == 0);
    EXPECT(encoded[24] == 0x08 && encoded[31] == 0x01);
    EXPECT(encoded[32] == 0 && encoded[33] == 1);
    EXPECT(encoded[48] == 0 && encoded[51] == 0);

    BreadcrumbArchiveRecord decoded{};
    EXPECT(decode_breadcrumb_archive_record(
               {encoded.data(), encoded.size()}, decoded).succeeded());
    EXPECT(decoded.session_id == expected.session_id);
    EXPECT(decoded.sequence == expected.sequence);
    EXPECT(decoded.captured_at_ms == expected.captured_at_ms);
    EXPECT(decoded.position_payload == expected.position_payload);
}

void test_codec_rejects_invalid_input_atomically() {
    auto candidate = record();
    std::array<std::uint8_t, kBreadcrumbArchiveRecordBytes> encoded{};
    encoded.fill(0xA5U);
    candidate.session_id = 0;
    EXPECT(encode_breadcrumb_archive_record(
               candidate, {encoded.data(), encoded.size()}).error ==
           BreadcrumbArchiveCodecError::invalid_record);
    EXPECT(encoded.front() == 0xA5U && encoded.back() == 0xA5U);
    candidate = record();
    candidate.sequence = 0;
    EXPECT(encode_breadcrumb_archive_record(
               candidate, {encoded.data(), encoded.size()}).error ==
           BreadcrumbArchiveCodecError::invalid_record);
    candidate = record();
    auto stale = current();
    stale.state = FixState::stale;
    EXPECT(encode_position(
               stale,
               {candidate.position_payload.data(),
                candidate.position_payload.size()}).encoded());
    EXPECT(encode_breadcrumb_archive_record(
               candidate, {encoded.data(), encoded.size()}).error ==
           BreadcrumbArchiveCodecError::invalid_position);
    EXPECT(encode_breadcrumb_archive_record(
               record(), {encoded.data(), encoded.size() - 1}).error ==
           BreadcrumbArchiveCodecError::output_too_small);

    candidate = record();
    EXPECT(encode_breadcrumb_archive_record(
               candidate, {encoded.data(), encoded.size()}).succeeded());
    BreadcrumbArchiveRecord output = record();
    const auto original = output;
    encoded[20] = 1;
    EXPECT(decode_breadcrumb_archive_record(
               {encoded.data(), encoded.size()}, output).error ==
           BreadcrumbArchiveCodecError::noncanonical_record);
    EXPECT(output.session_id == original.session_id &&
           output.sequence == original.sequence);
    encoded[20] = 0;
    encoded[40] ^= 0x80U;
    EXPECT(decode_breadcrumb_archive_record(
               {encoded.data(), encoded.size()}, output).error ==
           BreadcrumbArchiveCodecError::checksum_mismatch);
}

void test_explicit_start_stop_and_session_reuse() {
    FakeBreadcrumbArchiveTransport transport{};
    BreadcrumbArchiveSession archive{transport, {1000, 100}};
    EXPECT(archive.service(current(), 0).disposition ==
           PositionBroadcastScheduleDisposition::stopped);
    EXPECT(transport.attempts() == 0);
    EXPECT(archive.start(0, 0).error ==
           BreadcrumbArchiveSessionError::invalid_session);
    EXPECT(archive.start(10, 0).started());
    EXPECT(archive.start(11, 0).error ==
           BreadcrumbArchiveSessionError::already_active);
    archive.stop();
    EXPECT(archive.service(current(), 1).disposition ==
           PositionBroadcastScheduleDisposition::stopped);
    EXPECT(archive.start(10, 2).error ==
           BreadcrumbArchiveSessionError::session_reused);
    EXPECT(archive.start(11, 2).started());
    archive.stop();
    EXPECT(archive.start(9, 3).error ==
           BreadcrumbArchiveSessionError::session_reused);
    const auto status = archive.status();
    EXPECT(!status.active && status.has_prior_session);
    EXPECT(status.sessions_started == 2 && status.sessions_stopped == 2);
}

void test_current_fix_submits_minimized_record() {
    FakeBreadcrumbArchiveTransport transport{};
    BreadcrumbArchiveSession archive{transport, {1000, 100}};
    EXPECT(archive.start(50, 100).started());
    const auto submitted = archive.service(current(), 100);
    EXPECT(submitted.submitted());
    EXPECT(transport.size() == 1);
    EXPECT(transport.submitted_at(0) == 100);
    const auto decoded = decoded_at(transport, 0);
    EXPECT(decoded.session_id == 50);
    EXPECT(decoded.sequence == 1);
    EXPECT(decoded.captured_at_ms == 100);
    const auto position = decode_position(
        {decoded.position_payload.data(), decoded.position_payload.size()});
    EXPECT(position.decoded());
    EXPECT(position.position.latitude_e7 == 449775000);
    EXPECT(archive.status().next_sequence == 2);
    EXPECT(archive.status().records_submitted == 1);
}

void test_cadence_coalesces_without_backlog() {
    FakeBreadcrumbArchiveTransport transport{};
    BreadcrumbArchiveSession archive{transport, {1000, 100}};
    EXPECT(archive.start(60, 0).started());
    EXPECT(archive.service(current(1), 0).submitted());
    EXPECT(archive.service(current(2), 999).disposition ==
           PositionBroadcastScheduleDisposition::not_due);
    EXPECT(archive.service(current(3), 1000).submitted());
    EXPECT(archive.service(current(4), 5000).submitted());
    EXPECT(transport.size() == 3);
    EXPECT(decoded_at(transport, 0).sequence == 1);
    EXPECT(decoded_at(transport, 1).sequence == 2);
    EXPECT(decoded_at(transport, 2).sequence == 3);
    EXPECT(decoded_at(transport, 2).captured_at_ms == 5000);
}

void test_no_fix_never_reaches_archive_transport() {
    FakeBreadcrumbArchiveTransport transport{};
    BreadcrumbArchiveSession archive{transport, {1000, 100}};
    EXPECT(archive.start(70, 0).started());
    LocationSnapshot unavailable{};
    EXPECT(archive.service(unavailable, 0).error ==
           PositionBroadcastScheduleError::no_current_fix);
    auto invalid = current();
    invalid.error = FixError::no_fix;
    EXPECT(archive.service(invalid, 100).error ==
           PositionBroadcastScheduleError::encode_failed);
    EXPECT(transport.attempts() == 0);
    EXPECT(archive.status().records_submitted == 0);
}

void test_backpressure_retries_same_sequence_without_false_success() {
    FakeBreadcrumbArchiveTransport transport{};
    EXPECT(transport.enqueue_result(
        BreadcrumbArchiveTransportError::not_ready));
    EXPECT(transport.enqueue_result(BreadcrumbArchiveTransportError::full));
    BreadcrumbArchiveSession archive{transport, {1000, 100}};
    EXPECT(archive.start(80, 0).started());
    EXPECT(archive.service(current(), 0).error ==
           PositionBroadcastScheduleError::sink_not_ready);
    EXPECT(archive.status().next_sequence == 1);
    EXPECT(archive.service(current(), 100).error ==
           PositionBroadcastScheduleError::sink_full);
    EXPECT(archive.status().next_sequence == 1);
    EXPECT(archive.service(current(), 200).submitted());
    EXPECT(decoded_at(transport, 0).sequence == 1);
    EXPECT(archive.status().next_sequence == 2);
    EXPECT(archive.status().scheduler.backpressured == 2);
}

void test_transport_failure_is_visible_but_base_is_external() {
    FakeBreadcrumbArchiveTransport transport{};
    EXPECT(transport.enqueue_result(BreadcrumbArchiveTransportError::failed));
    BreadcrumbArchiveSession archive{transport, {1000, 100}};
    EXPECT(archive.start(90, 0).started());
    EXPECT(archive.service(current(), 0).error ==
           PositionBroadcastScheduleError::sink_failed);
    EXPECT(archive.status().last_record_error ==
           BreadcrumbArchiveRecordError::transport_failed);
    EXPECT(archive.status().active);
    archive.stop();
    EXPECT(!archive.status().active);
}

void test_clock_failure_latches_archive_off() {
    FakeBreadcrumbArchiveTransport transport{};
    BreadcrumbArchiveSession archive{transport, {1000, 100}};
    EXPECT(archive.start(100, 1000).started());
    EXPECT(archive.service(current(), 1000).submitted());
    EXPECT(archive.service(current(), 999).error ==
           PositionBroadcastScheduleError::clock_regression);
    EXPECT(!archive.status().active);
    EXPECT(archive.status().sessions_stopped == 1);
    EXPECT(archive.start(101, 1001).error ==
           BreadcrumbArchiveSessionError::scheduler_rejected);
    EXPECT(archive.status().scheduler.clock_failed);
}

void test_invalid_policy_and_time_horizon_never_submit() {
    FakeBreadcrumbArchiveTransport invalid_transport{};
    BreadcrumbArchiveSession invalid{invalid_transport, {0, 100}};
    const auto rejected = invalid.start(110, 0);
    EXPECT(rejected.error ==
           BreadcrumbArchiveSessionError::scheduler_rejected);
    EXPECT(rejected.scheduler_error ==
           PositionBroadcastScheduleError::invalid_policy);
    EXPECT(invalid_transport.attempts() == 0);

    FakeBreadcrumbArchiveTransport transport{};
    BreadcrumbArchiveSession archive{transport, {1000, 100}};
    const auto near_maximum =
        std::numeric_limits<std::uint64_t>::max() - 500U;
    EXPECT(archive.start(120, near_maximum).started());
    EXPECT(archive.service(current(), near_maximum).submitted());
    EXPECT(!archive.status().active);
    EXPECT(archive.status().scheduler.time_exhausted);
    EXPECT(archive.status().sessions_stopped == 1);
}

}  // namespace

int main() {
    test_codec_exact_shape_and_round_trip();
    test_codec_rejects_invalid_input_atomically();
    test_explicit_start_stop_and_session_reuse();
    test_current_fix_submits_minimized_record();
    test_cadence_coalesces_without_backlog();
    test_no_fix_never_reaches_archive_transport();
    test_backpressure_retries_same_sequence_without_false_success();
    test_transport_failure_is_visible_but_base_is_external();
    test_clock_failure_latches_archive_off();
    test_invalid_policy_and_time_horizon_never_submit();

    if (failures != 0) {
        std::cerr << failures << " breadcrumb archive assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 breadcrumb archive scenario groups\n";
    return EXIT_SUCCESS;
}
