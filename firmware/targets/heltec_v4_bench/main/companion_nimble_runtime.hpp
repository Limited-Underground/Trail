#pragma once

#include <cstdint>

#include "opentrail/companion_ble_runtime_owner.hpp"
#include "opentrail/companion_pairing_window.hpp"

namespace opentrail::target::heltec_v4_bench {

// Starts the single real NimBLE owner. The current OT-054 target admission must
// be denied; a deny-only private binding and authority remain injected, so no
// authorization claim or normal command can succeed.
// Binds the single target-local pairing-window owner before NimBLE starts.
// The owner is serialized across app_main and GAP callback contexts; the PIN
// never enters this API or a status value.
[[nodiscard]] companion::CompanionBleRuntimeError
start_companion_nimble_runtime(
    std::uint64_t now_ms,
    companion::CompanionPairingWindow& pairing_window);

// Drains at most the fixed callback queue capacity, services bounded
// re-advertising and the host-sync watchdog, and performs no device logging.
[[nodiscard]] companion::CompanionBleRuntimeError
service_companion_nimble_runtime(std::uint64_t now_ms);

[[nodiscard]] companion::CompanionBleRuntimeStatus
companion_nimble_runtime_status();

[[nodiscard]] companion::CompanionPairingWindowError
open_companion_pairing_window(
    std::uint64_t now_ms,
    std::uint64_t physical_event,
    std::uint64_t hold_ms);

[[nodiscard]] companion::CompanionPairingWindowError
service_companion_pairing_window(std::uint64_t now_ms);

[[nodiscard]] companion::CompanionPairingWindowError
fault_companion_pairing_window();

[[nodiscard]] companion::CompanionPairingWindowStatus
companion_pairing_window_status();

}  // namespace opentrail::target::heltec_v4_bench
