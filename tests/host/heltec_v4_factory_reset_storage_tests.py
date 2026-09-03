"""Static admission for the isolated Heltec V4 factory-reset storage ports."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "firmware/targets/heltec_v4_bench/main/heltec_v4_factory_reset_storage.hpp"
SOURCE = ROOT / "firmware/targets/heltec_v4_bench/main/heltec_v4_factory_reset_storage.cpp"
OWNER_HEADER = ROOT / "firmware/targets/heltec_v4_bench/main/companion_v1_heltec_adapters.hpp"
PARTITIONS = ROOT / "firmware/targets/heltec_v4_bench/partitions.csv"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


header = HEADER.read_text(encoding="utf-8")
source = SOURCE.read_text(encoding="utf-8")
owner_header = OWNER_HEADER.read_text(encoding="utf-8")
partitions = PARTITIONS.read_text(encoding="utf-8")

for text in (header, source):
    require("ESP_LOG" not in text and "printf(" not in text,
            "factory-reset storage ports must not log")

require('kHeltecV4FactoryResetMarkerNamespace[] = "ot_reset_v1"' in header,
        "reset intent needs an isolated default-NVS namespace")
require('kHeltecV4FactoryResetRecordKey[] = "record_v1"' in header and
        "kHeltecV4FactoryResetRecordBytes = 16" in header and
        "kHeltecV4FactoryResetRecordVersion = 1" in header and
        "kHeltecV4FactoryResetReceiptPendingValue = 0x5a" in header,
        "app reset needs one versioned atomic durable record")
require('kHeltecV4FactoryResetLegacyMarkerKey[] = "intent_v1"' in header and
        'kHeltecV4FactoryResetLegacyReceiptKey[] = "receipt_v1"' in header,
        "the unreleased split-key schema must be detected explicitly")
marker_reader = source[source.index("DeviceFactoryResetMarkerSnapshot read_marker"):
                        source.index("read_isolated_reset_marker")]
require(marker_reader.count("nvs_get_blob(") == 2 and
        "kHeltecV4FactoryResetRecordBytes" in marker_reader and
        "decode_marker_record(record)" in marker_reader and
        "legacy_marker.present || legacy_receipt.present" in marker_reader and
        "DeviceFactoryResetMarkerState::invalid" in marker_reader,
        "record size/version and either legacy split key must fail closed")
require("nvs_find_key(" in source and "NVS_TYPE_ANY" in source,
        "legacy keys must be detected even if their stored type is damaged")
isolated_marker_read = source[source.index("read_isolated_reset_marker"):
                              source.index("inspect_owner_namespace")]
require("kHeltecV4FactoryResetMarkerNamespace" in isolated_marker_read and
        "NVS_READONLY" in isolated_marker_read and
        "read_marker(handle)" in isolated_marker_read,
        "marker survival checks must reopen only the isolated reset namespace")
require('kHeltecV4FactoryResetStatePartitionLabel[] = "ot_state"' in header,
        "user-domain adapter must bind exact raw ot_state partition")
require("kHeltecV4FactoryResetStatePartitionType = 0x40" in header and
        "kHeltecV4FactoryResetStatePartitionSubtype = 0x00" in header and
        "0x00f00000" in header and "0x00100000" in header,
        "adapter must fail closed on any unexpected partition layout")
require("kHeltecV4FactoryResetHasActiveMapPackageStorage = false" in header,
        "current absence of active map-package storage must be explicit")
require('kCompanionV1OwnerNvsNamespace[] = "ot_v1_owner"' in owner_header,
        "owner domain authority unexpectedly changed")
require("ot_state,0x40,0x00,0xf00000,0x100000," in partitions,
        "accepted partition table no longer matches reset admission")

commit = source[source.index("commit_intent_and_readback"):
                source.index("complete_cleanup_and_readback")]
require(commit.count("set_marker_record(") == 1 and
        commit.index("set_marker_record(") < commit.index("nvs_commit(") <
        commit.rindex("read_marker("),
        "receipt and intent must use one atomic record, commit, then exact readback")
require("DeviceFactoryResetPortError::uncertain" in commit,
        "post-mutation marker failures must remain uncertain")

clear = source[source.index("complete_cleanup_and_readback"):
               source.index("consume_completion_receipt_and_readback")]
require("DeviceFactoryResetMarkerState::receipt_pending" in clear and
        clear.count("set_marker_record(") == 1 and
        clear.count("nvs_erase_key(") == 1 and
        clear.index("nvs_commit(") < clear.rindex("read_marker("),
        "app transition or physical clear must each mutate the same single record")

consume = source[source.index("consume_completion_receipt_and_readback"):
               source.index("HeltecV4FactoryResetUserDomainStorage::inspect_absence")]
require(consume.count("nvs_erase_key(") == 1 and
        consume.index("nvs_erase_key(") < consume.index("nvs_commit(") <
        consume.rindex("read_marker(") and
        "marker_verified_absent" not in consume,
        "receipt consumption must erase one record, commit, and exactly read absent")

marker_mutations = commit + clear + consume
require("nvs_set_u8(" not in marker_mutations and
        "nvs_set_u64(" not in marker_mutations and
        "kHeltecV4FactoryResetLegacyMarkerKey" not in marker_mutations and
        "kHeltecV4FactoryResetLegacyReceiptKey" not in marker_mutations,
        "the atomic marker path must never write or erase either legacy split key")
require("old record or complete new record after power loss" in commit,
        "the all-or-nothing power-loss contract must remain explicit at mutation")

user_erase = source[source.index("erase_all_and_verify_absent"):
                    source.index("HeltecV4FactoryResetNimbleBondStorage::inspect_empty")]
require(user_erase.index("erase_owner_namespace_and_verify") <
        user_erase.index("esp_partition_erase_range") <
        user_erase.rindex("inspect_absence"),
        "user cleanup must verify owner erase, raw state erase, then all absence")
require("esp_partition_read" in source and "bytes[index] != 0xff" in source,
        "raw ot_state erase must be verified byte-for-byte")

require("ble_hs_cfg.store_read_cb == ble_store_config_read" in source and
        "ble_hs_cfg.store_write_cb == ble_store_config_write" in source and
        "ble_hs_cfg.store_delete_cb == ble_store_config_delete" in source,
        "bond operations must prove ble_store_config_init installed callbacks")
require("set_store_access_ready(bool ready)" in header and
        "std::atomic<bool> store_access_ready_{false}" in header and
        source.count("store_access_ready_.load(std::memory_order_acquire)") >= 2,
        "bond operations must fail closed outside the live initialized NimBLE store window")
bond_erase = source[source.index("erase_all_and_verify_empty"):]
require(bond_erase.index("store_access_ready_.load") <
        bond_erase.index("before_marker") <
        bond_erase.index("ble_store_clear()") <
        bond_erase.index("after_marker") <
        bond_erase.index("inspect_nimble_store_empty()"),
        "bond clear must preserve committed reset intent before empty verification")
require("DeviceFactoryResetMarkerState::intent_committed" in bond_erase and
        "DeviceFactoryResetMarkerState::receipt_pending" in bond_erase and
        "DeviceFactoryResetPortError::uncertain" in bond_erase,
        "missing fail-closed marker survival checks around NimBLE bond clearing")
for object_type in (
    "BLE_STORE_OBJ_TYPE_OUR_SEC",
    "BLE_STORE_OBJ_TYPE_PEER_SEC",
    "BLE_STORE_OBJ_TYPE_CCCD",
    "BLE_STORE_OBJ_TYPE_CSFC",
    "BLE_STORE_OBJ_TYPE_PEER_ADDR",
    "BLE_STORE_OBJ_TYPE_ENC_ADV_DATA",
):
    require(object_type in source,
            f"missing NimBLE empty-inventory type: {object_type}")
bond_inventory = source[source.index("inspect_nimble_store_empty"):
                        source.index("}  // namespace")]
require("BLE_STORE_OBJ_TYPE_LOCAL_IRK" not in bond_inventory and
        "local IRK belongs to this device" in bond_inventory,
        "device-local IRK must not be classified as a peer bond")

for forbidden in (
    "nvs_flash_erase",
    "esp_ota_erase",
    "ESP_PARTITION_TYPE_APP",
    "ESP_PARTITION_SUBTYPE_DATA_OTA",
):
    require(forbidden not in source,
            f"factory-reset adapter crosses retained boundary: {forbidden}")

print("PASS: isolated Heltec V4 factory-reset storage admission")
