#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/companion_gatt_authorization.hpp"

namespace opentrail::companion {

inline constexpr std::size_t kCompanionV1OwnerRecordBytes = 32;

enum class CompanionV1OwnerStorageError : std::uint8_t {
    none = 0,
    not_ready,
    failed,
    uncertain,
    conflict,
};

struct CompanionV1OwnerStorageSnapshot {
    CompanionV1OwnerStorageError error{CompanionV1OwnerStorageError::not_ready};
    bool record_present{false};
    std::array<std::uint8_t, kCompanionV1OwnerRecordBytes> record{};
};

// Ordinary V1 application storage. This is intentionally distinct from the
// historical rollback-floor OAP0 backend. A successful commit must return a
// fresh byte-exact readback. Once mutation may have occurred, every failure is
// uncertain; implementations must never erase or repair automatically.
class CompanionV1OwnerStoragePort {
public:
    virtual ~CompanionV1OwnerStoragePort() = default;
    [[nodiscard]] virtual CompanionV1OwnerStorageSnapshot load() = 0;
    [[nodiscard]] virtual CompanionV1OwnerStorageSnapshot
    commit_absent_and_readback(
        const std::array<std::uint8_t,
                         kCompanionV1OwnerRecordBytes>& record) = 0;
};

enum class CompanionV1BondInventoryError : std::uint8_t {
    none = 0,
    not_ready,
    failed,
};

struct CompanionV1BondInventorySnapshot {
    CompanionV1BondInventoryError error{CompanionV1BondInventoryError::not_ready};
    std::uint8_t bond_count{0};
    std::array<CompanionBondIdentityToken, 2> private_references{};
};

// Returns only locally derived opaque private bond references. Addresses,
// names, peer payloads, and raw keys may not cross this boundary.
class CompanionV1BondInventoryPort {
public:
    virtual ~CompanionV1BondInventoryPort() = default;
    [[nodiscard]] virtual CompanionV1BondInventorySnapshot snapshot() = 0;
};

enum class CompanionV1BondCleanupError : std::uint8_t {
    none = 0,
    not_ready,
    failed,
    uncertain,
};

struct CompanionV1BondCleanupResult {
    CompanionV1BondCleanupError error{CompanionV1BondCleanupError::not_ready};
    CompanionV1BondInventorySnapshot verified_inventory{};
};

// Removes only the bond matching one opaque private reference. Implementations
// must reject missing or ambiguous matches before mutation, never evict an
// unrelated bond, and return a fresh inventory snapshot after deletion.
class CompanionV1BondCleanupPort {
public:
    virtual ~CompanionV1BondCleanupPort() = default;
    [[nodiscard]] virtual CompanionV1BondCleanupResult
    remove_exact_and_verify(CompanionBondIdentityToken private_reference) = 0;
};

enum class CompanionV1BondOwnerPhase : std::uint8_t {
    not_restored = 0,
    closed_unowned,
    closed_owned,
    controller_active,
    candidate_cleanup_required,
    reconcile_required,
};

enum class CompanionV1BondOwnerError : std::uint8_t {
    none = 0,
    invalid_argument,
    not_restored,
    not_ready,
    storage_failed,
    storage_uncertain,
    storage_conflict,
    record_invalid,
    bond_inventory_mismatch,
    owner_mismatch,
    controller_in_use,
    wrong_controller,
    bond_cleanup_failed,
    unsupported_operation,
    reentrant_call,
};

struct CompanionV1BondOwnerStatus {
    CompanionV1BondOwnerPhase phase{CompanionV1BondOwnerPhase::not_restored};
    bool owner_present{false};
    bool controller_active{false};
    bool persistence_uncertain{false};
    std::uint32_t owner_generation{0};
};

struct CompanionV1BondOwnerResult {
    CompanionV1BondOwnerError error{CompanionV1BondOwnerError::invalid_argument};
    CompanionV1BondOwnerPhase phase{CompanionV1BondOwnerPhase::not_restored};

    [[nodiscard]] constexpr bool accepted() const {
        return error == CompanionV1BondOwnerError::none;
    }
};

// Fixed-memory single-owner bridge for the accepted V1 physical-flash threat
// model. V1 has no phone-replacement authority.
// Calls must be externally serialized by one owner context; the reentry guard
// contains synchronous callback recursion and is not a thread mutex. Absence
// is the only coherent unowned record. It removes only an exact orphan or a
// failed initial-claim bond through the cleanup seam; it never performs broad
// erasure, speculative repair, owner replacement, or rollback-floor work.
class CompanionV1BondOwnerBridge {
public:
    CompanionV1BondOwnerBridge(CompanionV1OwnerStoragePort& storage,
                               CompanionV1BondInventoryPort& inventory);
    CompanionV1BondOwnerBridge(CompanionV1OwnerStoragePort& storage,
                               CompanionV1BondInventoryPort& inventory,
                               CompanionV1BondCleanupPort& cleanup);

    [[nodiscard]] CompanionV1BondOwnerResult restore();
    [[nodiscard]] CompanionV1BondOwnerResult accept_initial_bond(
        CompanionBondIdentityToken private_reference);
    [[nodiscard]] CompanionV1BondOwnerResult authorize_controller(
        CompanionBondIdentityToken private_reference,
        std::uint64_t controller_binding);
    [[nodiscard]] CompanionV1BondOwnerResult release_controller(
        std::uint64_t controller_binding);
    // After a known pre-mutation initial-owner commit failure, remove only the
    // exact failed-claim bond and verify the expected remaining inventory.
    [[nodiscard]] CompanionV1BondOwnerResult complete_pending_cleanup();
    [[nodiscard]] CompanionV1BondOwnerStatus status() const;

private:
    [[nodiscard]] CompanionV1BondOwnerResult reject(
        CompanionV1BondOwnerError error);
    void require_reconciliation(CompanionV1BondOwnerError error);

    CompanionV1OwnerStoragePort& storage_;
    CompanionV1BondInventoryPort& inventory_;
    CompanionV1BondCleanupPort* cleanup_{nullptr};
    CompanionV1BondOwnerPhase phase_{CompanionV1BondOwnerPhase::not_restored};
    CompanionBondIdentityToken owner_{};
    CompanionBondIdentityToken candidate_{};
    std::uint32_t generation_{0};
    std::uint64_t active_controller_binding_{0};
    bool persistence_uncertain_{false};
    bool operation_active_{false};
    bool reentry_observed_{false};
};

// Exact adapter into the existing protected-GATT lifecycle. Initial ownership
// must already have been committed by accept_initial_bond() after the accepted
// local pairing window. Bond state alone never reaches this adapter as owner
// authority. The retained legacy replace_controller enum is always denied as
// unsupported by this V1 authority.
class CompanionV1GattAuthorizationAuthority final
    : public CompanionGattAuthorizationAuthority {
public:
    explicit CompanionV1GattAuthorizationAuthority(
        CompanionV1BondOwnerBridge& owner,
        std::uint64_t expected_boot_challenge);

    [[nodiscard]] CompanionGattAuthorizationDecision apply_claim(
        CompanionAuthorizationPurpose purpose,
        const CompanionControllerClaim& claim,
        std::uint64_t now_ms) override;
    [[nodiscard]] CompanionGattAuthorizationAuthorityError release_connection(
        std::uint64_t controller_binding) override;

private:
    // This authority has the same externally serialized owner requirement as
    // the bridge. The target runs accept/authorize/release on the NimBLE host
    // context after completing restore before that host starts.
    CompanionV1BondOwnerBridge& owner_;
    std::uint64_t expected_boot_challenge_{0};
    std::uint64_t last_session_challenge_{0};
    std::uint64_t active_session_challenge_{0};
    std::uint64_t active_controller_binding_{0};
    bool operation_active_{false};
    bool reentry_observed_{false};
};

}  // namespace opentrail::companion
