#pragma once

namespace opentrail::target::heltec_v4_bench {

// Deterministic local-computation check only. This opens no transport and
// touches no device authority, persistent state, radio, or peripheral.
[[nodiscard]] bool run_companion_request_coordinator_self_check();

}  // namespace opentrail::target::heltec_v4_bench
