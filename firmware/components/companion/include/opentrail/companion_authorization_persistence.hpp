#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/companion_authorization.hpp"

namespace opentrail::companion {

inline constexpr std::size_t kCompanionAuthorizationDurableRecordBytes = 32;
inline constexpr std::size_t kCompanionPrivateBondReferenceBytes = 16;
inline constexpr std::size_t kCompanionBondBindingPrfMessageBytes = 40;
inline constexpr std::size_t kCompanionBondBindingPrfBytes = 32;

enum class CompanionAuthorizationDurableCodecError : std::uint8_t {
    none = 0,
    invalid_argument,
    invalid_size,
    invalid_magic,
    unsupported_version,
    invalid_record,
    integrity_failure,
};

struct CompanionAuthorizationDurableDecodeResult {
    CompanionAuthorizationDurableCodecError error{
        CompanionAuthorizationDurableCodecError::invalid_argument};
    CompanionAuthorizationRecord record{};

    [[nodiscard]] constexpr bool decoded() const {
        return error == CompanionAuthorizationDurableCodecError::none;
    }
};

[[nodiscard]] CompanionAuthorizationDurableCodecError
encode_companion_authorization_durable_record(
    const CompanionAuthorizationRecord& record,
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes>& output);

[[nodiscard]] CompanionAuthorizationDurableDecodeResult
decode_companion_authorization_durable_record(
    const std::array<std::uint8_t,
                     kCompanionAuthorizationDurableRecordBytes>& input);

// The record CRC detects accidental format corruption only. It is not a MAC,
// authenticity proof, or rollback defense; those remain mandatory properties
// of CompanionAuthorizationProtectedStore.

enum class CompanionAuthorizationProtectedStoreError : std::uint8_t {
    none = 0,
    not_ready,
    failed,
    uncertain,
    conflict,
};

struct CompanionAuthorizationProtectedSnapshot {
    CompanionAuthorizationProtectedStoreError error{
        CompanionAuthorizationProtectedStoreError::not_ready};
    bool record_present{false};
    std::uint32_t trusted_generation{0};
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> record{};
};

// This backend is a security boundary, not an ordinary NVS abstraction.
// load_verified() must read both the protected record and an independently
// rollback-resistant generation floor. compare_commit_and_verify() must compare
// expected_generation against fresh protected state, atomically publish the
// complete candidate plus new_generation, and return exact readback. `failed`
// is permitted only when no durable mutation was possible. Any error after a
// write may have applied is `uncertain`. The backend and caller are single-owner
// and externally serialized.
class CompanionAuthorizationProtectedStore {
public:
    virtual ~CompanionAuthorizationProtectedStore() = default;

    [[nodiscard]] virtual CompanionAuthorizationProtectedSnapshot
    load_verified() = 0;

    [[nodiscard]] virtual CompanionAuthorizationProtectedSnapshot
    compare_commit_and_verify(
        std::uint32_t expected_generation,
        std::uint32_t new_generation,
        const std::array<std::uint8_t,
                         kCompanionAuthorizationDurableRecordBytes>& record) = 0;
};

// Exact adapter for CompanionAuthorizationAuthority. It adds canonical record
// encoding and verifies the backend's record/floor readback. It does not make an
// unprotected backend safe and provides no concurrent-task synchronization.
class DurableCompanionAuthorizationPersistence final
    : public CompanionAuthorizationPersistence {
public:
    explicit DurableCompanionAuthorizationPersistence(
        CompanionAuthorizationProtectedStore& store);

    [[nodiscard]] CompanionAuthorizationLoadResult load() override;
    [[nodiscard]] CompanionAuthorizationCommitResult commit_and_verify(
        std::uint32_t expected_generation,
        const CompanionAuthorizationRecord& candidate) override;

private:
    CompanionAuthorizationProtectedStore& store_;
    bool operation_active_{false};
    bool reentry_observed_{false};
};

// Private reference owned by the trusted bond store. It is not a BLE address,
// public identity, peer-supplied value, or raw key. Re-pairing must allocate a
// new reference or advance bond_generation before the new bond can resolve.
// Distinctness depends on that private allocation/generation plus the protected
// PRF; no guarantee is derived from a public peer address.
struct CompanionPrivateBondReference {
    std::array<std::uint8_t, kCompanionPrivateBondReferenceBytes> value{};
    std::uint32_t bond_generation{0};
};

enum class CompanionBondBindingPrfError : std::uint8_t {
    none = 0,
    not_ready,
    failed,
};

// A target backend must use a separately provisioned device-secret PRF, such as
// an ESP32-S3 HMAC_UP eFuse key distinct from the NVS-encryption key. Raw key
// material never crosses this interface.
class CompanionBondBindingPrf {
public:
    virtual ~CompanionBondBindingPrf() = default;

    [[nodiscard]] virtual CompanionBondBindingPrfError calculate(
        const std::array<std::uint8_t,
                         kCompanionBondBindingPrfMessageBytes>& message,
        std::array<std::uint8_t, kCompanionBondBindingPrfBytes>& output) = 0;
};

enum class CompanionBondBindingError : std::uint8_t {
    none = 0,
    invalid_argument,
    not_ready,
    derivation_failed,
    invalid_output,
    reentrant_call,
};

struct CompanionBondBindingResult {
    CompanionBondBindingError error{CompanionBondBindingError::invalid_argument};
    CompanionBondIdentityToken token{};

    [[nodiscard]] constexpr bool resolved() const {
        return error == CompanionBondBindingError::none;
    }
};

class CompanionBondBindingResolver {
public:
    explicit CompanionBondBindingResolver(CompanionBondBindingPrf& prf);

    [[nodiscard]] CompanionBondBindingResult resolve(
        const CompanionPrivateBondReference& reference);

private:
    CompanionBondBindingPrf& prf_;
    bool operation_active_{false};
    bool reentry_observed_{false};
};

enum class CompanionAuthorizationTargetAdmissionError : std::uint8_t {
    none = 0,
    nvs_encryption_not_configured,
    protected_nvs_not_verified,
    nvs_hmac_key_protection_not_configured,
    nvs_hmac_key_not_verified,
    private_bond_store_missing,
    separate_binding_prf_key_missing,
    atomic_record_floor_missing,
    rollback_floor_missing,
};

struct CompanionAuthorizationTargetSecurityEvidence {
    bool nvs_encryption_configured{false};
    bool protected_nvs_initialized_and_verified{false};
    bool nvs_hmac_key_protection_configured{false};
    bool nvs_hmac_key_provisioned_and_usable{false};
    bool private_bond_store_available{false};
    bool separate_binding_prf_key_provisioned{false};
    bool atomic_record_and_floor_backend{false};
    bool independent_rollback_floor{false};
};

[[nodiscard]] constexpr CompanionAuthorizationTargetAdmissionError
evaluate_companion_authorization_target_security(
    const CompanionAuthorizationTargetSecurityEvidence& evidence) {
    if (!evidence.nvs_encryption_configured) {
        return CompanionAuthorizationTargetAdmissionError::
            nvs_encryption_not_configured;
    }
    if (!evidence.protected_nvs_initialized_and_verified) {
        return CompanionAuthorizationTargetAdmissionError::
            protected_nvs_not_verified;
    }
    if (!evidence.nvs_hmac_key_protection_configured) {
        return CompanionAuthorizationTargetAdmissionError::
            nvs_hmac_key_protection_not_configured;
    }
    if (!evidence.nvs_hmac_key_provisioned_and_usable) {
        return CompanionAuthorizationTargetAdmissionError::
            nvs_hmac_key_not_verified;
    }
    if (!evidence.private_bond_store_available) {
        return CompanionAuthorizationTargetAdmissionError::
            private_bond_store_missing;
    }
    if (!evidence.separate_binding_prf_key_provisioned) {
        return CompanionAuthorizationTargetAdmissionError::
            separate_binding_prf_key_missing;
    }
    if (!evidence.atomic_record_and_floor_backend) {
        return CompanionAuthorizationTargetAdmissionError::
            atomic_record_floor_missing;
    }
    if (!evidence.independent_rollback_floor) {
        return CompanionAuthorizationTargetAdmissionError::
            rollback_floor_missing;
    }
    return CompanionAuthorizationTargetAdmissionError::none;
}

}  // namespace opentrail::companion
