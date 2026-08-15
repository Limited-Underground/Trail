#pragma once

#include <cstdint>

#include "opentrail/companion_request_coordinator.hpp"

namespace opentrail::target::heltec_v4_bench {

struct CompanionGattAuthorizationResult {
    bool authorized{false};
    std::uint64_t controller_binding{0};
};

// Injected application policy only. The adapter separately rechecks the live
// NimBLE encrypted, authenticated, and bonded connection state on every access.
class CompanionGattApplicationAuthorization {
public:
    virtual ~CompanionGattApplicationAuthorization() = default;
    [[nodiscard]] virtual CompanionGattAuthorizationResult authorize(
        std::uint16_t connection_handle) const = 0;
};

// Pure definition check. It does not initialize NimBLE, register a service,
// start a controller, advertise, access a device, or open a transport.
[[nodiscard]] bool companion_nimble_gatt_definition_self_check();

// Future host-owner seam. Call only after NimBLE host initialization and from
// its serialized owner context. Registration fails unless both the persistent
// application-authorization authority and the already-constructed device
// coordinator are injected. This target does not call this function.
[[nodiscard]] int register_companion_nimble_gatt_service(
    CompanionGattApplicationAuthorization* application_authorization,
    companion::CompanionRequestCoordinator* coordinator);

// Future BLE_GAP_EVENT_AUTHORIZE seam for known characteristic value handles.
// It deliberately does not infer a Stream CCCD handle from value_handle + 1.
// The current target installs no registration, subscription, disconnect, or
// authorization callback, so NimBLE's default for AUTHOR remains rejection.
[[nodiscard]] bool companion_nimble_gatt_attribute_authorized(
    std::uint16_t connection_handle,
    std::uint16_t attribute_handle);

}  // namespace opentrail::target::heltec_v4_bench
