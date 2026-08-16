#include "companion_authorization_storage.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif

#if __has_include("esp_efuse.h") && __has_include("esp_hmac.h") && __has_include("esp_partition.h")
#define OPENTRAIL_HAS_ESP_SECURITY_PROBE 1
#include "esp_efuse.h"
#include "esp_hmac.h"
#include "esp_partition.h"
#else
#define OPENTRAIL_HAS_ESP_SECURITY_PROBE 0
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

[[maybe_unused]] constexpr char kProtectedNvsPartitionLabel[] = "nvs";

[[maybe_unused]] void secure_zero(void* data, std::size_t size) {
    auto* current = static_cast<volatile std::uint8_t*>(data);
    while (size > 0) {
        *current = 0;
        ++current;
        --size;
    }
}

class EspIdfReadOnlyProbePort final
    : public CompanionAuthorizationStorageReadOnlyProbePort {
public:
    bool protected_nvs_partition_present() override {
#if OPENTRAIL_HAS_ESP_SECURITY_PROBE
        return esp_partition_find_first(
                   ESP_PARTITION_TYPE_DATA,
                   ESP_PARTITION_SUBTYPE_DATA_NVS,
                   kProtectedNvsPartitionLabel) != nullptr;
#else
        return false;
#endif
    }

    bool hmac_key_has_expected_purpose(std::uint8_t key_id) override {
#if OPENTRAIL_HAS_ESP_SECURITY_PROBE
        if (key_id > 5U) {
            return false;
        }
        const auto block = static_cast<esp_efuse_block_t>(
            static_cast<int>(EFUSE_BLK_KEY0) + key_id);
        return esp_efuse_get_key_purpose(block) ==
               ESP_EFUSE_KEY_PURPOSE_HMAC_UP;
#else
        static_cast<void>(key_id);
        return false;
#endif
    }

    bool hmac_key_is_read_protected(std::uint8_t key_id) override {
#if OPENTRAIL_HAS_ESP_SECURITY_PROBE
        if (key_id > 5U) {
            return false;
        }
        const auto block = static_cast<esp_efuse_block_t>(
            static_cast<int>(EFUSE_BLK_KEY0) + key_id);
        return esp_efuse_get_key_dis_read(block);
#else
        static_cast<void>(key_id);
        return false;
#endif
    }

    bool hmac_key_is_operational(std::uint8_t key_id) override {
#if OPENTRAIL_HAS_ESP_SECURITY_PROBE
        if (key_id > 5U) {
            return false;
        }
        std::array<std::uint8_t, 32> message{
            0x4F, 0x70, 0x65, 0x6E, 0x54, 0x72, 0x61, 0x69,
            0x6C, 0x20, 0x4E, 0x56, 0x53, 0x20, 0x70, 0x72,
            0x6F, 0x62, 0x65, 0x20, 0x76, 0x30, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        std::array<std::uint8_t, 32> output{};
        const auto result = esp_hmac_calculate(
            static_cast<hmac_key_id_t>(key_id), message.data(),
            message.size(), output.data());
        secure_zero(message.data(), message.size());
        secure_zero(output.data(), output.size());
        return result == ESP_OK;
#else
        static_cast<void>(key_id);
        return false;
#endif
    }
};

CompanionAuthorizationStorageProbeConfiguration target_configuration() {
    CompanionAuthorizationStorageProbeConfiguration configuration{};
#if defined(CONFIG_NVS_ENCRYPTION) && CONFIG_NVS_ENCRYPTION
    configuration.nvs_encryption_configured = true;
#endif
#if defined(CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC) && CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC
    configuration.nvs_hmac_key_protection_configured = true;
#endif
#if defined(CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID)
    configuration.nvs_hmac_key_id = CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID;
#endif
    return configuration;
}

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

CompanionAuthorizationStorageProbeSnapshot
probe_companion_authorization_storage(
    const CompanionAuthorizationStorageProbeConfiguration& configuration,
    CompanionAuthorizationStorageReadOnlyProbePort& port) {
    CompanionAuthorizationStorageProbeSnapshot snapshot{};
    if (!configuration.nvs_encryption_configured) {
        snapshot.error = CompanionAuthorizationStorageProbeError::
            nvs_encryption_not_configured;
        return snapshot;
    }

    snapshot.protected_nvs_partition_present =
        port.protected_nvs_partition_present();
    if (!snapshot.protected_nvs_partition_present) {
        snapshot.error = CompanionAuthorizationStorageProbeError::
            protected_nvs_partition_missing;
        return snapshot;
    }
    if (!configuration.nvs_hmac_key_protection_configured) {
        snapshot.error = CompanionAuthorizationStorageProbeError::
            nvs_hmac_key_protection_not_configured;
        return snapshot;
    }
    if (configuration.nvs_hmac_key_id < 0 ||
        configuration.nvs_hmac_key_id > 5) {
        snapshot.error = CompanionAuthorizationStorageProbeError::
            nvs_hmac_key_id_not_selected;
        return snapshot;
    }

    snapshot.nvs_hmac_key_id_selected = true;
    const auto key_id =
        static_cast<std::uint8_t>(configuration.nvs_hmac_key_id);
    snapshot.nvs_hmac_key_purpose_verified =
        port.hmac_key_has_expected_purpose(key_id);
    if (!snapshot.nvs_hmac_key_purpose_verified) {
        snapshot.error = CompanionAuthorizationStorageProbeError::
            nvs_hmac_key_purpose_mismatch;
        return snapshot;
    }
    snapshot.nvs_hmac_key_read_protected =
        port.hmac_key_is_read_protected(key_id);
    if (!snapshot.nvs_hmac_key_read_protected) {
        snapshot.error = CompanionAuthorizationStorageProbeError::
            nvs_hmac_key_not_read_protected;
        return snapshot;
    }
    snapshot.nvs_hmac_key_operational =
        port.hmac_key_is_operational(key_id);
    snapshot.error = snapshot.nvs_hmac_key_operational
                         ? CompanionAuthorizationStorageProbeError::none
                         : CompanionAuthorizationStorageProbeError::
                               nvs_hmac_key_unusable;
    return snapshot;
}

CompanionAuthorizationStorageProbeSnapshot
companion_authorization_storage_probe() {
    EspIdfReadOnlyProbePort port{};
    return probe_companion_authorization_storage(target_configuration(), port);
}

CompanionAuthorizationTargetSecurityEvidence
companion_authorization_storage_security_evidence() {
    EspIdfReadOnlyProbePort port{};
    return companion_authorization_storage_security_evidence(
        target_configuration(), port);
}

CompanionAuthorizationTargetSecurityEvidence
companion_authorization_storage_security_evidence(
    const CompanionAuthorizationStorageProbeConfiguration& configuration,
    CompanionAuthorizationStorageReadOnlyProbePort& port) {
    const auto probe =
        probe_companion_authorization_storage(configuration, port);
    CompanionAuthorizationTargetSecurityEvidence evidence{};
    evidence.nvs_encryption_configured =
        configuration.nvs_encryption_configured;
    evidence.nvs_hmac_key_protection_configured =
        configuration.nvs_hmac_key_protection_configured;
    // This probe deliberately does not initialize NVS. Partition presence is
    // not secure-initialization evidence, so this remains false.
    evidence.protected_nvs_initialized_and_verified = false;
    evidence.nvs_hmac_key_provisioned_and_usable = probe.accepted();
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
