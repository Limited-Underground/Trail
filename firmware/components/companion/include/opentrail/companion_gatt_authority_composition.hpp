#pragma once

#include <cstdint>

#include "opentrail/companion_authorization_persistence.hpp"
#include "opentrail/companion_gatt_authorization_adapter.hpp"

namespace opentrail::companion {

enum class CompanionGattPrivateBondReferenceError : std::uint8_t {
    none = 0,
    not_ready,
    failed,
};

struct CompanionGattPrivateBondReferenceResult {
    CompanionGattPrivateBondReferenceError error{
        CompanionGattPrivateBondReferenceError::not_ready};
    CompanionPrivateBondReference reference{};
};

// Trusted private source for one exact live connection generation. A target
// implementation must resolve an opaque locally owned reference without
// exposing or deriving authority from a public address, name, peer payload, or
// raw key. Non-success must return an empty reference and consume no identity.
class CompanionGattPrivateBondReferenceSource {
public:
    virtual ~CompanionGattPrivateBondReferenceSource() = default;

    [[nodiscard]] virtual CompanionGattPrivateBondReferenceResult resolve(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation) = 0;
};

struct CompanionGattPrivateSessionContext {
    std::uint16_t connection_handle{kCompanionGattInvalidConnectionHandle};
    std::uint64_t transport_generation{0};
    CompanionBondIdentityToken bond_identity{};
};

enum class CompanionGattPrivateSessionError : std::uint8_t {
    none = 0,
    not_ready,
    failed,
    exhausted,
};

struct CompanionGattPrivateSessionResult {
    CompanionGattPrivateSessionError error{
        CompanionGattPrivateSessionError::not_ready};
    std::uint64_t boot_challenge{0};
    std::uint64_t session_challenge{0};
    std::uint64_t controller_binding{0};
    std::uint32_t provisional_session_nonce{0};
};

// Private serialized issuer. A successful issue must bind every returned value
// to the exact context, keep the boot challenge stable for the boot, and mint
// fresh nonzero session, controller-binding, and provisional-session values.
// A non-success result must consume nothing. No returned value is public or
// loggable.
class CompanionGattPrivateSessionIssuer {
public:
    virtual ~CompanionGattPrivateSessionIssuer() = default;

    [[nodiscard]] virtual CompanionGattPrivateSessionResult issue(
        const CompanionGattPrivateSessionContext& context) = 0;
};

// Host-composable implementation of the existing trusted-binding seam. The
// first successful resolution for one connection/transport tuple is cached and
// returned byte-for-byte on later security refreshes. Observing a newer
// transport generation invalidates the old tuple; older generations and a
// different connection at the current generation fail closed. This class owns
// no target storage, cryptographic key, log surface, or concurrent-task lock.
class PersistentCompanionGattTrustedBindingAuthority final
    : public CompanionGattTrustedBindingAuthority {
public:
    PersistentCompanionGattTrustedBindingAuthority(
        CompanionGattPrivateBondReferenceSource& reference_source,
        CompanionBondBindingResolver& binding_resolver,
        CompanionGattPrivateSessionIssuer& session_issuer);

    [[nodiscard]] CompanionGattTrustedBindingResult resolve(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation) override;

private:
    CompanionGattPrivateBondReferenceSource& reference_source_;
    CompanionBondBindingResolver& binding_resolver_;
    CompanionGattPrivateSessionIssuer& session_issuer_;
    bool operation_active_{false};
    bool reentry_observed_{false};
    bool context_seen_{false};
    bool reference_seen_{false};
    bool cached_{false};
    std::uint16_t connection_handle_{kCompanionGattInvalidConnectionHandle};
    std::uint64_t transport_generation_{0};
    CompanionPrivateBondReference reference_{};
    CompanionGattTrustedBindingResult cached_result_{};
};

// Exact adapter from the accepted one-phone authorization authority to the
// GATT claim seam. First-owner authorization uses claim_owner(), an existing
// owner uses authorize_connection(), replacement uses replace_owner(), and
// disconnect releases only the exact controller lease. Physical windows and
// durable restore remain explicitly owned outside this adapter.
class PersistentCompanionGattAuthorizationAuthority final
    : public CompanionGattAuthorizationAuthority {
public:
    explicit PersistentCompanionGattAuthorizationAuthority(
        CompanionAuthorizationAuthority& authority);

    [[nodiscard]] CompanionGattAuthorizationDecision apply_claim(
        CompanionAuthorizationPurpose purpose,
        const CompanionControllerClaim& claim,
        std::uint64_t now_ms) override;

    [[nodiscard]] CompanionGattAuthorizationAuthorityError
    release_connection(std::uint64_t controller_binding) override;

private:
    CompanionAuthorizationAuthority& authority_;
    bool operation_active_{false};
    bool reentry_observed_{false};
};

}  // namespace opentrail::companion
