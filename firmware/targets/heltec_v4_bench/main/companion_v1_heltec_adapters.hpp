#pragma once

#include <array>
#include <cstdint>

#include "nvs.h"

#include "opentrail/companion_gatt_authorization_adapter.hpp"
#include "opentrail/companion_v1_bond_owner.hpp"
#include "opentrail/secure_random.hpp"

namespace opentrail::targets::heltec_v4_bench {

// Ordinary application state. This namespace is intentionally distinct from
// both NimBLE's bond store and the historical OAP0 rollback-floor backend.
inline constexpr char kCompanionV1OwnerNvsNamespace[] = "ot_v1_owner";
inline constexpr char kCompanionV1OwnerNvsKey[] = "owner_v0";

// Opens the dedicated namespace on construction after the caller has
// initialized the default NVS partition. The adapter owns only its namespace
// handle; it never erases, repairs, retries, or logs durable state.
class HeltecV4CompanionV1OwnerStorage final
    : public companion::CompanionV1OwnerStoragePort {
public:
    HeltecV4CompanionV1OwnerStorage();
    ~HeltecV4CompanionV1OwnerStorage() override;

    HeltecV4CompanionV1OwnerStorage(
        const HeltecV4CompanionV1OwnerStorage&) = delete;
    HeltecV4CompanionV1OwnerStorage& operator=(
        const HeltecV4CompanionV1OwnerStorage&) = delete;

    [[nodiscard]] companion::CompanionV1OwnerStorageSnapshot load() override;
    [[nodiscard]] companion::CompanionV1OwnerStorageSnapshot
    commit_absent_and_readback(
        const std::array<std::uint8_t,
                         companion::kCompanionV1OwnerRecordBytes>& record)
        override;

private:
    nvs_handle_t handle_{0};
};

// The NimBLE address and raw bond-key records remain private to the .cpp.
// Inventory and live-connection resolution expose only the same stable,
// domain-separated 128-bit reference derived from authenticated SC bond
// material. The trusted-binding result is cached for one exact transport
// generation. Its boot/controller/nonce values come from the platform secure
// random source; its session challenge is a strictly increasing boot-local
// counter.
class HeltecV4CompanionV1NimbleBondAdapter final
    : public companion::CompanionV1BondInventoryPort,
      public companion::CompanionGattTrustedBindingAuthority {
public:
    explicit HeltecV4CompanionV1NimbleBondAdapter(
        security::SecureRandomSource& random);

    [[nodiscard]] companion::CompanionV1BondInventorySnapshot snapshot()
        override;
    [[nodiscard]] companion::CompanionGattTrustedBindingResult resolve(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation) override;

private:
    security::SecureRandomSource& random_;
    bool operation_active_{false};
    bool boot_challenge_ready_{false};
    bool context_seen_{false};
    bool reference_seen_{false};
    bool cached_{false};
    std::uint16_t connection_handle_{
        companion::kCompanionGattInvalidConnectionHandle};
    std::uint64_t transport_generation_{0};
    std::uint64_t boot_challenge_{0};
    std::uint64_t next_session_challenge_{1};
    companion::CompanionBondIdentityToken reference_{};
    companion::CompanionGattTrustedBindingResult cached_result_{};
};

}  // namespace opentrail::targets::heltec_v4_bench
