#pragma once

#include <cstdint>

#include "opentrail/companion_gatt_authorization_adapter.hpp"

struct ble_gap_event;

namespace opentrail::target::heltec_v4_bench {

[[nodiscard]] bool companion_nimble_gatt_definition_self_check();

// Obtaining the real port performs no controller, GATT, advertising, or device
// I/O. A future serialized host owner uses it to construct the callback adapter.
[[nodiscard]] companion::CompanionGattIndicationPort&
companion_nimble_gatt_indication_port();

// Attaches the exact callback adapter and service definitions before
// ble_gatts_start(). The current build-only target never calls this function.
// Protocol Info and Command use NimBLE ENC+AUTHEN+AUTHOR permissions. Their
// AUTHOR callback and actual access callback each re-read the current link;
// encryption/key-size failure maps to INSUFFICIENT_ENC, authentication/bond
// failure to INSUFFICIENT_AUTHEN, and private-binding/lifecycle denial to
// INSUFFICIENT_AUTHOR. A successful exact v0.1 Protocol Info read is the
// client-visible device-enforced proof for this restricted path.
[[nodiscard]] int register_companion_nimble_gatt_service(
    companion::CompanionGattAuthorizationCallbackAdapter* adapter);

// Exact serialized GAP callback for a future peripheral owner. It never starts
// or resumes advertising. Both a new bonded controller and a secure reconnect
// use v0.1 plus Claim Start; this adapter has no v0.0 owner shortcut.
[[nodiscard]] int companion_nimble_gatt_gap_event(
    ble_gap_event* event,
    void* argument);

// Exact device-local physical-decision and timer seams. Callers retain the full
// tuple from status(); stale generations/tokens fail closed.
[[nodiscard]] companion::CompanionGattAuthorizationRequestResult
companion_nimble_gatt_resolve_claim(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation,
    std::uint32_t session_nonce,
    std::uint32_t exchange_id,
    std::uint64_t now_ms);
[[nodiscard]] companion::CompanionGattAdapterError
companion_nimble_gatt_service_timeout(
    const companion::CompanionGattAdapterPendingIndication& expected,
    std::uint64_t now_ms);

}  // namespace opentrail::target::heltec_v4_bench
