#pragma once

#include <cstdint>

#include "opentrail/companion_ble_runtime_owner.hpp"

namespace opentrail::target::heltec_v4_bench {

// Starts the single real NimBLE owner. The current OT-054 target admission must
// be denied; a deny-only private binding and authority remain injected, so no
// authorization claim or normal command can succeed.
[[nodiscard]] companion::CompanionBleRuntimeError
start_companion_nimble_runtime(std::uint64_t now_ms);

// Drains at most the fixed callback queue capacity, services bounded
// re-advertising and the host-sync watchdog, and performs no device logging.
[[nodiscard]] companion::CompanionBleRuntimeError
service_companion_nimble_runtime(std::uint64_t now_ms);

[[nodiscard]] companion::CompanionBleRuntimeStatus
companion_nimble_runtime_status();

}  // namespace opentrail::target::heltec_v4_bench
