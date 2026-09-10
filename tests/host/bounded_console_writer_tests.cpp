#include "opentrail/bounded_console_writer.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace opentrail::diagnostics;

static void check(bool value) {
    if (!value) { std::cerr << "bounded console writer check failed\n"; std::abort(); }
}

struct Fake {
    std::uint64_t now = 0, pause_us = 1, lock_us = 0, write_us = 0, unlock_us = 0, flush_us = 0;
    unsigned lock_busy = 0, locks = 0, unlocks = 0, writes = 0, flushes = 0;
    bool locked = false, stuck_write = false, stuck_flush = false, regress = false;
    bool reenter = false;
    int flush_result = 1;
    std::vector<int> outcomes;
    std::string fifo;
    BoundedConsoleWriter* writer = nullptr;

    ConsoleTransport transport() noexcept {
        return {this,
            [](void* context) noexcept {
                auto& f = *static_cast<Fake*>(context); ++f.locks; f.now += f.lock_us;
                if (f.lock_busy) { --f.lock_busy; return false; }
                check(!f.locked); f.locked = true; return true;
            },
            [](void* context) noexcept {
                auto& f = *static_cast<Fake*>(context); check(f.locked);
                f.locked = false; ++f.unlocks; f.now += f.unlock_us;
            },
            [](void* context, std::uint8_t byte) noexcept {
                auto& f = *static_cast<Fake*>(context); check(f.locked);
                const auto index = f.writes++; f.now += f.write_us;
                if (f.regress) f.now = 0;
                if (f.reenter) {
                    f.reenter = false;
                    const auto nested = f.writer->write(&byte, 1, 100);
                    check(nested.status == ConsoleWriteStatus::concurrent_access);
                }
                const int result = index < f.outcomes.size() ? f.outcomes[index] : (f.stuck_write ? 0 : 1);
                if (result == 1) f.fifo.push_back(static_cast<char>(byte));
                return result;
            },
            [](void* context) noexcept {
                auto& f = *static_cast<Fake*>(context); check(f.locked); ++f.flushes; f.now += f.flush_us;
                return f.stuck_flush ? 0 : f.flush_result;
            },
            [](void* context) noexcept { return static_cast<Fake*>(context)->now; },
            [](void* context) noexcept {
                auto& f = *static_cast<Fake*>(context); f.now += f.pause_us;
            }};
    }
};

static ConsoleWriteResult write(BoundedConsoleWriter& writer, const char* text, std::uint64_t budget = 100) {
    return writer.write(reinterpret_cast<const std::uint8_t*>(text), std::string(text).size(), budget);
}

int main() {
    unsigned cases = 0;
    { // Successful FIFO acceptance and terminal handoff, including CRLF.
        Fake f; BoundedConsoleWriter w(f.transport());
        const auto r = write(w, "A\nB");
        check(r.status == ConsoleWriteStatus::accepted && r.logical_bytes == 3 && r.fifo_bytes == 4);
        check(f.fifo == "A\r\nB" && f.flushes == 1 && f.unlocks == 1); ++cases;
    }
    { // Only explicitly zero-accepted bytes are retried; no duplicate accepted A.
        Fake f; f.lock_busy = 2; f.outcomes = {0, 0, 1, 0, 1}; BoundedConsoleWriter w(f.transport());
        auto r = write(w, "AB");
        check(r.status == ConsoleWriteStatus::accepted && f.fifo == "AB" && f.now == 5); ++cases;
    }
    { // Lock waiting consumes the same deadline, with no writes or unlock of an unheld lock.
        Fake f; f.lock_busy = 100; BoundedConsoleWriter w(f.transport());
        auto r = write(w, "A", 3);
        check(r.status == ConsoleWriteStatus::timeout && f.locks == 3 && f.writes == 0 && f.unlocks == 0); ++cases;
    }
    { // Lock acquired at the boundary must be released without starting a byte.
        Fake f; f.lock_us = 3; BoundedConsoleWriter w(f.transport()); auto r = write(w, "A", 3);
        check(r.status == ConsoleWriteStatus::timeout && f.writes == 0 && f.unlocks == 1); ++cases;
    }
    { // A byte known accepted at the boundary remains truthfully counted, but the write fails.
        Fake f; f.write_us = 3; BoundedConsoleWriter w(f.transport()); auto r = write(w, "AB", 3);
        check(r.status == ConsoleWriteStatus::timeout && r.logical_bytes == 1 && r.fifo_bytes == 1);
        check(f.fifo == "A" && f.flushes == 0); ++cases;
    }
    { // Partial CRLF: the CR is FIFO-accepted, but the LF logical byte is incomplete.
        Fake f; f.outcomes = {1, 1}; f.stuck_write = true; BoundedConsoleWriter w(f.transport());
        auto r = write(w, "A\n", 3);
        check(r.status == ConsoleWriteStatus::timeout && r.logical_bytes == 1 && r.fifo_bytes == 2);
        check(f.fifo == "A\r"); const auto calls = f.writes;
        check(write(w, "X").status == ConsoleWriteStatus::timeout && f.writes == calls); ++cases;
    }
    { // Invalid/indeterminate acceptance is never retried and faults the lifecycle.
        Fake f; f.outcomes = {1, -1}; BoundedConsoleWriter w(f.transport()); auto r = write(w, "ABC");
        check(r.status == ConsoleWriteStatus::transport_fault && r.logical_bytes == 1 && f.writes == 2); ++cases;
    }
    { // All logical bytes accepted does not hide terminal handoff failure.
        Fake f; f.stuck_flush = true; BoundedConsoleWriter w(f.transport()); auto r = write(w, "A", 3);
        check(r.status == ConsoleWriteStatus::timeout && r.logical_bytes == 1 && r.fifo_bytes == 1);
        const auto calls = f.flushes; check(write(w, "B").status == ConsoleWriteStatus::timeout && f.flushes == calls); ++cases;
    }
    {
        Fake f; f.flush_result = 2; BoundedConsoleWriter w(f.transport()); auto r = write(w, "A");
        check(r.status == ConsoleWriteStatus::transport_fault && f.flushes == 1 && f.unlocks == 1); ++cases;
    }
    { // A nonadvancing clock cannot make a busy transport loop forever.
        Fake f; f.pause_us = 0; f.stuck_write = true; BoundedConsoleWriter w(f.transport());
        auto r = write(w, "A"); check(r.status == ConsoleWriteStatus::attempt_limit);
        check(f.locks + f.writes == BoundedConsoleWriter::max_attempts && f.unlocks == 1); ++cases;
    }
    {
        Fake f; f.now = 9; f.regress = true; BoundedConsoleWriter w(f.transport()); auto r = write(w, "A");
        check(r.status == ConsoleWriteStatus::clock_regressed && r.logical_bytes == 1 && f.unlocks == 1); ++cases;
    }
    { // Reentrant invocation cannot acquire another lock or let the outer call succeed.
        Fake f; BoundedConsoleWriter w(f.transport()); f.writer = &w; f.reenter = true;
        auto r = write(w, "AB"); check(r.status == ConsoleWriteStatus::concurrent_access);
        check(f.writes == 1 && f.locks == 1 && f.unlocks == 1); ++cases;
    }
    { // Unlock is part of the total elapsed budget too.
        Fake f; f.unlock_us = 5; BoundedConsoleWriter w(f.transport()); auto r = write(w, "A", 5);
        check(r.status == ConsoleWriteStatus::timeout && f.unlocks == 1); ++cases;
    }
    {
        Fake f; BoundedConsoleWriter w(f.transport()); auto r = w.write(nullptr, 0, 10);
        check(r.status == ConsoleWriteStatus::accepted && r.logical_bytes == 0 && f.flushes == 0); ++cases;
    }
    {
        Fake f; BoundedConsoleWriter w(f.transport()); auto r = w.write(nullptr, 1, 10);
        check(r.status == ConsoleWriteStatus::invalid_input && f.locks == 0); ++cases;
    }
    {
        Fake f; f.now = std::numeric_limits<std::uint64_t>::max() - 1;
        BoundedConsoleWriter w(f.transport()); auto r = write(w, "A", 2);
        check(r.status == ConsoleWriteStatus::invalid_input && f.locks == 0); ++cases;
    }
    {
        Fake f; BoundedConsoleWriter w(f.transport()); std::array<std::uint8_t, 1025> bytes{};
        auto r = w.write(bytes.data(), bytes.size(), 100);
        check(r.status == ConsoleWriteStatus::invalid_input && f.writes == 0); ++cases;
    }
    {
        Fake f; auto transport = f.transport(); transport.try_flush = nullptr;
        BoundedConsoleWriter w(transport); check(write(w, "A").status == ConsoleWriteStatus::invalid_input); ++cases;
    }
    { // Backend success after the deadline remains a failure with truthful counts.
        Fake f; f.flush_us = 6; BoundedConsoleWriter w(f.transport()); auto r = write(w, "A", 5);
        check(r.status == ConsoleWriteStatus::timeout && r.logical_bytes == 1 && r.fifo_bytes == 1);
        check(f.flushes == 1 && f.unlocks == 1); ++cases;
    }
    { // The attempt cap also bounds lock admission when time never advances.
        Fake f; f.pause_us = 0; f.lock_busy = 10000; BoundedConsoleWriter w(f.transport());
        auto r = write(w, "A"); check(r.status == ConsoleWriteStatus::attempt_limit);
        check(f.locks == BoundedConsoleWriter::max_attempts && f.writes == 0 && f.unlocks == 0); ++cases;
    }
    { // Full byte acceptance does not bypass the cap on a stuck terminal handoff.
        Fake f; f.pause_us = 0; f.stuck_flush = true; BoundedConsoleWriter w(f.transport());
        auto r = write(w, "A"); check(r.status == ConsoleWriteStatus::attempt_limit);
        check(r.logical_bytes == 1 && r.fifo_bytes == 1 && f.unlocks == 1);
        check(f.locks + f.writes + f.flushes == BoundedConsoleWriter::max_attempts); ++cases;
    }
    std::cout << "bounded console writer: " << cases << " cases passed\n";
}
