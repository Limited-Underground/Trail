#include "policy_console.hpp"
#include "opentrail/usb_fifo_console_transport.hpp"
#include "opentrail/bounded_receipt_formatter.hpp"

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <limits>
#include <reent.h>
#include <sys/types.h>
#include <unistd.h>
#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_private/startup_internal.h"
#include "hal/usb_serial_jtag_ll.h"

namespace {
// Constant-initialized, before the CORE callback and before C++ constructors.
// Only healthy normal runtime is admitted. Panic/reset end this session.
constexpr unsigned boot = 0, quarantine = 1, owned = 2, fault = 3;
DRAM_ATTR std::atomic<unsigned> phase{boot};
static_assert(std::atomic<unsigned>::is_always_lock_free);
void IRAM_ATTR output_bypass(char) { phase.store(fault, std::memory_order_relaxed); }
// Owned by the receipt_active caller. FIFO admission uses this original absolute
// deadline even if the relative writer starts later. An already-admitted MMIO
// operation cannot be cancelled; the next admission and final check fail closed.
std::uint64_t receipt_deadline = 0, receipt_last_admission = 0;
bool available(void*) noexcept {
    if (phase.load() != owned) return false;
    const auto current = static_cast<std::uint64_t>(esp_timer_get_time());
    if (current < receipt_last_admission || current >= receipt_deadline) {
        phase.store(fault); return false;
    }
    receipt_last_admission = current;
    return true;
}
bool writable(void*) noexcept {
    return available(nullptr) && usb_serial_jtag_ll_txfifo_writable();
}
int write_one(void*, std::uint8_t byte) noexcept {
    if (!available(nullptr)) return -1;
    return static_cast<int>(usb_serial_jtag_ll_write_txfifo(&byte, 1));
}
void flush(void*) noexcept {
    if (available(nullptr)) usb_serial_jtag_ll_txfifo_flush();
}
std::uint64_t now(void*) noexcept { return static_cast<std::uint64_t>(esp_timer_get_time()); }
// Fixed backoff lets asynchronous USB polls advance without exhausting the
// finite-attempt cap in a tight loop. It never waits on an external resource.
void pause(void*) noexcept {
    if (!available(nullptr)) return;
    const auto remaining = receipt_deadline - receipt_last_admission;
    esp_rom_delay_us(static_cast<unsigned>(remaining < 5 ? remaining : 5));
}
using namespace opentrail::diagnostics;
UsbSerialJtagConsoleAdapter adapter({nullptr, available, writable, write_one, flush, now, pause});
BoundedConsoleWriter writer(adapter.transport());
std::atomic_flag receipt_active = ATOMIC_FLAG_INIT;
std::atomic<bool> receipt_enabled{false};
std::atomic<bool> session_ever_started{false};
constexpr std::uint64_t receipt_budget_us = 20000;
void fail() noexcept { phase.store(fault); adapter.invalidate(); }
}

// Pinned SDK: both CPUs finished low-level ROM output; CPU1 is parked.
// CORE callbacks precede constructors and secondary initialization/core1 resume.
// No timer, adapter, formatter, heap or logging may be used from this callback.
ESP_SYSTEM_INIT_FN(ot_receipt_quarantine, CORE, BIT(0), 0, IRAM_ATTR) {
    if (phase.load(std::memory_order_relaxed) != boot) return ESP_FAIL;
    esp_rom_install_channel_putc(1, output_bypass);
    esp_rom_install_channel_putc(2, output_bypass);
    unsigned expected = boot;
    if (!phase.compare_exchange_strong(expected, quarantine)) return ESP_FAIL;
    return ESP_OK;
}

extern "C" int __real_esp_rom_output_tx_one_char(std::uint8_t);
extern "C" void __real_ets_write_char_uart(char);
extern "C" int __real_uart_tx_one_char(std::uint8_t);
extern "C" int IRAM_ATTR __wrap_esp_rom_output_tx_one_char(std::uint8_t byte) {
    if (phase.load(std::memory_order_relaxed) == boot) return __real_esp_rom_output_tx_one_char(byte);
    phase.store(fault, std::memory_order_relaxed); return 1;
}
extern "C" void IRAM_ATTR __wrap_ets_write_char_uart(char byte) {
    if (phase.load(std::memory_order_relaxed) == boot) { __real_ets_write_char_uart(byte); return; }
    phase.store(fault, std::memory_order_relaxed);
}
extern "C" int IRAM_ATTR __wrap_uart_tx_one_char(std::uint8_t byte) {
    if (phase.load(std::memory_order_relaxed) == boot) return __real_uart_tx_one_char(byte);
    phase.store(fault, std::memory_order_relaxed); return 1;
}

bool ot_console_install() noexcept {
    // Constructors and timer init have completed, but no benchmark workers exist.
    if (phase.load() != quarantine) return false;
    if (!adapter.admit_exclusive_ownership()) { fail(); return false; }
    unsigned expected = quarantine;
    if (!phase.compare_exchange_strong(expected, owned)) { fail(); return false; }
    return true;
}
bool ot_console_healthy() noexcept { return phase.load() == owned; }
bool ot_console_session_started() noexcept { return ot_console_healthy() && receipt_enabled.load(); }
bool ot_console_begin_session() noexcept {
    if (!ot_console_healthy()) return false;
    bool expected=false;
    if(!session_ever_started.compare_exchange_strong(expected,true))return false;
    receipt_enabled.store(true); // One admission only; never rearm after a send.
    return ot_console_healthy();
}

bool ot_policy_send(const char* bytes,std::size_t size) noexcept {
    if(!bytes || size==0 || size>128 || !ot_console_healthy() || !receipt_enabled.exchange(false))return false;
    const std::uint64_t started=now(nullptr);
    if(receipt_active.test_and_set(std::memory_order_acquire)){fail();return false;}
    struct Release{~Release(){receipt_active.clear(std::memory_order_release);}} release;
    if(started>std::numeric_limits<std::uint64_t>::max()-receipt_budget_us){fail();return false;}
    receipt_deadline=started+receipt_budget_us;receipt_last_admission=started;
    const auto result=writer.write(reinterpret_cast<const std::uint8_t*>(bytes),size,receipt_budget_us);
    const auto ended=now(nullptr);
    if(result.status!=ConsoleWriteStatus::accepted || ended<started || ended>=receipt_deadline || !ot_console_healthy()){fail();return false;}
    return true;
}
bool ot_policy_read(char& byte) noexcept {
    if(!ot_console_healthy() || !usb_serial_jtag_ll_rxfifo_data_available())return false;
    std::uint8_t value=0;if(usb_serial_jtag_ll_read_rxfifo(&value,1)!=1)return false;byte=static_cast<char>(value);return true;
}

// Receipt output bypasses FILE entirely. ELF-resolved stdio is linker-wrapped.
// Final ELF audit must verify every live console-write call routes here; SDK
// syscall implementations may remain defined but must have no bypass callers.
// No writer or adapter is touched before constructors. SDK logs are compiled out.
extern "C" ssize_t __wrap__write_r(struct _reent* r, int fd, const void*, size_t size) {
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO) { __errno_r(r) = EBADF; return -1; }
    if (phase.load(std::memory_order_relaxed) == boot) return static_cast<ssize_t>(size);
    phase.store(fault, std::memory_order_relaxed);
    __errno_r(r) = EIO;
    return -1;
}
