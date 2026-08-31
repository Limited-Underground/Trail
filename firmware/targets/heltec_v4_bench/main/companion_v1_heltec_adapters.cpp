#include "companion_v1_heltec_adapters.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "psa/crypto.h"

namespace opentrail::targets::heltec_v4_bench {
namespace {

constexpr std::size_t kSha256Bytes = 32;
constexpr char kBondReferenceDomain[] = "OpenTrail/V1/bond-ref";
constexpr std::uint8_t kBondReferenceVersion = 1;
constexpr std::size_t kMaximumBondReferences = 2;

static_assert(sizeof(kBondReferenceDomain) - 1 == 21);

class ScopedOperation {
public:
    explicit ScopedOperation(bool& active) : active_(active) { active_ = true; }
    ~ScopedOperation() { active_ = false; }

    ScopedOperation(const ScopedOperation&) = delete;
    ScopedOperation& operator=(const ScopedOperation&) = delete;

private:
    bool& active_;
};

void secure_clear(void* data, std::size_t size) {
    volatile std::uint8_t* cursor =
        static_cast<volatile std::uint8_t*>(data);
    while (size-- != 0) {
        *cursor++ = 0;
    }
}

std::uint64_t read_u64_be(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8U) | input[index];
    }
    return value;
}

std::uint32_t read_u32_be(const std::uint8_t* input) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8U) | input[index];
    }
    return value;
}

companion::CompanionV1OwnerStorageSnapshot storage_failure(
    companion::CompanionV1OwnerStorageError error,
    bool record_present = false) {
    companion::CompanionV1OwnerStorageSnapshot result{};
    result.error = error;
    result.record_present = record_present;
    return result;
}

companion::CompanionV1OwnerStorageError initial_nvs_error(esp_err_t error) {
    return error == ESP_ERR_NVS_INVALID_HANDLE
               ? companion::CompanionV1OwnerStorageError::not_ready
               : companion::CompanionV1OwnerStorageError::failed;
}

companion::CompanionGattTrustedBindingResult binding_failure(
    companion::CompanionGattTrustedBindingError error) {
    return {error, {}, 0};
}

enum class PrivateBondReferenceError : std::uint8_t {
    none = 0,
    not_ready,
    failed,
};

struct PrivateBondReferenceResult {
    PrivateBondReferenceError error{PrivateBondReferenceError::not_ready};
    companion::CompanionBondIdentityToken reference{};
};

bool exact_authenticated_sc_ltk(const ble_store_value_sec& value) {
    return value.ltk_present != 0 && value.authenticated != 0 &&
           value.sc != 0 &&
           value.key_size == companion::kCompanionGattMinimumSecurityKeyBytes;
}

PrivateBondReferenceResult derive_private_reference(
    const ble_addr_t& private_peer_identity) {
    ble_store_key_sec key{};
    key.peer_addr = private_peer_identity;

    ble_store_value_sec security_record{};
    const int read_error = ble_store_read_our_sec(&key, &security_record);
    if (read_error == BLE_HS_ENOENT) {
        return {PrivateBondReferenceError::not_ready, {}};
    }
    if (read_error != 0 ||
        !exact_authenticated_sc_ltk(security_record)) {
        secure_clear(&security_record, sizeof(security_record));
        return {PrivateBondReferenceError::failed, {}};
    }

    std::array<std::uint8_t,
               sizeof(kBondReferenceDomain) - 1 + 1 +
                   companion::kCompanionGattMinimumSecurityKeyBytes>
        derivation{};
    std::copy_n(reinterpret_cast<const std::uint8_t*>(kBondReferenceDomain),
                sizeof(kBondReferenceDomain) - 1, derivation.begin());
    derivation[sizeof(kBondReferenceDomain) - 1] = kBondReferenceVersion;
    std::copy_n(
        security_record.ltk,
        companion::kCompanionGattMinimumSecurityKeyBytes,
        derivation.begin() + sizeof(kBondReferenceDomain));

    std::array<std::uint8_t, kSha256Bytes> digest{};
    std::size_t digest_size = 0;
    const psa_status_t init_error = psa_crypto_init();
    const psa_status_t hash_error =
        init_error == PSA_SUCCESS
            ? psa_hash_compute(PSA_ALG_SHA_256, derivation.data(),
                               derivation.size(), digest.data(), digest.size(),
                               &digest_size)
            : init_error;
    secure_clear(&security_record, sizeof(security_record));
    secure_clear(derivation.data(), derivation.size());
    if (hash_error != PSA_SUCCESS || digest_size != digest.size()) {
        secure_clear(digest.data(), digest.size());
        return {PrivateBondReferenceError::failed, {}};
    }

    const companion::CompanionBondIdentityToken reference{
        read_u64_be(digest.data()),
        read_u64_be(digest.data() + sizeof(std::uint64_t))};
    secure_clear(digest.data(), digest.size());
    if (!companion::valid_bond_identity(reference)) {
        return {PrivateBondReferenceError::failed, {}};
    }
    return {PrivateBondReferenceError::none, reference};
}

companion::CompanionV1BondInventorySnapshot nimble_bond_snapshot() {
    companion::CompanionV1BondInventorySnapshot result{};
    std::array<ble_addr_t, kMaximumBondReferences> private_peer_identities{};
    int bond_count = 0;
    const int inventory_error = ble_store_util_bonded_peers(
        private_peer_identities.data(), &bond_count,
        static_cast<int>(private_peer_identities.size()));
    if (inventory_error == BLE_HS_ENOTSUP) {
        result.error = companion::CompanionV1BondInventoryError::not_ready;
        return result;
    }
    if (inventory_error != 0 || bond_count < 0 ||
        bond_count > static_cast<int>(private_peer_identities.size())) {
        result.error = companion::CompanionV1BondInventoryError::failed;
        return result;
    }

    for (int index = 0; index < bond_count; ++index) {
        const auto derived = derive_private_reference(
            private_peer_identities[static_cast<std::size_t>(index)]);
        if (derived.error == PrivateBondReferenceError::not_ready) {
            result = {};
            result.error = companion::CompanionV1BondInventoryError::not_ready;
            return result;
        }
        if (derived.error != PrivateBondReferenceError::none) {
            result = {};
            result.error = companion::CompanionV1BondInventoryError::failed;
            return result;
        }
        for (int previous = 0; previous < index; ++previous) {
            if (result.private_references[static_cast<std::size_t>(previous)] ==
                derived.reference) {
                result = {};
                result.error =
                    companion::CompanionV1BondInventoryError::failed;
                return result;
            }
        }
        result.private_references[static_cast<std::size_t>(index)] =
            derived.reference;
    }
    result.error = companion::CompanionV1BondInventoryError::none;
    result.bond_count = static_cast<std::uint8_t>(bond_count);
    return result;
}

PrivateBondReferenceResult resolve_live_private_reference(
    std::uint16_t connection_handle) {
    ble_gap_conn_desc connection{};
    if (ble_gap_conn_find(connection_handle, &connection) != 0) {
        return {PrivateBondReferenceError::failed, {}};
    }
    if (connection.sec_state.encrypted == 0 ||
        connection.sec_state.authenticated == 0 ||
        connection.sec_state.bonded == 0 ||
        connection.sec_state.key_size !=
            companion::kCompanionGattMinimumSecurityKeyBytes) {
        return {PrivateBondReferenceError::failed, {}};
    }

    const auto live = derive_private_reference(connection.peer_id_addr);
    if (live.error != PrivateBondReferenceError::none) {
        return live;
    }
    const auto inventory = nimble_bond_snapshot();
    if (inventory.error == companion::CompanionV1BondInventoryError::not_ready) {
        return {PrivateBondReferenceError::not_ready, {}};
    }
    if (inventory.error != companion::CompanionV1BondInventoryError::none) {
        return {PrivateBondReferenceError::failed, {}};
    }

    for (std::uint8_t index = 0; index < inventory.bond_count; ++index) {
        if (inventory.private_references[index] == live.reference) {
            return live;
        }
    }
    return {PrivateBondReferenceError::failed, {}};
}

void force_nonzero(std::uint64_t& value) {
    if (value == 0) {
        value = 1;
    }
}

void force_nonzero(std::uint32_t& value) {
    if (value == 0) {
        value = 1;
    }
}

}  // namespace

HeltecV4CompanionV1OwnerStorage::HeltecV4CompanionV1OwnerStorage() {
    if (nvs_open(kCompanionV1OwnerNvsNamespace, NVS_READWRITE, &handle_) !=
        ESP_OK) {
        handle_ = 0;
    }
}

HeltecV4CompanionV1OwnerStorage::~HeltecV4CompanionV1OwnerStorage() {
    if (handle_ != 0) {
        nvs_close(handle_);
    }
}

companion::CompanionV1OwnerStorageSnapshot
HeltecV4CompanionV1OwnerStorage::load() {
    if (handle_ == 0) {
        return storage_failure(
            companion::CompanionV1OwnerStorageError::not_ready);
    }

    std::size_t stored_size = 0;
    const esp_err_t size_error =
        nvs_get_blob(handle_, kCompanionV1OwnerNvsKey, nullptr, &stored_size);
    if (size_error == ESP_ERR_NVS_NOT_FOUND) {
        companion::CompanionV1OwnerStorageSnapshot absent{};
        absent.error = companion::CompanionV1OwnerStorageError::none;
        return absent;
    }
    if (size_error != ESP_OK) {
        return storage_failure(initial_nvs_error(size_error));
    }
    if (stored_size != companion::kCompanionV1OwnerRecordBytes) {
        return storage_failure(companion::CompanionV1OwnerStorageError::failed,
                               true);
    }

    companion::CompanionV1OwnerStorageSnapshot loaded{};
    loaded.record_present = true;
    std::size_t read_size = loaded.record.size();
    const esp_err_t read_error = nvs_get_blob(
        handle_, kCompanionV1OwnerNvsKey, loaded.record.data(), &read_size);
    if (read_error != ESP_OK || read_size != loaded.record.size()) {
        return storage_failure(
            read_error == ESP_ERR_NVS_INVALID_HANDLE
                ? companion::CompanionV1OwnerStorageError::not_ready
                : companion::CompanionV1OwnerStorageError::failed,
            true);
    }
    loaded.error = companion::CompanionV1OwnerStorageError::none;
    return loaded;
}

companion::CompanionV1OwnerStorageSnapshot
HeltecV4CompanionV1OwnerStorage::commit_absent_and_readback(
    const std::array<std::uint8_t,
                     companion::kCompanionV1OwnerRecordBytes>& record) {
    if (handle_ == 0) {
        return storage_failure(
            companion::CompanionV1OwnerStorageError::not_ready);
    }

    std::size_t existing_size = 0;
    const esp_err_t presence_error = nvs_get_blob(
        handle_, kCompanionV1OwnerNvsKey, nullptr, &existing_size);
    if (presence_error == ESP_OK) {
        return storage_failure(
            companion::CompanionV1OwnerStorageError::conflict, true);
    }
    if (presence_error != ESP_ERR_NVS_NOT_FOUND) {
        return storage_failure(initial_nvs_error(presence_error));
    }

    // Every failure from this point is uncertain because staged or committed
    // mutation may have occurred. The adapter never erases or retries it.
    if (nvs_set_blob(handle_, kCompanionV1OwnerNvsKey, record.data(),
                     record.size()) != ESP_OK) {
        return storage_failure(
            companion::CompanionV1OwnerStorageError::uncertain);
    }
    if (nvs_commit(handle_) != ESP_OK) {
        return storage_failure(
            companion::CompanionV1OwnerStorageError::uncertain);
    }

    companion::CompanionV1OwnerStorageSnapshot committed{};
    committed.record_present = true;
    std::size_t read_size = committed.record.size();
    if (nvs_get_blob(handle_, kCompanionV1OwnerNvsKey,
                     committed.record.data(), &read_size) != ESP_OK ||
        read_size != committed.record.size() || committed.record != record) {
        return storage_failure(
            companion::CompanionV1OwnerStorageError::uncertain, true);
    }
    committed.error = companion::CompanionV1OwnerStorageError::none;
    return committed;
}

HeltecV4CompanionV1NimbleBondAdapter::
    HeltecV4CompanionV1NimbleBondAdapter(
        security::SecureRandomSource& random)
    : random_(random) {}

companion::CompanionV1BondInventorySnapshot
HeltecV4CompanionV1NimbleBondAdapter::snapshot() {
    return nimble_bond_snapshot();
}

companion::CompanionGattTrustedBindingResult
HeltecV4CompanionV1NimbleBondAdapter::resolve(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation) {
    if (operation_active_ ||
        connection_handle == companion::kCompanionGattInvalidConnectionHandle ||
        transport_generation == 0) {
        return binding_failure(
            companion::CompanionGattTrustedBindingError::failed);
    }

    ScopedOperation operation(operation_active_);
    if (context_seen_) {
        if (transport_generation < transport_generation_ ||
            (transport_generation == transport_generation_ &&
             connection_handle != connection_handle_)) {
            return binding_failure(
                companion::CompanionGattTrustedBindingError::failed);
        }
        if (transport_generation == transport_generation_ && cached_) {
            return cached_result_;
        }
    }
    if (!context_seen_ || transport_generation > transport_generation_) {
        context_seen_ = true;
        reference_seen_ = false;
        cached_ = false;
        reference_ = {};
        cached_result_ = {};
        connection_handle_ = connection_handle;
        transport_generation_ = transport_generation;
    }

    const auto live = resolve_live_private_reference(connection_handle_);
    if (live.error == PrivateBondReferenceError::not_ready) {
        return binding_failure(
            companion::CompanionGattTrustedBindingError::not_ready);
    }
    if (live.error != PrivateBondReferenceError::none) {
        return binding_failure(
            companion::CompanionGattTrustedBindingError::failed);
    }
    if (reference_seen_ && live.reference != reference_) {
        return binding_failure(
            companion::CompanionGattTrustedBindingError::failed);
    }
    if (!reference_seen_) {
        reference_ = live.reference;
        reference_seen_ = true;
    }

    if (next_session_challenge_ == 0) {
        return binding_failure(
            companion::CompanionGattTrustedBindingError::failed);
    }

    if (random_.state() == security::EntropyState::not_ready) {
        return binding_failure(
            companion::CompanionGattTrustedBindingError::not_ready);
    }
    if (random_.state() != security::EntropyState::ready) {
        return binding_failure(
            companion::CompanionGattTrustedBindingError::failed);
    }

    constexpr std::size_t kBootBytes = sizeof(std::uint64_t);
    constexpr std::size_t kPrivateConnectionBytes =
        sizeof(std::uint64_t) + sizeof(std::uint32_t);
    std::array<std::uint8_t, kBootBytes + kPrivateConnectionBytes>
        random_bytes{};
    const std::size_t offset = boot_challenge_ready_ ? kBootBytes : 0;
    const auto fill = random_.fill(random_bytes.data() + offset,
                                   random_bytes.size() - offset);
    if (!fill.ok() || fill.bytes_written != random_bytes.size() - offset) {
        secure_clear(random_bytes.data(), random_bytes.size());
        return binding_failure(
            fill.error == security::RandomFillError::entropy_not_ready
                ? companion::CompanionGattTrustedBindingError::not_ready
                : companion::CompanionGattTrustedBindingError::failed);
    }

    if (!boot_challenge_ready_) {
        boot_challenge_ = read_u64_be(random_bytes.data());
        force_nonzero(boot_challenge_);
        boot_challenge_ready_ = true;
    }
    std::uint64_t controller_binding =
        read_u64_be(random_bytes.data() + kBootBytes);
    std::uint32_t provisional_session_nonce =
        read_u32_be(random_bytes.data() + kBootBytes + sizeof(std::uint64_t));
    secure_clear(random_bytes.data(), random_bytes.size());
    force_nonzero(controller_binding);
    force_nonzero(provisional_session_nonce);

    const std::uint64_t session_challenge = next_session_challenge_++;

    companion::CompanionControllerClaim claim{};
    claim.bond_identity = live.reference;
    claim.boot_challenge = boot_challenge_;
    claim.session_challenge = session_challenge;
    claim.controller_binding = controller_binding;
    cached_result_ = {companion::CompanionGattTrustedBindingError::none, claim,
                      provisional_session_nonce};
    cached_ = true;
    return cached_result_;
}

}  // namespace opentrail::targets::heltec_v4_bench
