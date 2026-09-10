#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace opentrail::diagnostics {

// Candidate FIFO submission only; target lifecycle requires separate validation.
// Every callback must be nonblocking and noexcept. The writer cannot interrupt
// a callback that violates that contract. One transport must serialize ALL of
// its writers with the supplied lock, including writers outside this object.
struct ConsoleTransport {
    void* context;
    bool (*try_lock)(void*) noexcept;
    void (*unlock)(void*) noexcept;
    // 0 means definitely not accepted and is retryable; 1 means exactly one
    // byte accepted by the FIFO. Every other value is an indeterminate fault.
    int (*try_write_byte)(void*, std::uint8_t) noexcept;
    // 0 means terminal handoff is pending; 1 means the backend's required
    // terminal packet/ZLP handoff sequence completed. A single flush-register
    // write is NOT sufficient for a backend that requires a later ZLP. Neither
    // outcome proves that the host application received the bytes.
    int (*try_flush)(void*) noexcept;
    std::uint64_t (*monotonic_us)(void*) noexcept;
    void (*pause)(void*) noexcept;
};

enum class ConsoleWriteStatus {
    accepted, invalid_input, timeout, attempt_limit, transport_fault,
    clock_regressed, concurrent_access
};
static_assert(std::atomic<ConsoleWriteStatus>::is_always_lock_free,
              "Bounded console fault state requires lock-free atomics");

struct ConsoleWriteResult {
    ConsoleWriteStatus status;
    std::size_t logical_bytes;
    std::size_t fifo_bytes;
};

class BoundedConsoleWriter {
public:
    static constexpr std::size_t max_input_bytes = 1024;
    static constexpr std::size_t max_attempts = 4096;

    explicit BoundedConsoleWriter(ConsoleTransport transport) noexcept : transport_(transport) {}
    BoundedConsoleWriter(const BoundedConsoleWriter&) = delete;
    BoundedConsoleWriter& operator=(const BoundedConsoleWriter&) = delete;

    // A fault is sticky for this object's lifetime. Replaying a partially
    // accepted write is prohibited; reconstruct only as part of an independently
    // established transport lifecycle. LF counts as one logical byte after both
    // CR and LF are accepted. All other bytes, including CR, pass unchanged.
    // A concurrent fault cannot cancel an already-admitted operation (including
    // the gap between admission and callback entry). Its known byte acceptance
    // is counted; the next observed fault stops further transfer attempts.
    ConsoleWriteResult write(const std::uint8_t* data, std::size_t size,
                             std::uint64_t budget_us) noexcept {
        if (active_.test_and_set(std::memory_order_acquire)) {
            latch(ConsoleWriteStatus::concurrent_access);
            return {fault_.load(), 0, 0};
        }
        ConsoleWriteResult result{ConsoleWriteStatus::accepted, 0, 0};
        bool locked = false;
        bool timed = false;
        std::uint64_t last = 0, deadline = 0;
        auto finish = [&]() noexcept {
            if (locked) transport_.unlock(transport_.context);
            if (timed) {
                const auto now = transport_.monotonic_us(transport_.context);
                if (now < last) latch(ConsoleWriteStatus::clock_regressed);
                if (now >= deadline) latch(ConsoleWriteStatus::timeout);
            }
            result.status = fault_.load();
            active_.clear(std::memory_order_release);
            return result;
        };
        if (fault_.load() != ConsoleWriteStatus::accepted) return finish();
        if ((!data && size) || size > max_input_bytes || budget_us == 0
            || !transport_.try_lock || !transport_.unlock || !transport_.try_write_byte
            || !transport_.try_flush || !transport_.monotonic_us || !transport_.pause) {
            latch(ConsoleWriteStatus::invalid_input);
            return finish();
        }
        last = transport_.monotonic_us(transport_.context);
        if (budget_us > std::numeric_limits<std::uint64_t>::max() - last) {
            latch(ConsoleWriteStatus::invalid_input);
            return finish();
        }
        deadline = last + budget_us;
        timed = true;
        std::size_t attempts = 0;
        auto in_time = [&]() noexcept {
            const auto now = transport_.monotonic_us(transport_.context);
            if (now < last) latch(ConsoleWriteStatus::clock_regressed);
            last = now;
            if (now >= deadline) latch(ConsoleWriteStatus::timeout);
            return fault_.load() == ConsoleWriteStatus::accepted;
        };
        auto step = [&]() noexcept {
            if (!in_time()) return false;
            if (attempts == max_attempts) {
                latch(ConsoleWriteStatus::attempt_limit);
                return false;
            }
            ++attempts;
            return true;
        };
        while (!locked) {
            if (!step()) return finish();
            locked = transport_.try_lock(transport_.context);
            if (!in_time()) return finish();
            if (!locked) transport_.pause(transport_.context);
        }
        for (std::size_t logical = 0; logical < size; ++logical) {
            const std::uint8_t byte = data[logical];
            const unsigned count = byte == '\n' ? 2U : 1U;
            for (unsigned part = 0; part < count; ++part) {
                bool accepted = false;
                while (!accepted) {
                    if (!step()) return finish();
                    const auto value = count == 2U && part == 0U ? std::uint8_t{'\r'} : byte;
                    const int outcome = transport_.try_write_byte(transport_.context, value);
                    if (outcome == 1) {
                        ++result.fifo_bytes;
                        accepted = true;
                        if (part + 1U == count) ++result.logical_bytes;
                    } else if (outcome != 0) {
                        latch(ConsoleWriteStatus::transport_fault);
                    }
                    if (!in_time()) return finish();
                    if (!accepted) transport_.pause(transport_.context);
                }
            }
        }
        // Empty writes acquire/release the lock but do not transfer or flush.
        if (size != 0) {
            while (true) {
                if (!step()) return finish();
                const int outcome = transport_.try_flush(transport_.context);
                if (outcome != 0 && outcome != 1) latch(ConsoleWriteStatus::transport_fault);
                if (!in_time()) return finish();
                if (outcome == 1) break;
                transport_.pause(transport_.context);
            }
        }
        return finish();
    }

private:
    void latch(ConsoleWriteStatus status) noexcept {
        auto expected = ConsoleWriteStatus::accepted;
        fault_.compare_exchange_strong(expected, status);
    }

    ConsoleTransport transport_;
    std::atomic_flag active_ = ATOMIC_FLAG_INIT;
    std::atomic<ConsoleWriteStatus> fault_{ConsoleWriteStatus::accepted};
};

} // namespace opentrail::diagnostics
