#include "companion_authorization_storage.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif

namespace opentrail::target::heltec_v4_bench {
namespace {

using namespace opentrail::companion;

// These remain false until separately reviewed target implementations exist.
// CONFIG_BT_NIMBLE_NVS_PERSIST is intentionally disabled in this candidate and
// ordinary NimBLE persistence would not by itself provide the required private
// bond-reference, atomic record/floor, or rollback guarantees.
constexpr bool kPrivateBondStoreImplemented = false;
constexpr bool kSeparateBindingPrfKeyProvisioned = false;
constexpr bool kAtomicRecordAndFloorBackendImplemented = false;
constexpr bool kIndependentRollbackFloorImplemented = false;

class MemoryProtectedStore final : public CompanionAuthorizationProtectedStore {
public:
    CompanionAuthorizationProtectedSnapshot load_verified() override {
        return {CompanionAuthorizationProtectedStoreError::none, present_,
                generation_, record_};
    }

    CompanionAuthorizationProtectedSnapshot compare_commit_and_verify(
        std::uint32_t expected_generation,
        std::uint32_t new_generation,
        const std::array<std::uint8_t,
                         kCompanionAuthorizationDurableRecordBytes>& record)
        override {
        if (expected_generation != generation_) {
            return {CompanionAuthorizationProtectedStoreError::conflict,
                    present_, generation_, record_};
        }
        present_ = true;
        generation_ = new_generation;
        record_ = record;
        return {CompanionAuthorizationProtectedStoreError::none, present_,
                generation_, record_};
    }

private:
    bool present_{false};
    std::uint32_t generation_{0};
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> record_{};
};

class FixedPrf final : public CompanionBondBindingPrf {
public:
    CompanionBondBindingPrfError calculate(
        const std::array<std::uint8_t,
                         kCompanionBondBindingPrfMessageBytes>& message,
        std::array<std::uint8_t, kCompanionBondBindingPrfBytes>& output)
        override {
        for (std::size_t index = 0; index < output.size(); ++index) {
            output[index] = static_cast<std::uint8_t>(
                message[index] ^ message[index + 8] ^
                static_cast<std::uint8_t>(0x71U + index));
        }
        return CompanionBondBindingPrfError::none;
    }
};

}  // namespace

CompanionAuthorizationTargetSecurityEvidence
companion_authorization_storage_security_evidence() {
    CompanionAuthorizationTargetSecurityEvidence evidence{};
#if defined(CONFIG_NVS_ENCRYPTION) && CONFIG_NVS_ENCRYPTION
    evidence.nvs_encryption_configured = true;
#endif
#if defined(CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC) && \
    CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC
    evidence.nvs_hmac_key_protection_configured = true;
#endif
    // Configuration never proves a partition was securely initialized or an
    // eFuse key is provisioned, read-protected, and usable. Those fields remain
    // false until a future authorized runtime verifier supplies that evidence.
    evidence.protected_nvs_initialized_and_verified = false;
    evidence.nvs_hmac_key_provisioned_and_usable = false;
    evidence.private_bond_store_available = kPrivateBondStoreImplemented;
    evidence.separate_binding_prf_key_provisioned =
        kSeparateBindingPrfKeyProvisioned;
    evidence.atomic_record_and_floor_backend =
        kAtomicRecordAndFloorBackendImplemented;
    evidence.independent_rollback_floor =
        kIndependentRollbackFloorImplemented;
    return evidence;
}

CompanionAuthorizationTargetAdmissionError
companion_authorization_storage_preflight() {
    return evaluate_companion_authorization_target_security(
        companion_authorization_storage_security_evidence());
}

bool run_companion_authorization_storage_self_check() {
    if (companion_authorization_storage_preflight() !=
        CompanionAuthorizationTargetAdmissionError::
            nvs_encryption_not_configured) {
        return false;
    }

    FixedPrf prf{};
    CompanionBondBindingResolver resolver{prf};
    CompanionPrivateBondReference reference{};
    for (std::size_t index = 0; index < reference.value.size(); ++index) {
        reference.value[index] = static_cast<std::uint8_t>(index + 1);
    }
    reference.bond_generation = 1;
    const auto binding = resolver.resolve(reference);
    if (!binding.resolved()) {
        return false;
    }

    MemoryProtectedStore store{};
    DurableCompanionAuthorizationPersistence first_boot{store};
    CompanionAuthorizationAuthority first_authority{first_boot, 100};
    if (!first_authority.restore().accepted()) {
        return false;
    }
    const CompanionPhysicalPresenceInput claim_window{
        CompanionPhysicalAuthorizationAction::claim_owner, binding.token,
        100, 1, 10};
    if (!first_authority.open_physical_window(claim_window).accepted()) {
        return false;
    }
    const CompanionControllerClaim first_claim{
        binding.token, 100, 1, 11, true, true};
    if (!first_authority.claim_owner(first_claim, 11).
            connection_authorized()) {
        return false;
    }

    DurableCompanionAuthorizationPersistence second_boot{store};
    CompanionAuthorizationAuthority second_authority{second_boot, 200};
    if (!second_authority.restore().accepted() ||
        !second_authority.status(20).owner_present ||
        second_authority.status(20).owner_generation != 1) {
        return false;
    }
    const CompanionControllerClaim second_claim{
        binding.token, 200, 1, 21, true, true};
    if (!second_authority.authorize_connection(second_claim, 20).
            connection_authorized() ||
        !second_authority.release_connection(21).accepted()) {
        return false;
    }
    const CompanionPhysicalPresenceInput revoke_window{
        CompanionPhysicalAuthorizationAction::revoke_owner, {}, 200, 1, 21};
    if (!second_authority.open_physical_window(revoke_window).accepted() ||
        !second_authority.revoke_owner(22).accepted()) {
        return false;
    }

    DurableCompanionAuthorizationPersistence third_boot{store};
    CompanionAuthorizationAuthority third_authority{third_boot, 300};
    return third_authority.restore().accepted() &&
           !third_authority.status(30).owner_present &&
           third_authority.status(30).owner_generation == 2 &&
           resolver.resolve(reference).token == binding.token;
}

}  // namespace opentrail::target::heltec_v4_bench
