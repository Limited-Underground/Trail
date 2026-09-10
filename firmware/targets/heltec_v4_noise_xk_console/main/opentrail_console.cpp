#include "opentrail_console.h"
#include "opentrail/usb_fifo_console_transport.hpp"

#include <atomic>
#include <cerrno>
#include <reent.h>
#include <sys/types.h>
#include <unistd.h>
#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "hal/usb_serial_jtag_ll.h"

namespace {
// The hook can run outside task context. Its compiled body must remain in IRAM
// and use only lock-free stores to DRAM; it never logs or acquires a lock.
DRAM_ATTR std::atomic<unsigned> phase{0}; // 0 startup, 1 owned, 2 terminal fault
static_assert(std::atomic<unsigned>::is_always_lock_free);
std::atomic_flag startup_active = ATOMIC_FLAG_INIT;
void IRAM_ATTR output_bypass(char) { phase.store(2, std::memory_order_relaxed); }
bool available(void*) noexcept {
    // No host-connected assertion: an absent host is bounded by FIFO deadline.
    return phase.load(std::memory_order_relaxed) == 1;
}
bool writable(void*) noexcept { return usb_serial_jtag_ll_txfifo_writable(); }
int write_one(void*, std::uint8_t byte) noexcept {
    if (!available(nullptr)) return -1;
    return static_cast<int>(usb_serial_jtag_ll_write_txfifo(&byte, 1));
}
void flush(void*) noexcept { usb_serial_jtag_ll_txfifo_flush(); }
std::uint64_t now(void*) noexcept { return static_cast<std::uint64_t>(esp_timer_get_time()); }
void pause(void*) noexcept { asm volatile("nop"); }
using namespace opentrail::diagnostics;
UsbSerialJtagConsoleAdapter adapter({nullptr, available, writable, write_one, flush, now, pause});
BoundedConsoleWriter writer(adapter.transport());
constexpr std::uint64_t write_budget_us = 20000;
}

extern "C" int __real_esp_rom_output_tx_one_char(std::uint8_t);
extern "C" void __real_ets_write_char_uart(char);
extern "C" int __real_uart_tx_one_char(std::uint8_t);

// These wrappers cover ELF-linked references only. They cannot intercept a ROM
// internal branch. Startup remains the original output lifecycle; any later
// bypass invalidates this benchmark session and emits no competing bytes.
extern "C" int IRAM_ATTR __wrap_esp_rom_output_tx_one_char(std::uint8_t byte) {
    if (phase.load(std::memory_order_relaxed) == 0) return __real_esp_rom_output_tx_one_char(byte);
    phase.store(2, std::memory_order_relaxed); return 1;
}
extern "C" void IRAM_ATTR __wrap_ets_write_char_uart(char byte) {
    if (phase.load(std::memory_order_relaxed) == 0) { __real_ets_write_char_uart(byte); return; }
    phase.store(2, std::memory_order_relaxed);
}
extern "C" int IRAM_ATTR __wrap_uart_tx_one_char(std::uint8_t byte) {
    if (phase.load(std::memory_order_relaxed) == 0) return __real_uart_tx_one_char(byte);
    phase.store(2, std::memory_order_relaxed); return 1;
}

bool ot_console_install() noexcept {
    if (startup_active.test_and_set(std::memory_order_acquire)) return false;
    struct Release { ~Release() { startup_active.clear(std::memory_order_release); } } release;
    if (phase.load() != 0) return false;
    // Single startup task, before app logging, radio initialization or CLI task.
    // Candidate admission is conditional on the exact ELF normal-writer audit.
    esp_rom_install_channel_putc(1, output_bypass);
    esp_rom_install_channel_putc(2, output_bypass);
    if (!adapter.admit_exclusive_ownership()) { phase.store(2); return false; }
    unsigned expected = 0;
    if (!phase.compare_exchange_strong(expected, 1)) { adapter.invalidate(); return false; }
    return true;
}
bool ot_console_healthy() noexcept { return phase.load() == 1; }

extern "C" ssize_t _write_r(struct _reent* r, int fd, const void* data, size_t size) {
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO) { __errno_r(r) = EBADF; return -1; }
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    if (phase.load() == 0) {
        if (startup_active.test_and_set(std::memory_order_acquire)) { __errno_r(r) = EIO; return -1; }
        struct Release { ~Release() { startup_active.clear(std::memory_order_release); } } release;
        if (phase.load() != 0) { phase.store(2); __errno_r(r) = EIO; return -1; }
        // Preserve IDF startup diagnostics before the bounded ownership handoff.
        for (size_t i = 0; i < size; ++i) {
            if (bytes[i] == '\n') __real_esp_rom_output_tx_one_char('\r');
            __real_esp_rom_output_tx_one_char(bytes[i]);
        }
        return static_cast<ssize_t>(size);
    }
    if (!ot_console_healthy()) { __errno_r(r) = EIO; return -1; }
    const auto result = writer.write(bytes, size, write_budget_us);
    if (result.status != ConsoleWriteStatus::accepted || !ot_console_healthy()) {
        phase.store(2); adapter.invalidate(); __errno_r(r) = EIO;
        // Even a full logical count can have a failed terminal packet handoff.
        // Never report success or invite a replay of uncertain bytes.
        return -1;
    }
    return static_cast<ssize_t>(result.logical_bytes);
}
