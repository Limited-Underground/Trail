#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#ifndef OPENTRAIL_CONNECTION_DIAGNOSTICS
#define OPENTRAIL_CONNECTION_DIAGNOSTICS 0
#endif
#ifndef OPENTRAIL_DIAGNOSTIC_SENSOR_DISPLAY
#define OPENTRAIL_DIAGNOSTIC_SENSOR_DISPLAY 1
#endif
#if !OPENTRAIL_CONNECTION_DIAGNOSTICS && !OPENTRAIL_DIAGNOSTIC_SENSOR_DISPLAY
#error Sensor display isolation requires the explicit diagnostic build.
#endif

namespace opentrail::target::heltec_v4_bench::connection_diagnostics {

enum class Kind : std::uint32_t {
    protocol_enter = 1, protocol_lock, protocol_security, protocol_exit,
    gap_enter, gap_lock, gap_exit, authorization, connect, encryption,
    disconnect, authorization_check, host_reset,
};
struct Event {
    Kind kind{};
    std::uint32_t uptime_ms{}, first{}, second{};
};

// Producers: NimBLE callbacks (including synchronous owner-task teardown).
// One try-acquire prevents nested/foreign writers; contention drops, never spins.
// Single consumer: app_main. Drop count is an unsigned modulo-2^32 counter.
// No application-level wait/retry, allocation, logging or GATT lock. The
// platform implements the atomic operations; this is not a wait-free claim.
// Full queues preserve the
// earliest unconsumed boundary and explicitly count dropped observations.
class Events final {
public:
    static constexpr std::uint32_t capacity = 64;
    void push(Event event) noexcept {
        if (producer_.test_and_set(std::memory_order_acquire)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const auto write = write_.load(std::memory_order_relaxed);
        if (write - read_.load(std::memory_order_acquire) >= capacity) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            producer_.clear(std::memory_order_release);
            return;
        }
        events_[write % capacity] = event;
        write_.store(write + 1, std::memory_order_release);
        producer_.clear(std::memory_order_release);
    }
    bool pop(Event& event) noexcept {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) return false;
        event = events_[read % capacity];
        read_.store(read + 1, std::memory_order_release);
        return true;
    }
    std::uint32_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }
private:
    std::array<Event, capacity> events_{};
    std::atomic_flag producer_ = ATOMIC_FLAG_INIT;
    std::atomic<std::uint32_t> write_{0}, read_{0}, dropped_{0};
};

#if OPENTRAIL_CONNECTION_DIAGNOSTICS
extern Events events;
// Deliberately out of line: do not add a logging frame to the BLE call stack.
void record(Kind kind, std::uint32_t first = 0, std::uint32_t second = 0) noexcept;
#else
inline void record(Kind, std::uint32_t = 0, std::uint32_t = 0) noexcept {}
#endif

}  // namespace opentrail::target::heltec_v4_bench::connection_diagnostics
