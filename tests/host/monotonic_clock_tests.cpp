#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "fake_monotonic_counter_source.hpp"
#include "opentrail/monotonic_clock.hpp"

namespace {

using opentrail::time::CheckedMonotonicClock;
using opentrail::time::MonotonicClockError;
using opentrail::time::RawClockReadError;
using opentrail::time::test_support::FakeMonotonicCounterSource;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

void test_default_not_ready_then_zero_origin() {
    FakeMonotonicCounterSource source;
    CheckedMonotonicClock clock(source);

    const auto unavailable = clock.now();
    EXPECT(unavailable.error == MonotonicClockError::not_ready);
    EXPECT(unavailable.value_ms == 0);
    EXPECT(!clock.status().initialized);
    EXPECT(!clock.status().fault_latched);

    EXPECT(source.enqueue_time(0));
    const auto origin = clock.now();
    EXPECT(origin.ok());
    EXPECT(origin.value_ms == 0);
    EXPECT(clock.status().initialized);
    EXPECT(clock.status().successful_reads == 1);
}

void test_equal_millisecond_reads_are_valid() {
    FakeMonotonicCounterSource source;
    EXPECT(source.enqueue_time(42));
    EXPECT(source.enqueue_time(42));
    CheckedMonotonicClock clock(source);

    EXPECT(clock.now().ok());
    const auto same_tick = clock.now();
    EXPECT(same_tick.ok());
    EXPECT(same_tick.value_ms == 42);
    EXPECT(clock.status().last_value_ms == 42);
    EXPECT(clock.status().successful_reads == 2);
}

void test_forward_progress_and_maximum_value() {
    FakeMonotonicCounterSource source;
    EXPECT(source.enqueue_time(10));
    EXPECT(source.enqueue_time(25));
    EXPECT(source.enqueue_time(std::numeric_limits<std::uint64_t>::max()));
    CheckedMonotonicClock clock(source);

    EXPECT(clock.now().value_ms == 10);
    EXPECT(clock.now().value_ms == 25);
    const auto maximum = clock.now();
    EXPECT(maximum.ok());
    EXPECT(maximum.value_ms == std::numeric_limits<std::uint64_t>::max());
    const auto status = clock.status();
    EXPECT(status.read_attempts == 3);
    EXPECT(status.successful_reads == 3);
    EXPECT(status.last_value_ms == std::numeric_limits<std::uint64_t>::max());
}

void test_not_ready_does_not_destroy_continuity() {
    FakeMonotonicCounterSource source;
    EXPECT(source.enqueue_time(100));
    EXPECT(source.enqueue_not_ready());
    EXPECT(source.enqueue_time(101));
    CheckedMonotonicClock clock(source);

    EXPECT(clock.now().value_ms == 100);
    EXPECT(clock.now().error == MonotonicClockError::not_ready);
    const auto recovered = clock.now();
    EXPECT(recovered.ok());
    EXPECT(recovered.value_ms == 101);
    const auto status = clock.status();
    EXPECT(!status.fault_latched);
    EXPECT(status.not_ready_reads == 1);
    EXPECT(status.successful_reads == 2);
}

void test_rollback_latches_without_consuming_later_samples() {
    FakeMonotonicCounterSource source;
    EXPECT(source.enqueue_time(100));
    EXPECT(source.enqueue_time(99));
    EXPECT(source.enqueue_time(101));
    CheckedMonotonicClock clock(source);

    EXPECT(clock.now().ok());
    const auto rollback = clock.now();
    EXPECT(rollback.error == MonotonicClockError::rollback_detected);
    EXPECT(rollback.value_ms == 0);
    const auto refused = clock.now();
    EXPECT(refused.error == MonotonicClockError::fault_latched);
    EXPECT(source.read_count() == 2);
    EXPECT(source.queued_count() == 1);

    const auto status = clock.status();
    EXPECT(status.fault_latched);
    EXPECT(status.latched_error == MonotonicClockError::rollback_detected);
    EXPECT(status.rollback_failures == 1);
    EXPECT(status.latched_refusals == 1);
    EXPECT(status.last_value_ms == 100);
}

void test_source_failure_latches_closed() {
    FakeMonotonicCounterSource source;
    EXPECT(source.enqueue_failure());
    EXPECT(source.enqueue_time(200));
    CheckedMonotonicClock clock(source);

    const auto failure = clock.now();
    EXPECT(failure.error == MonotonicClockError::source_failed);
    EXPECT(failure.value_ms == 0);
    EXPECT(clock.now().error == MonotonicClockError::fault_latched);
    EXPECT(source.read_count() == 1);
    EXPECT(source.queued_count() == 1);

    const auto status = clock.status();
    EXPECT(status.fault_latched);
    EXPECT(status.latched_error == MonotonicClockError::source_failed);
    EXPECT(status.source_failures == 1);
    EXPECT(status.latched_refusals == 1);
}

void test_new_boot_composition_can_read_after_latched_fault() {
    FakeMonotonicCounterSource source;
    EXPECT(source.enqueue_time(20));
    EXPECT(source.enqueue_time(19));
    EXPECT(source.enqueue_time(0));
    CheckedMonotonicClock first_boot(source);

    EXPECT(first_boot.now().ok());
    EXPECT(first_boot.now().error == MonotonicClockError::rollback_detected);
    CheckedMonotonicClock next_boot(source);
    const auto new_origin = next_boot.now();
    EXPECT(new_origin.ok());
    EXPECT(new_origin.value_ms == 0);
    EXPECT(next_boot.status().last_value_ms == 0);
    EXPECT(!next_boot.status().fault_latched);
}

void test_fake_source_is_bounded_fifo() {
    FakeMonotonicCounterSource source;
    for (std::size_t index = 0; index < source.kCapacity; ++index) {
        EXPECT(source.enqueue_time(index));
    }
    EXPECT(!source.enqueue_time(999));
    EXPECT(source.queued_count() == source.kCapacity);

    for (std::size_t index = 0; index < source.kCapacity; ++index) {
        const auto sample = source.read();
        EXPECT(sample.error == RawClockReadError::none);
        EXPECT(sample.value_ms == index);
    }
    const auto empty = source.read();
    EXPECT(empty.error == RawClockReadError::not_ready);
    EXPECT(empty.value_ms == 0);
    EXPECT(source.queued_count() == 0);
    EXPECT(source.read_count() == source.kCapacity + 1);
}

}  // namespace

int main() {
    test_default_not_ready_then_zero_origin();
    test_equal_millisecond_reads_are_valid();
    test_forward_progress_and_maximum_value();
    test_not_ready_does_not_destroy_continuity();
    test_rollback_latches_without_consuming_later_samples();
    test_source_failure_latches_closed();
    test_new_boot_composition_can_read_after_latched_fault();
    test_fake_source_is_bounded_fifo();

    if (failures != 0) {
        std::cerr << failures << " monotonic-clock assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 monotonic-clock boundary scenarios\n";
    return EXIT_SUCCESS;
}
