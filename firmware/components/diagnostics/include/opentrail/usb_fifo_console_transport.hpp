#pragma once

#include "opentrail/bounded_console_writer.hpp"

namespace opentrail::diagnostics {

// Nonblocking ESP32-S3 USB Serial/JTAG register operations. write_one reports
// exactly 0 or 1 accepted bytes. No driver installation or RX ownership change.
struct UsbSerialJtagFifoOps {
    void* context;
    bool (*connected)(void*) noexcept;
    bool (*writable)(void*) noexcept;
    int (*write_one)(void*, std::uint8_t) noexcept;
    void (*flush)(void*) noexcept;
    std::uint64_t (*monotonic_us)(void*) noexcept;
    void (*pause)(void*) noexcept;
};

class UsbSerialJtagConsoleAdapter {
public:
    explicit UsbSerialJtagConsoleAdapter(UsbSerialJtagFifoOps ops) noexcept : ops_(ops) {}

    // This is admission, not discovery or proof. Caller must establish exclusive
    // normal-runtime TX ownership, including stdio and direct ROM writers,
    // BEFORE calling. printf channel hooks alone do not establish it. Panic and
    // reset invalidate the candidate session; they are not exclusive-writer
    // guarantees provided by this adapter.
    // Ownership cannot be readmitted after any fault or reused across reset.
    bool admit_exclusive_ownership() noexcept {
        if (!ops_.connected || !ops_.writable || !ops_.write_one || !ops_.flush
            || !ops_.monotonic_us || !ops_.pause) return false;
        State expected = State::unowned;
        return state_.compare_exchange_strong(expected, State::owned);
    }

    // Safe concurrent invalidation, but target ISR/cache-off callers must also
    // prove their actual compiled call path is IRAM-safe before using this API.
    void invalidate() noexcept { state_.store(State::fault); }

    ConsoleTransport transport() noexcept {
        return {this,
            [](void* c) noexcept { return !self(c).lock_.test_and_set(std::memory_order_acquire); },
            [](void* c) noexcept { self(c).lock_.clear(std::memory_order_release); },
            [](void* c, std::uint8_t byte) noexcept { return self(c).write_one(byte); },
            [](void* c) noexcept { return self(c).finish_packet(); },
            [](void* c) noexcept {
                auto& a = self(c); return a.ops_.monotonic_us ? a.ops_.monotonic_us(a.ops_.context) : 0;
            },
            [](void* c) noexcept { auto& a = self(c); if (a.ops_.pause) a.ops_.pause(a.ops_.context); }};
    }

private:
    enum class State { unowned, owned, fault };
    enum class Drain { first_flush, terminal_flush, terminal_wait, complete };
    static_assert(std::atomic<State>::is_always_lock_free, "Console ownership requires lock-free atomics");

    static UsbSerialJtagConsoleAdapter& self(void* context) noexcept {
        return *static_cast<UsbSerialJtagConsoleAdapter*>(context);
    }
    bool ready() noexcept {
        if (state_.load() != State::owned) return false;
        if (!ops_.connected(ops_.context)) { invalidate(); return false; }
        return true;
    }

    // One state transition per call. The first flush may send old/partial FIFO
    // data. After writable returns, the second flush ends a preceding 64-byte
    // transaction with a ZLP. Waiting again preserves an empty starting FIFO.
    // No flush is issued while an automatic full-packet flush is busy.
    int drain_step() noexcept {
        if (!ready()) return -1;
        if (drain_ == Drain::complete) return 1;
        if (!ops_.writable(ops_.context)) return 0;
        if (!ready()) return -1;
        if (drain_ == Drain::terminal_wait) {
            drain_ = Drain::complete;
            return 1;
        }
        ops_.flush(ops_.context);
        drain_ = drain_ == Drain::first_flush ? Drain::terminal_flush : Drain::terminal_wait;
        return 0;
    }

    int write_one(std::uint8_t byte) noexcept {
        if (!ready()) return -1;
        if (!initialized_) {
            const int result = drain_step();
            if (result != 1) return result;
            initialized_ = true;
        }
        if (!ops_.writable(ops_.context)) return 0;
        const int result = ops_.write_one(ops_.context, byte);
        if (result == 1) {
            drain_ = Drain::first_flush;
        } else if (result != 0) {
            invalidate();
            return -1;
        }
        return result;
    }

    int finish_packet() noexcept { return drain_step(); }

    UsbSerialJtagFifoOps ops_;
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::atomic<State> state_{State::unowned};
    Drain drain_{Drain::first_flush};
    bool initialized_{false};
};

} // namespace opentrail::diagnostics
