#pragma once

#include <cstdint>

#include "opentrail/companion_ble_runtime_owner.hpp"
#include "opentrail/companion_pairing_window.hpp"
#include "opentrail/device_factory_reset_executor.hpp"
#include "opentrail/secure_random.hpp"

namespace opentrail::target::heltec_v4_bench {
class StartupDisplayOwner;

enum class CompanionAppFactoryResetPhase : std::uint8_t {
    idle = 0,
    response_pending,
    response_confirmed,
    response_unknown,
};

struct CompanionAppFactoryResetStatus {
    CompanionAppFactoryResetPhase phase{
        CompanionAppFactoryResetPhase::idle};
    bool protected_access_blocked{false};
};

// Starts the single real NimBLE owner and binds the target-local pairing window,
// exact private bond owner, and verified bond-cleanup adapters before the host
// starts. The owner is serialized across app_main and GAP callback contexts;
// the PIN and private bond reference never enter this API or a status value.
[[nodiscard]] companion::CompanionBleRuntimeError
start_companion_nimble_runtime(
    std::uint64_t now_ms,
    companion::CompanionPairingWindow& pairing_window,
    security::SecureRandomSource& random,
    StartupDisplayOwner& display);

// Drains at most the fixed callback queue capacity, services bounded
// re-advertising and the host-sync watchdog, and performs no device logging.
[[nodiscard]] companion::CompanionBleRuntimeError
service_companion_nimble_runtime(std::uint64_t now_ms);

// NimBLE callbacks queue only verified protected-link progress. The serialized
// runtime owner renews the exact handle/generation lease while draining events.
void observe_companion_verified_gatt_progress(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation,
    std::uint64_t observed_at_ms);

[[nodiscard]] companion::CompanionBleRuntimeStatus
companion_nimble_runtime_status();

// Privacy-safe startup diagnostic. Zero means that secure configuration has
// not failed; nonzero values identify only a bounded configuration stage.
[[nodiscard]] std::uint8_t companion_nimble_security_failure_stage();
[[nodiscard]] std::uint8_t companion_nimble_security_failure_detail();

// Independently requests complete BLE host/controller containment and reports
// true only after the runtime owner has verified platform shutdown.
[[nodiscard]] bool contain_companion_nimble_runtime_for_recovery();

// Serializes the physical path against a protected app reset, stops the
// complete BLE host/controller, and then durably commits reset intent. The
// caller must reboot after every accepted or uncertain attempt; erasure is
// performed by the pre-owner boot gate on the following boot.
[[nodiscard]] companion::DeviceFactoryResetResult
begin_companion_factory_reset();

// Physical-confirmation-only recovery from an already-contained companion
// startup failure. No BLE/data service is restarted. After verified stack
// shutdown this may commit reset intent from idle_old_state, not_restored, or
// reconciliation_required so the next boot performs normal verified cleanup.
[[nodiscard]] companion::DeviceFactoryResetResult
begin_contained_companion_factory_reset_recovery();

// Target-local hooks used by the protected GATT bridge. An admitted action
// result proves only durable intent; completion remains the verified unowned
// D1 boot after cleanup. These functions expose no PIN, bond, or device ID.
// An exact reset command must acquire this shared serialization boundary before
// calling the protected adapter and release it immediately afterward.
[[nodiscard]] bool acquire_companion_factory_reset_serialization();
void release_companion_factory_reset_serialization();
void observe_companion_app_factory_reset_command(
    bool response_pending,
    std::uint64_t now_ms);
void observe_companion_app_factory_reset_response(bool confirmed);
[[nodiscard]] bool
companion_app_factory_reset_blocks_protected_access();
[[nodiscard]] CompanionAppFactoryResetStatus
companion_app_factory_reset_status();

[[nodiscard]] companion::CompanionPairingWindowError
service_companion_pairing_window(std::uint64_t now_ms);

[[nodiscard]] companion::CompanionPairingWindowError
fault_companion_pairing_window();

[[nodiscard]] companion::CompanionPairingWindowStatus
companion_pairing_window_status();

}  // namespace opentrail::target::heltec_v4_bench
