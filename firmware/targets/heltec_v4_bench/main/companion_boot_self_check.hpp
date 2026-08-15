#pragma once

namespace opentrail::target::heltec_v4_bench {

// Deterministic local-computation check only. This opens no transport and
// touches no device authority, persistent state, radio, or peripheral.
[[nodiscard]] bool run_companion_request_coordinator_self_check();

// Deterministic target-neutral GATT lifecycle check. This reserves and submits
// fixed bytes only to an in-memory sink; it starts no Bluetooth runtime.
[[nodiscard]] bool run_companion_gatt_session_self_check();

// Deterministic restricted authorization lifecycle check. This uses only
// fixed fake bond/authority evidence and an in-memory indication sink; it does
// not register or start a Bluetooth controller, GATT server, or advertiser.
[[nodiscard]] bool run_companion_gatt_authorization_self_check();

}  // namespace opentrail::target::heltec_v4_bench
