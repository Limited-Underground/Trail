#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

#include "opentrail/ring_log_sink.hpp"
#include "opentrail/update_recovery_diagnostics.hpp"

namespace {

using namespace opentrail::diagnostics;
using namespace opentrail::update;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

std::string_view component(const RingLogEntry& entry) {
    return {entry.record.component.data(), entry.record.component_bytes};
}

std::string_view message(const RingLogEntry& entry) {
    return {entry.record.message.data(), entry.record.message_bytes};
}

UpdateRecoveryStatus baseline_status() {
    UpdateRecoveryStatus status{};
    status.operation = UpdateRecoveryStatusOperation::boot;
    status.state = UpdateRecoveryOperatorState::operational;
    status.reason = UpdateRecoveryOperatorReason::clean_baseline;
    status.action = UpdateRecoveryOperatorAction::continue_operation;
    status.operation_succeeded = true;
    status.normal_operation_blocked = false;
    status.attention_required = false;
    return status;
}

std::uint8_t hex_nibble(char character) {
    if (character >= '0' && character <= '9') {
        return static_cast<std::uint8_t>(character - '0');
    }
    return static_cast<std::uint8_t>(character - 'A' + 10);
}

std::uint32_t diagnostic_word(std::string_view value) {
    std::uint32_t word = 0;
    for (std::size_t index = 6; index < value.size(); ++index) {
        word = static_cast<std::uint32_t>(
            (word << 4U) | hex_nibble(value[index]));
    }
    return word;
}

void test_empty_snapshot_and_initial_status() {
    RingLogSink sink{};
    const auto result = sink.snapshot(nullptr, 0);
    const auto status = sink.status();
    EXPECT(result.succeeded());
    EXPECT(result.entry_count == 0);
    EXPECT(result.oldest_sequence == 0);
    EXPECT(result.newest_sequence == 0);
    EXPECT(status.retained == 0);
    EXPECT(status.total_written == 0);
    EXPECT(status.last_sequence == 0);
    EXPECT(!status.sequence_exhausted);
}

void test_logger_writes_sequence_and_snapshot_oldest_first() {
    RingLogSink sink{};
    Logger<LogLevel::trace> logger{sink};
    EXPECT(logger.log<LogLevel::info>(10, "radio", "ready"));
    EXPECT(logger.log<LogLevel::warn>(11, "power", "low"));
    EXPECT(logger.log<LogLevel::error>(12, "storage", "failed"));

    std::array<RingLogEntry, kRingLogCapacity> snapshot{};
    const auto result = sink.snapshot(snapshot.data(), snapshot.size());
    EXPECT(result.succeeded());
    EXPECT(result.entry_count == 3);
    EXPECT(result.oldest_sequence == 1);
    EXPECT(result.newest_sequence == 3);
    EXPECT(snapshot[0].sequence == 1);
    EXPECT(snapshot[1].sequence == 2);
    EXPECT(snapshot[2].sequence == 3);
    EXPECT(component(snapshot[0]) == "radio");
    EXPECT(message(snapshot[0]) == "ready");
    EXPECT(snapshot[2].record.timestamp_ms == 12);
}

void test_full_ring_overwrites_exactly_the_oldest() {
    RingLogSink sink{};
    Logger<LogLevel::info> logger{sink};
    for (std::size_t index = 0; index < kRingLogCapacity + 5U; ++index) {
        EXPECT(logger.log<LogLevel::info>(
            index, "sequence", std::to_string(index)));
    }

    std::array<RingLogEntry, kRingLogCapacity> snapshot{};
    const auto result = sink.snapshot(snapshot.data(), snapshot.size());
    const auto status = sink.status();
    EXPECT(result.succeeded());
    EXPECT(result.entry_count == kRingLogCapacity);
    EXPECT(result.oldest_sequence == 6);
    EXPECT(result.newest_sequence == kRingLogCapacity + 5U);
    EXPECT(message(snapshot.front()) == "5");
    EXPECT(message(snapshot.back()) ==
           std::to_string(kRingLogCapacity + 4U));
    EXPECT(status.overwritten == 5);
    EXPECT(status.total_written == kRingLogCapacity + 5U);
    EXPECT(logger.status().sink_dropped == 0);
}

void test_snapshot_failures_leave_caller_storage_unchanged() {
    RingLogSink sink{};
    Logger<LogLevel::info> logger{sink};
    EXPECT(logger.log<LogLevel::info>(1, "one", "first"));
    EXPECT(logger.log<LogLevel::info>(2, "two", "second"));

    std::array<RingLogEntry, 2> output{};
    output[0].sequence = 77;
    output[1].sequence = 88;
    const auto null_result = sink.snapshot(nullptr, output.size());
    EXPECT(null_result.error == RingLogSnapshotError::invalid_argument);
    const auto short_result = sink.snapshot(output.data(), 1);
    EXPECT(short_result.error ==
           RingLogSnapshotError::insufficient_capacity);
    EXPECT(output[0].sequence == 77);
    EXPECT(output[1].sequence == 88);
    const auto exact_result = sink.snapshot(output.data(), output.size());
    EXPECT(exact_result.succeeded());
    EXPECT(output[0].sequence == 1);
    EXPECT(output[1].sequence == 2);
}

void test_clear_erases_records_but_preserves_boot_sequence() {
    RingLogSink sink{};
    Logger<LogLevel::info> logger{sink};
    EXPECT(logger.log<LogLevel::info>(1, "one", "first"));
    EXPECT(logger.log<LogLevel::info>(2, "two", "second"));
    sink.clear();
    auto status = sink.status();
    EXPECT(status.retained == 0);
    EXPECT(status.total_written == 2);
    EXPECT(status.last_sequence == 2);
    EXPECT(status.clears == 1);
    EXPECT(sink.snapshot(nullptr, 0).succeeded());

    EXPECT(logger.log<LogLevel::info>(3, "three", "third"));
    std::array<RingLogEntry, 1> output{};
    EXPECT(sink.snapshot(output.data(), output.size()).succeeded());
    EXPECT(output[0].sequence == 3);
    EXPECT(message(output[0]) == "third");
}

void test_malformed_direct_records_are_rejected() {
    RingLogSink sink{};
    const auto valid = detail::make_log_record(
        1, LogLevel::info, "component", "message",
        LogPrivacy::public_data);

    auto malformed = valid;
    malformed.level = LogLevel::off;
    EXPECT(!sink.write(malformed));
    malformed = valid;
    malformed.level = static_cast<LogLevel>(99);
    EXPECT(!sink.write(malformed));
    malformed = valid;
    malformed.component_bytes = 0;
    EXPECT(!sink.write(malformed));
    malformed = valid;
    malformed.component_bytes = malformed.component.size();
    EXPECT(!sink.write(malformed));
    malformed = valid;
    malformed.message_bytes = malformed.message.size();
    EXPECT(!sink.write(malformed));
    malformed = valid;
    malformed.component[malformed.component_bytes] = 'x';
    EXPECT(!sink.write(malformed));
    malformed = valid;
    malformed.message[0] = '\n';
    EXPECT(!sink.write(malformed));
    malformed = valid;
    malformed.redacted = true;
    EXPECT(!sink.write(malformed));

    const auto status = sink.status();
    EXPECT(status.rejected == 8);
    EXPECT(status.retained == 0);
    EXPECT(status.total_written == 0);
    EXPECT(status.last_sequence == 0);
}

void test_sensitive_logger_records_remain_canonical_and_redacted() {
    RingLogSink sink{};
    Logger<LogLevel::debug> logger{sink};
    const std::string secret = "private-channel-material";
    EXPECT(logger.log<LogLevel::debug>(
        1, "security", secret, LogPrivacy::sensitive));
    std::array<RingLogEntry, 1> output{};
    EXPECT(sink.snapshot(output.data(), output.size()).succeeded());
    EXPECT(output[0].record.redacted);
    EXPECT(message(output[0]) == "[REDACTED]");
    EXPECT(std::string(message(output[0])).find(secret) == std::string::npos);
}

void test_update_recovery_event_uses_the_production_ring() {
    RingLogSink sink{};
    Logger<LogLevel::trace> logger{sink};
    const auto baseline =
        record_update_recovery_status(logger, baseline_status(), 100);
    const auto service =
        record_update_recovery_status(logger, UpdateRecoveryStatus{}, 200);
    EXPECT(baseline.accepted() && baseline.stored);
    EXPECT(service.accepted() && service.stored);

    std::array<RingLogEntry, 2> output{};
    const auto result = sink.snapshot(output.data(), output.size());
    EXPECT(result.succeeded());
    EXPECT(component(output[0]) == "update-recovery");
    EXPECT(message(output[0]) == "OTRD0=D0105084");
    EXPECT(output[0].record.level == LogLevel::info);
    EXPECT(output[1].record.level == LogLevel::error);
    const auto decoded =
        decode_update_recovery_diagnostic(diagnostic_word(message(output[1])));
    EXPECT(decoded.decoded());
    EXPECT(decoded.diagnostic.state ==
           UpdateRecoveryOperatorState::service_required);
    EXPECT(decoded.diagnostic.sensitive_detail_redacted);
}

static_assert(std::is_trivially_copyable_v<RingLogEntry>);
static_assert(kRingLogCapacity == 32);
static_assert(sizeof(RingLogSink) <= 6144);

}  // namespace

int main() {
    test_empty_snapshot_and_initial_status();
    test_logger_writes_sequence_and_snapshot_oldest_first();
    test_full_ring_overwrites_exactly_the_oldest();
    test_snapshot_failures_leave_caller_storage_unchanged();
    test_clear_erases_records_but_preserves_boot_sequence();
    test_malformed_direct_records_are_rejected();
    test_sensitive_logger_records_remain_canonical_and_redacted();
    test_update_recovery_event_uses_the_production_ring();

    if (failures != 0) {
        std::cerr << failures << " ring log sink assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 bounded ring log sink scenarios\n";
    return EXIT_SUCCESS;
}
