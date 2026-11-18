// P1-Pro adapter composition. Hardware initialization and all TX remain disabled.
#include <Arduino.h>
#include <Adafruit_TinyUSB.h> // BSP runtime Serial linkage; no application USB calls.
#include "opentrail/repeater_dispatch_controller.hpp"
#include "p1pro_sx1262_radio_port.hpp"

namespace {
using namespace opentrail::delivery;
class DeniedAuthority final : public RepeaterLifecycleProvider {
public:
    bool consume_request(RepeaterLifecycleOperation, std::uint64_t,
        const RepeaterLifecycleScope&, std::uint64_t, RepeaterLifecycleRequest&) noexcept override { return false; }
    bool request_current(RepeaterLifecycleOperation, std::uint64_t,
        const RepeaterLifecycleRequest&, std::uint64_t) noexcept override { return false; }
    bool current(const RepeaterLifecycleSnapshot&, std::uint64_t) noexcept override { return false; }
    bool consume_consent(std::uint64_t, const RepeaterLifecycleSnapshot&,
        std::uint64_t, RepeaterLifecycleConsent&) noexcept override { return false; }
    bool verify_frame(std::uint64_t, const RepeaterLifecycleSnapshot&,
        opentrail::radio::ByteView, std::uint64_t, VerifiedForwardingMetadata&) noexcept override { return false; }
};
class UnavailableRoles final : public RepeaterLifecycleRoleStorage {
public:
    RepeaterRoleStorageError read(RepeaterLifecycleRole&) noexcept override { return RepeaterRoleStorageError::io_failure; }
    RepeaterRoleStorageError write(const RepeaterLifecycleRole&) noexcept override { return RepeaterRoleStorageError::io_failure; }
};
class UnavailableReplay final : public DuplicateCheckpointStorage {
public:
    DuplicateCheckpointStorageError read_slot(std::uint8_t, std::uint8_t*, std::size_t) override { return DuplicateCheckpointStorageError::io_failure; }
    DuplicateCheckpointStorageError write_slot(std::uint8_t, const std::uint8_t*, std::size_t) override { return DuplicateCheckpointStorageError::io_failure; }
    DuplicateCheckpointStorageError erase_slot(std::uint8_t) override { return DuplicateCheckpointStorageError::io_failure; }
};
class UnavailableLedger final : public RepeaterDispatchLedgerPort {
public:
    RepeaterDispatchLedgerError read(RepeaterDispatchLedger&) noexcept override { return RepeaterDispatchLedgerError::unavailable; }
    RepeaterDispatchLedgerError compare_exchange(const RepeaterDispatchLedger&, const RepeaterDispatchLedger&) noexcept override { return RepeaterDispatchLedgerError::unavailable; }
};
DeniedAuthority authority;
UnavailableRoles roles;
UnavailableReplay replay;
UnavailableLedger ledger;
opentrail::p1pro::RadioLibSx1262Backend radio_backend;
opentrail::p1pro::Sx1262RetainedState radio_state;
opentrail::p1pro::P1ProSx1262RadioPort radio(radio_backend, radio_state);
// Deliberately invalid zero authority/configuration. No production budget selected.
const RepeaterDispatchConfiguration configuration{};
RepeaterDispatchController controller(configuration, authority, roles, replay, ledger, radio);
// Explicit bench entry points retained in ELF, never invoked by setup/loop.
std::int16_t receive_probe(const opentrail::p1pro::P1ProReceiveSettings& s) { return radio_backend.initialize_receive_only(s); }
std::int16_t rearm_probe() { return radio_backend.rearm_receive_only(); }
std::int16_t irq_probe(std::uint16_t& flags) { return radio_backend.read_irq_flags(flags); }
auto (* volatile receive_hook)(const opentrail::p1pro::P1ProReceiveSettings&) = receive_probe;
auto (* volatile rearm_hook)() = rearm_probe;
auto (* volatile irq_hook)(std::uint16_t&) = irq_probe;
volatile bool bench_hooks_present = false;
volatile bool smoke_refused = false;
volatile std::size_t smoke_controller_bytes = sizeof(controller);
}

void setup() {
    bench_hooks_present = receive_hook != nullptr && rearm_hook != nullptr && irq_hook != nullptr;
    const auto recovery = controller.recover(0, 0);
    const auto consent = controller.consent(0, 0);
    const auto admission = controller.admit(0, {nullptr, 0}, 0);
    const auto service = controller.service(0);
    const auto status = controller.status();
    smoke_refused = recovery != RepeaterDispatchFault::none &&
        consent != RepeaterDispatchFault::none && service != RepeaterDispatchFault::none &&
        !admission.forwarding.queued &&
        status.state != RepeaterDispatchState::ready;
    (void)controller.shutdown();
}

void loop() {
    // No RF initialization, transport send, enrollment, persistence or automatic recovery.
    delay(1000);
}
