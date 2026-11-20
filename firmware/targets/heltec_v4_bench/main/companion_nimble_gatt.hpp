#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/companion_gatt_authorization_adapter.hpp"
#include "opentrail/companion_configuration_dispatcher.hpp"
#include "opentrail/selected_enrollment_request_owner.hpp"

struct ble_gap_event;

namespace opentrail::target::heltec_v4_bench {

// App task owns the backend. These calls can read the existing radio grant and
// exchange data on it, but they do not confer or extend any grant.
class CompanionPhoneTextBackend {
public:
    virtual ~CompanionPhoneTextBackend() = default;
    virtual std::size_t command(const companion::DeviceNameContext&, radio::ByteView,
                                radio::MutableByteView, std::uint64_t now_ms) = 0;
    virtual std::size_t next_event(const companion::DeviceNameContext&,
                                  radio::MutableByteView, std::uint64_t now_ms) = 0;
    virtual void retire(const companion::DeviceNameContext&) = 0;
};

// Install from app_main before starting NimBLE in the opt-in development build.
void install_companion_phone_text_backend(CompanionPhoneTextBackend&);
[[nodiscard]] bool companion_phone_text_link_current(const companion::DeviceNameContext&);

// Configuration work is copied on the host and executed only by app_main.
[[nodiscard]] bool initialize_companion_configuration(
    companion::DeviceNamePersistence&, companion::ConfigurationBaseHandler&,
    companion::RegionPersistence&, companion::PublicProfilePersistence* public_profile = nullptr);
void service_companion_configuration();
// R2a: why an admission did not happen. `refused` covers lane, session and
// security-gate failures, which get no reply; the others are answered with
// a correlated rejected result so the phone can tell them apart.
enum class SelectedEnrollmentAdmission : std::uint8_t {
    refused, admitted, busy, invalid, retired,
};
// Called only by the normal selected configuration owner after a kind-2
// request is decoded. It admits a pending phone request, not enrollment.
[[nodiscard]] SelectedEnrollmentAdmission admit_selected_enrollment_request(
    const companion::DeviceNameContext&, std::uint32_t exchange_id);
// Application-owner only; no GATT mutex may be held. Must succeed before the
// existing NimBLE/controller owner disables its shared physical entropy source.
[[nodiscard]] bool close_companion_confirmation();
void invalidate_companion_configuration(bool revoke = false);
// Terminal containment gate. Must succeed before BLE teardown or reset cleanup.
[[nodiscard]] bool retire_selected_enrollment_request();
// V1 F5 R2a: the app task reads the admitted pairing request and a fresh
// connection authority together, under the GATT lock. It observes (and so
// expires or cancels) the request but never admits, extends or claims it.
[[nodiscard]] bool companion_pairing_request_snapshot(
    companion::SelectedEnrollmentRequest& request,
    companion::DeviceNameAuthority& authority);
// App-task publication is bound to its admitted ticket, independently of
// the session request consumed on commit. GATT serializes these calls.
// Release only after session cleanup and any final-response linger end.
void companion_pairing_request_complete(const companion::SelectedEnrollmentRequest&);
[[nodiscard]] bool companion_pairing_result_claim(const companion::SelectedEnrollmentRequest&);
void companion_pairing_result_publish(const companion::SelectedEnrollmentRequest&,
    companion::CompanionPairingState, companion::CompanionPairingReason, std::uint16_t peer_short_id);
[[nodiscard]] bool companion_pairing_result_snapshot(const companion::DeviceNameContext&,
    companion::CompanionPairingResult&);
[[nodiscard]] time::OledClockReading companion_configuration_clock();
[[nodiscard]] companion::DeviceNamePayload companion_configuration_name();
[[nodiscard]] companion::ConfigurationRegionPayload companion_configuration_region();

[[nodiscard]] bool companion_nimble_gatt_definition_self_check();

// Obtaining the real port performs no controller, GATT, advertising, or device
// I/O. A future serialized host owner uses it to construct the callback adapter.
[[nodiscard]] companion::CompanionGattIndicationPort&
companion_nimble_gatt_indication_port();

[[nodiscard]] companion::CompanionGattAdapterStatus
companion_nimble_gatt_adapter_status();

[[nodiscard]] bool companion_nimble_gatt_phone_ready(std::uint64_t generation);

// Attaches the exact callback adapter and service definitions before
// ble_gatts_start(). Normal0.2 is offered only after confirmed claim promotion.
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
