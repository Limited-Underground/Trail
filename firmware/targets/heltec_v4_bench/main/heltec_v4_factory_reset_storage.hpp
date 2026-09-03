#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "nvs.h"

#include "opentrail/device_factory_reset_executor.hpp"

namespace opentrail::targets::heltec_v4_bench {

inline constexpr char kHeltecV4FactoryResetMarkerNamespace[] = "ot_reset_v1";
inline constexpr char kHeltecV4FactoryResetRecordKey[] = "record_v1";
inline constexpr char kHeltecV4FactoryResetLegacyMarkerKey[] = "intent_v1";
inline constexpr char kHeltecV4FactoryResetLegacyReceiptKey[] = "receipt_v1";
inline constexpr std::size_t kHeltecV4FactoryResetRecordBytes = 16;
inline constexpr std::uint8_t kHeltecV4FactoryResetRecordVersion = 1;
inline constexpr std::uint8_t kHeltecV4FactoryResetMarkerValue = 0xa5;
inline constexpr std::uint8_t kHeltecV4FactoryResetReceiptPendingValue = 0x5a;

inline constexpr char kHeltecV4FactoryResetStatePartitionLabel[] = "ot_state";
inline constexpr std::uint8_t kHeltecV4FactoryResetStatePartitionType = 0x40;
inline constexpr std::uint8_t kHeltecV4FactoryResetStatePartitionSubtype = 0x00;
inline constexpr std::size_t kHeltecV4FactoryResetStatePartitionAddress =
    0x00f00000;
inline constexpr std::size_t kHeltecV4FactoryResetStatePartitionBytes =
    0x00100000;

// The current Heltec V4 target has no active on-device map-package storage.
// User-associated map selectors and metadata are inside ot_state and are
// erased with that whole partition. If a package store is later activated,
// this false admission must change and that store must join exact cleanup
// before DEVICE_FACTORY_RESET_V1 can be integrated again.
inline constexpr bool kHeltecV4FactoryResetHasActiveMapPackageStorage = false;

static_assert(sizeof(kHeltecV4FactoryResetMarkerNamespace) <=
              NVS_NS_NAME_MAX_SIZE);
static_assert(sizeof(kHeltecV4FactoryResetRecordKey) <=
              NVS_KEY_NAME_MAX_SIZE);
static_assert(sizeof(kHeltecV4FactoryResetLegacyMarkerKey) <=
              NVS_KEY_NAME_MAX_SIZE);
static_assert(sizeof(kHeltecV4FactoryResetLegacyReceiptKey) <=
              NVS_KEY_NAME_MAX_SIZE);

// Construct only after nvs_flash_init() succeeds. The marker is deliberately
// outside ot_v1_owner so user-domain erasure cannot remove reset intent before
// cleanup and exact absence verification finish.
class HeltecV4FactoryResetMarkerStorage final
    : public companion::DeviceFactoryResetMarkerPort {
public:
    HeltecV4FactoryResetMarkerStorage();
    ~HeltecV4FactoryResetMarkerStorage() override;

    HeltecV4FactoryResetMarkerStorage(
        const HeltecV4FactoryResetMarkerStorage&) = delete;
    HeltecV4FactoryResetMarkerStorage& operator=(
        const HeltecV4FactoryResetMarkerStorage&) = delete;

    [[nodiscard]] companion::DeviceFactoryResetMarkerSnapshot load() override;
    [[nodiscard]] companion::DeviceFactoryResetMarkerSnapshot
    commit_intent_and_readback(std::uint64_t reset_receipt) override;
    [[nodiscard]] companion::DeviceFactoryResetMarkerSnapshot
    complete_cleanup_and_readback() override;
    [[nodiscard]] companion::DeviceFactoryResetReceiptConsumeSnapshot
    consume_completion_receipt_and_readback() override;

private:
    nvs_handle_t handle_{0};
};

// Erases the complete current Trail user persistence boundary: every entry in
// ot_v1_owner and every byte of the exact raw ot_state partition. It never
// erases the default NVS partition, application/OTA partitions, otadata,
// bootloader, eFuses, immutable identity, or calibration domains.
class HeltecV4FactoryResetUserDomainStorage final
    : public companion::DeviceFactoryResetUserDomainPort {
public:
    [[nodiscard]] companion::DeviceFactoryResetAbsenceSnapshot
    inspect_absence() override;
    [[nodiscard]] companion::DeviceFactoryResetAbsenceSnapshot
    erase_all_and_verify_absent() override;
};

// Requires the exact ESP-IDF NimBLE store-config callbacks installed by
// ble_store_config_init(). In the pinned ESP-IDF 6.0.2 implementation,
// ble_store_clear() serializes each store operation with the host lock and is
// valid after nimble_port_init(), including before the host task starts. The
// target runtime must keep access enabled only while that initialized store is
// live, and must contain active connections/advertising before erasure.
class HeltecV4FactoryResetNimbleBondStorage final
    : public companion::DeviceFactoryResetBondDomainPort {
public:
    void set_store_access_ready(bool ready);

    [[nodiscard]] companion::DeviceFactoryResetAbsenceSnapshot
    inspect_empty() override;
    [[nodiscard]] companion::DeviceFactoryResetAbsenceSnapshot
    erase_all_and_verify_empty() override;

private:
    std::atomic<bool> store_access_ready_{false};
};

}  // namespace opentrail::targets::heltec_v4_bench
