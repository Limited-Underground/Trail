#include "heltec_v4_factory_reset_storage.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp_partition.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "store/config/ble_store_config.h"

#include "companion_v1_heltec_adapters.hpp"

namespace opentrail::targets::heltec_v4_bench {
namespace {

using companion::DeviceFactoryResetAbsenceSnapshot;
using companion::DeviceFactoryResetMarkerSnapshot;
using companion::DeviceFactoryResetMarkerState;
using companion::DeviceFactoryResetPortError;
using companion::DeviceFactoryResetReceiptConsumeSnapshot;

constexpr std::size_t kEraseVerificationChunkBytes = 256;

struct DomainCheck {
    DeviceFactoryResetPortError error{DeviceFactoryResetPortError::failed};
    bool absent{false};
};

using FactoryResetRecord =
    std::array<std::uint8_t, kHeltecV4FactoryResetRecordBytes>;

constexpr std::array<std::uint8_t, 4> kFactoryResetRecordMagic{
    'O', 'T', 'F', 'R'};

DeviceFactoryResetPortError classify_nvs_access_error(esp_err_t error) {
    return error == ESP_ERR_NVS_NOT_INITIALIZED ||
                   error == ESP_ERR_NVS_INVALID_HANDLE
               ? DeviceFactoryResetPortError::not_ready
               : DeviceFactoryResetPortError::failed;
}

struct KeyPresence {
    DeviceFactoryResetPortError error{DeviceFactoryResetPortError::failed};
    bool present{false};
};

KeyPresence find_key(nvs_handle_t handle, const char* key) {
    nvs_type_t type = NVS_TYPE_ANY;
    const esp_err_t result = nvs_find_key(handle, key, &type);
    if (result == ESP_OK) {
        return {DeviceFactoryResetPortError::none, true};
    }
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return {DeviceFactoryResetPortError::none, false};
    }
    return {classify_nvs_access_error(result), false};
}

FactoryResetRecord encode_marker_record(
    DeviceFactoryResetMarkerState state,
    std::uint64_t receipt) {
    FactoryResetRecord record{};
    for (std::size_t index = 0; index < kFactoryResetRecordMagic.size();
         ++index) {
        record[index] = kFactoryResetRecordMagic[index];
    }
    record[4] = kHeltecV4FactoryResetRecordVersion;
    record[5] = state == DeviceFactoryResetMarkerState::intent_committed
                    ? kHeltecV4FactoryResetMarkerValue
                    : kHeltecV4FactoryResetReceiptPendingValue;
    for (std::size_t index = 0; index < sizeof(receipt); ++index) {
        record[8 + index] = static_cast<std::uint8_t>(
            (receipt >> (index * 8U)) & UINT64_C(0xff));
    }
    return record;
}

DeviceFactoryResetMarkerSnapshot decode_marker_record(
    const FactoryResetRecord& record) {
    for (std::size_t index = 0; index < kFactoryResetRecordMagic.size();
         ++index) {
        if (record[index] != kFactoryResetRecordMagic[index]) {
            return {DeviceFactoryResetPortError::none,
                    DeviceFactoryResetMarkerState::invalid, 0};
        }
    }
    if (record[4] != kHeltecV4FactoryResetRecordVersion ||
        record[6] != 0 || record[7] != 0) {
        return {DeviceFactoryResetPortError::none,
                DeviceFactoryResetMarkerState::invalid, 0};
    }

    std::uint64_t receipt = 0;
    for (std::size_t index = 0; index < sizeof(receipt); ++index) {
        receipt |= static_cast<std::uint64_t>(record[8 + index])
                   << (index * 8U);
    }
    if (record[5] == kHeltecV4FactoryResetMarkerValue) {
        return {DeviceFactoryResetPortError::none,
                DeviceFactoryResetMarkerState::intent_committed, receipt};
    }
    if (record[5] == kHeltecV4FactoryResetReceiptPendingValue &&
        receipt != 0) {
        return {DeviceFactoryResetPortError::none,
                DeviceFactoryResetMarkerState::receipt_pending, receipt};
    }
    return {DeviceFactoryResetPortError::none,
            DeviceFactoryResetMarkerState::invalid, 0};
}

esp_err_t set_marker_record(
    nvs_handle_t handle,
    DeviceFactoryResetMarkerState state,
    std::uint64_t receipt) {
    const auto record = encode_marker_record(state, receipt);
    return nvs_set_blob(handle, kHeltecV4FactoryResetRecordKey,
                        record.data(), record.size());
}

DeviceFactoryResetMarkerSnapshot read_marker(nvs_handle_t handle) {
    if (handle == 0) {
        return {DeviceFactoryResetPortError::not_ready,
                DeviceFactoryResetMarkerState::invalid, 0};
    }

    // The split-key form existed only during development. It cannot be
    // migrated safely because a power loss may have persisted either half.
    // Detect either legacy key by type and fail closed without mutating it.
    const auto legacy_marker =
        find_key(handle, kHeltecV4FactoryResetLegacyMarkerKey);
    const auto legacy_receipt =
        find_key(handle, kHeltecV4FactoryResetLegacyReceiptKey);
    if (legacy_marker.error != DeviceFactoryResetPortError::none ||
        legacy_receipt.error != DeviceFactoryResetPortError::none) {
        return {legacy_marker.error != DeviceFactoryResetPortError::none
                    ? legacy_marker.error
                    : legacy_receipt.error,
                DeviceFactoryResetMarkerState::invalid, 0};
    }
    if (legacy_marker.present || legacy_receipt.present) {
        return {DeviceFactoryResetPortError::failed,
                DeviceFactoryResetMarkerState::invalid, 0};
    }

    std::size_t record_bytes = 0;
    const esp_err_t size_result = nvs_get_blob(
        handle, kHeltecV4FactoryResetRecordKey, nullptr, &record_bytes);
    if (size_result == ESP_ERR_NVS_NOT_FOUND) {
        return {DeviceFactoryResetPortError::none,
                DeviceFactoryResetMarkerState::absent, 0};
    }
    if (size_result != ESP_OK) {
        return {classify_nvs_access_error(size_result),
                DeviceFactoryResetMarkerState::invalid, 0};
    }
    if (record_bytes != kHeltecV4FactoryResetRecordBytes) {
        return {DeviceFactoryResetPortError::none,
                DeviceFactoryResetMarkerState::invalid, 0};
    }

    FactoryResetRecord record{};
    const esp_err_t read_result = nvs_get_blob(
        handle, kHeltecV4FactoryResetRecordKey, record.data(), &record_bytes);
    if (read_result != ESP_OK) {
        return {classify_nvs_access_error(read_result),
                DeviceFactoryResetMarkerState::invalid, 0};
    }
    if (record_bytes != record.size()) {
        return {DeviceFactoryResetPortError::none,
                DeviceFactoryResetMarkerState::invalid, 0};
    }
    return decode_marker_record(record);
}

DeviceFactoryResetMarkerSnapshot read_isolated_reset_marker() {
    nvs_handle_t handle = 0;
    const esp_err_t opened = nvs_open(
        kHeltecV4FactoryResetMarkerNamespace, NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) {
        return {DeviceFactoryResetPortError::none,
                DeviceFactoryResetMarkerState::absent, 0};
    }
    if (opened != ESP_OK) {
        return {classify_nvs_access_error(opened),
                DeviceFactoryResetMarkerState::invalid, 0};
    }
    const auto marker = read_marker(handle);
    nvs_close(handle);
    return marker;
}

DomainCheck inspect_owner_namespace() {
    nvs_handle_t handle = 0;
    const esp_err_t opened = nvs_open(
        kCompanionV1OwnerNvsNamespace, NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) {
        return {DeviceFactoryResetPortError::none, true};
    }
    if (opened != ESP_OK) {
        return {classify_nvs_access_error(opened), false};
    }

    std::size_t used_entries = 0;
    const esp_err_t inspected =
        nvs_get_used_entry_count(handle, &used_entries);
    nvs_close(handle);
    if (inspected != ESP_OK) {
        return {classify_nvs_access_error(inspected), false};
    }
    return {DeviceFactoryResetPortError::none, used_entries == 0};
}

DomainCheck erase_owner_namespace_and_verify() {
    nvs_handle_t handle = 0;
    const esp_err_t opened = nvs_open(
        kCompanionV1OwnerNvsNamespace, NVS_READWRITE, &handle);
    if (opened != ESP_OK && opened != ESP_ERR_NVS_NOT_FOUND) {
        return {classify_nvs_access_error(opened), false};
    }
    if (opened == ESP_OK) {
        const esp_err_t erased = nvs_erase_all(handle);
        if (erased != ESP_OK) {
            nvs_close(handle);
            return {DeviceFactoryResetPortError::failed, false};
        }
        const esp_err_t committed = nvs_commit(handle);
        nvs_close(handle);
        if (committed != ESP_OK) {
            return {DeviceFactoryResetPortError::failed, false};
        }
    }
    return inspect_owner_namespace();
}

const esp_partition_t* exact_state_partition() {
    const auto* partition = esp_partition_find_first(
        static_cast<esp_partition_type_t>(
            kHeltecV4FactoryResetStatePartitionType),
        static_cast<esp_partition_subtype_t>(
            kHeltecV4FactoryResetStatePartitionSubtype),
        kHeltecV4FactoryResetStatePartitionLabel);
    if (partition == nullptr ||
        partition->address != kHeltecV4FactoryResetStatePartitionAddress ||
        partition->size != kHeltecV4FactoryResetStatePartitionBytes ||
        partition->encrypted) {
        return nullptr;
    }
    return partition;
}

DomainCheck inspect_state_partition() {
    const auto* partition = exact_state_partition();
    if (partition == nullptr) {
        return {DeviceFactoryResetPortError::failed, false};
    }

    std::array<std::uint8_t, kEraseVerificationChunkBytes> bytes{};
    const std::size_t partition_size =
        static_cast<std::size_t>(partition->size);
    for (std::size_t offset = 0; offset < partition_size;
         offset += bytes.size()) {
        const std::size_t remaining = partition_size - offset;
        const std::size_t length =
            remaining < bytes.size() ? remaining : bytes.size();
        if (esp_partition_read(partition, offset, bytes.data(), length) !=
            ESP_OK) {
            return {DeviceFactoryResetPortError::failed, false};
        }
        for (std::size_t index = 0; index < length; ++index) {
            if (bytes[index] != 0xff) {
                return {DeviceFactoryResetPortError::none, false};
            }
        }
    }
    return {DeviceFactoryResetPortError::none, true};
}

bool exact_nimble_store_config_installed() {
    return ble_hs_cfg.store_read_cb == ble_store_config_read &&
           ble_hs_cfg.store_write_cb == ble_store_config_write &&
           ble_hs_cfg.store_delete_cb == ble_store_config_delete;
}

int observe_nimble_store_entry(int, union ble_store_value*, void* cookie) {
    *static_cast<bool*>(cookie) = true;
    return 1;
}

DeviceFactoryResetAbsenceSnapshot inspect_nimble_store_empty() {
    if (!exact_nimble_store_config_installed()) {
        return {DeviceFactoryResetPortError::not_ready, false};
    }

    // The local IRK belongs to this device and NimBLE recreates it during
    // normal unowned startup. It is not a peer bond and must not make an
    // otherwise empty phone-bond domain appear occupied after reset.
    constexpr int kPeerAssociatedStoredObjectTypes[] = {
        BLE_STORE_OBJ_TYPE_OUR_SEC,
        BLE_STORE_OBJ_TYPE_PEER_SEC,
        BLE_STORE_OBJ_TYPE_CCCD,
        BLE_STORE_OBJ_TYPE_CSFC,
        BLE_STORE_OBJ_TYPE_PEER_ADDR,
#if MYNEWT_VAL(ENC_ADV_DATA)
        BLE_STORE_OBJ_TYPE_ENC_ADV_DATA,
#endif
    };
    for (const int object_type : kPeerAssociatedStoredObjectTypes) {
        bool entry_found = false;
        const int result = ble_store_iterate(
            object_type, observe_nimble_store_entry, &entry_found);
        if (result != 0) {
            return {DeviceFactoryResetPortError::failed, false};
        }
        if (entry_found) {
            return {DeviceFactoryResetPortError::none, false};
        }
    }
    return {DeviceFactoryResetPortError::none, true};
}

}  // namespace

HeltecV4FactoryResetMarkerStorage::HeltecV4FactoryResetMarkerStorage() {
    if (nvs_open(kHeltecV4FactoryResetMarkerNamespace,
                 NVS_READWRITE, &handle_) != ESP_OK) {
        handle_ = 0;
    }
}

HeltecV4FactoryResetMarkerStorage::~HeltecV4FactoryResetMarkerStorage() {
    if (handle_ != 0) {
        nvs_close(handle_);
    }
}

DeviceFactoryResetMarkerSnapshot
HeltecV4FactoryResetMarkerStorage::load() {
    return read_marker(handle_);
}

DeviceFactoryResetMarkerSnapshot
HeltecV4FactoryResetMarkerStorage::commit_intent_and_readback(
    std::uint64_t reset_receipt) {
    const auto before = read_marker(handle_);
    if (before.error != DeviceFactoryResetPortError::none) {
        return before;
    }
    if (before.state == DeviceFactoryResetMarkerState::intent_committed) {
        return before.reset_receipt == reset_receipt
                   ? before
                   : DeviceFactoryResetMarkerSnapshot{
                         DeviceFactoryResetPortError::failed,
                         DeviceFactoryResetMarkerState::invalid, 0};
    }
    if (before.state != DeviceFactoryResetMarkerState::absent) {
        return {DeviceFactoryResetPortError::failed,
                DeviceFactoryResetMarkerState::invalid, 0};
    }

    // From this single-record set onward, failure is uncertain. NVS can recover
    // only the complete old record or complete new record after power loss;
    // the executor must reconcile before any user-domain access in-process.
    if (set_marker_record(handle_,
                          DeviceFactoryResetMarkerState::intent_committed,
                          reset_receipt) != ESP_OK) {
        return {DeviceFactoryResetPortError::uncertain,
                DeviceFactoryResetMarkerState::invalid, 0};
    }
    if (nvs_commit(handle_) != ESP_OK) {
        return {DeviceFactoryResetPortError::uncertain,
                DeviceFactoryResetMarkerState::invalid, 0};
    }
    const auto after = read_marker(handle_);
    if (after.error != DeviceFactoryResetPortError::none ||
        after.state != DeviceFactoryResetMarkerState::intent_committed ||
        after.reset_receipt != reset_receipt) {
        return {DeviceFactoryResetPortError::uncertain, after.state,
                after.reset_receipt};
    }
    return after;
}

DeviceFactoryResetMarkerSnapshot
HeltecV4FactoryResetMarkerStorage::complete_cleanup_and_readback() {
    const auto before = read_marker(handle_);
    if (before.error != DeviceFactoryResetPortError::none) {
        return before;
    }
    if (before.state == DeviceFactoryResetMarkerState::receipt_pending) {
        return before;
    }
    if (before.state != DeviceFactoryResetMarkerState::intent_committed) {
        return {DeviceFactoryResetPortError::failed,
                DeviceFactoryResetMarkerState::invalid, 0};
    }

    const bool receipt_required = before.reset_receipt != 0;
    const esp_err_t changed = receipt_required
        ? set_marker_record(
              handle_, DeviceFactoryResetMarkerState::receipt_pending,
              before.reset_receipt)
        : nvs_erase_key(handle_, kHeltecV4FactoryResetRecordKey);
    if (changed != ESP_OK) {
        return {DeviceFactoryResetPortError::uncertain,
                DeviceFactoryResetMarkerState::invalid, 0};
    }
    if (nvs_commit(handle_) != ESP_OK) {
        return {DeviceFactoryResetPortError::uncertain,
                DeviceFactoryResetMarkerState::invalid, 0};
    }
    const auto after = read_marker(handle_);
    if (after.error != DeviceFactoryResetPortError::none ||
        after.state != (receipt_required
                            ? DeviceFactoryResetMarkerState::receipt_pending
                            : DeviceFactoryResetMarkerState::absent) ||
        after.reset_receipt != before.reset_receipt) {
        return {DeviceFactoryResetPortError::uncertain, after.state,
                after.reset_receipt};
    }
    return after;
}

DeviceFactoryResetReceiptConsumeSnapshot
HeltecV4FactoryResetMarkerStorage::
consume_completion_receipt_and_readback() {
    const auto before = read_marker(handle_);
    if (before.error != DeviceFactoryResetPortError::none) {
        return {before.error, 0, false};
    }
    if (before.state != DeviceFactoryResetMarkerState::receipt_pending ||
        before.reset_receipt == 0) {
        return {DeviceFactoryResetPortError::failed, 0, false};
    }

    if (nvs_erase_key(handle_, kHeltecV4FactoryResetRecordKey) != ESP_OK ||
        nvs_commit(handle_) != ESP_OK) {
        return {DeviceFactoryResetPortError::uncertain, 0, false};
    }
    const auto after = read_marker(handle_);
    if (after.error != DeviceFactoryResetPortError::none ||
        after.state != DeviceFactoryResetMarkerState::absent ||
        after.reset_receipt != 0) {
        return {DeviceFactoryResetPortError::uncertain, 0, false};
    }
    return {DeviceFactoryResetPortError::none, before.reset_receipt, true};
}

DeviceFactoryResetAbsenceSnapshot
HeltecV4FactoryResetUserDomainStorage::inspect_absence() {
    static_assert(!kHeltecV4FactoryResetHasActiveMapPackageStorage);

    const auto owner = inspect_owner_namespace();
    if (owner.error != DeviceFactoryResetPortError::none) {
        return {owner.error, false};
    }
    const auto state = inspect_state_partition();
    if (state.error != DeviceFactoryResetPortError::none) {
        return {state.error, false};
    }
    return {DeviceFactoryResetPortError::none,
            owner.absent && state.absent};
}

DeviceFactoryResetAbsenceSnapshot
HeltecV4FactoryResetUserDomainStorage::erase_all_and_verify_absent() {
    static_assert(!kHeltecV4FactoryResetHasActiveMapPackageStorage);

    const auto owner = erase_owner_namespace_and_verify();
    if (owner.error != DeviceFactoryResetPortError::none) {
        return {owner.error, false};
    }
    if (!owner.absent) {
        return {DeviceFactoryResetPortError::none, false};
    }

    const auto* partition = exact_state_partition();
    if (partition == nullptr ||
        esp_partition_erase_range(partition, 0, partition->size) != ESP_OK) {
        return {DeviceFactoryResetPortError::failed, false};
    }
    const auto erased_state = inspect_state_partition();
    if (erased_state.error != DeviceFactoryResetPortError::none ||
        !erased_state.absent) {
        return {erased_state.error, false};
    }
    return inspect_absence();
}

DeviceFactoryResetAbsenceSnapshot
HeltecV4FactoryResetNimbleBondStorage::inspect_empty() {
    if (!store_access_ready_.load(std::memory_order_acquire)) {
        return {DeviceFactoryResetPortError::not_ready, false};
    }
    return inspect_nimble_store_empty();
}

void HeltecV4FactoryResetNimbleBondStorage::set_store_access_ready(
    bool ready) {
    store_access_ready_.store(ready, std::memory_order_release);
}

DeviceFactoryResetAbsenceSnapshot
HeltecV4FactoryResetNimbleBondStorage::erase_all_and_verify_empty() {
    if (!store_access_ready_.load(std::memory_order_acquire) ||
        !exact_nimble_store_config_installed()) {
        return {DeviceFactoryResetPortError::not_ready, false};
    }
    const auto before_marker = read_isolated_reset_marker();
    if (before_marker.error != DeviceFactoryResetPortError::none) {
        return {before_marker.error, false};
    }
    if (before_marker.state !=
            DeviceFactoryResetMarkerState::intent_committed &&
        before_marker.state !=
            DeviceFactoryResetMarkerState::receipt_pending) {
        return {DeviceFactoryResetPortError::failed, false};
    }
    if (ble_store_clear() != 0) {
        return {DeviceFactoryResetPortError::failed, false};
    }
    // Bond deletion is a distinct NVS domain. Re-read the reset namespace
    // through a fresh handle so a broad or misconfigured clear can never be
    // mistaken for successful reset completion.
    const auto after_marker = read_isolated_reset_marker();
    if (after_marker.error != DeviceFactoryResetPortError::none ||
        after_marker.state != before_marker.state ||
        after_marker.reset_receipt != before_marker.reset_receipt) {
        return {DeviceFactoryResetPortError::uncertain, false};
    }
    return inspect_nimble_store_empty();
}

}  // namespace opentrail::targets::heltec_v4_bench
