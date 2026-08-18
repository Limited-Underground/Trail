import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PLAN_PATH = (
    ROOT
    / "firmware"
    / "targets"
    / "heltec_v4_bench"
    / "protected-root-inventory-reader-plan.json"
)

EXPECTED_ROUTE_ID = "OTPRR0/v0/esp32s3-esp-idf-6.0.2-metadata-adapter"

EXPECTED_ACCEPTED_SOURCES = {
    "components/efuse/src/efuse_controller/keys/with_key_purposes/esp_efuse_api_key.c":
        "CD6C5462CB1B2ADFE7735915810461EDE96ECF0B830A0761E4CAF2E6CB982C73",
    "components/efuse/src/esp_efuse_api.c":
        "66A12FA28B11642385C54A249CEC8EBEE139BF7A5BF562B9D5AFF29A3B8CF3F4",
    "components/efuse/esp32s3/esp_efuse_table.csv":
        "0B22F89D2B0F7EE315046DE5108C1DDDA8F46BB451985C7F66EB753301BFA69E",
    "components/efuse/include/esp_efuse.h":
        "4D488D3F2A75F0E55B903410987E08DCBAA550E11344032E93B315BEC87648A7",
    "components/efuse/esp32s3/include/esp_efuse_chip.h":
        "B5299EE67627C912C5E7A0E4A908D1678FD0D2F12D5AFD7A58D849FC1BADAA30",
}

EXPECTED_ALLOWED_APIS = {
    "esp_efuse_get_key_purpose",
    "esp_efuse_get_key_dis_read",
    "esp_efuse_get_key_dis_write",
    "esp_efuse_get_keypurpose_dis_write",
    "esp_efuse_key_block_unused",
}

EXPECTED_SIX_SLOT_FIELDS = [
    "purpose_category",
    "provisioned_state",
    "unused_state",
    "read_protection_state",
    "write_protection_state",
    "keypurpose_write_protection_state",
    "reservation_state",
    "conflict_category",
]


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def source_map(entries):
    result = {item["logical_path"]: item["sha256"] for item in entries}
    for digest in result.values():
        require(len(digest) == 64, "SHA-256 must contain 64 hex characters")
        int(digest, 16)
    return result


def test_identity_and_accepted_sources(plan):
    require(plan["schema"] == "OTPRR0/v0", "wrong route schema")
    require(plan["route_id"] == EXPECTED_ROUTE_ID, "wrong route id")
    require(plan["plan_id"] == "OT-080", "wrong plan id")
    require(plan["status"] == "OFFLINE_ROUTE_ACCEPTED", "wrong route status")
    require(plan["execution_status"] == "UNAUTHORIZED", "execution must deny")
    route = plan["accepted_target_side_route"]
    require(route["execution_model"] == "TARGET_SIDE_ESP_IDF_IN_PROCESS_METADATA_ADAPTER",
            "accepted execution model drift")
    require(route["esp_idf_version"] == "6.0.2", "ESP-IDF version drift")
    require(source_map(route["accepted_source_files"]) == EXPECTED_ACCEPTED_SOURCES,
            "accepted target-side source identity drift")


def test_rejected_host_route(plan):
    require(len(plan["rejected_host_routes"]) == 1, "one rejected host route required")
    rejected = plan["rejected_host_routes"][0]
    require(rejected["route"] == "PYTHON_ESPEFUSE_ESPEFUSES", "host route identity drift")
    require(rejected["status"] == "REJECTED", "host route must be rejected")
    require(rejected["esptool_espefuse_version"] == "5.3.1", "host version drift")
    reason = rejected["reason"]
    require("KEY0 through KEY5" in reason and "host memory" in reason,
            "raw-key host-memory rejection reason required")
    require(len(source_map(rejected["audited_source_files"])) == 6,
            "six rejected-route source identities required")


def test_exact_api_surface(plan):
    route = plan["accepted_target_side_route"]
    require(route["implementation_present"] is True,
            "build-only adapter implementation must be present")
    implementation = route["implementation_evidence"]
    require(implementation["increment"] == "OT-081", "wrong adapter increment")
    require(implementation["build_status"] ==
            "BUILD-COMPILED-NOT-RUNTIME-INJECTED", "wrong build status")
    require(implementation["execution_status"] ==
            "NOT-AUTHORIZED-NOT-RUN", "execution must remain denied")
    require(implementation["complete_inventory_output"] is False,
            "coarse roster must not become complete inventory")
    for path in (implementation["adapter_header"], implementation["adapter_source"],
                 implementation["host_test"], implementation["evidence"]):
        require((ROOT / path).is_file(), f"missing OT-081 evidence path: {path}")
    require(route["direct_in_process_only"] is True, "adapter must be in-process")
    require(set(route["allowed_decoded_read_only_apis"]) == EXPECTED_ALLOWED_APIS,
            "decoded API allowlist drift")
    require(route["shell_permitted"] is False, "shell must deny")
    require(route["subprocess_permitted"] is False, "subprocess must deny")
    require(route["external_command_permitted"] is False, "external command must deny")
    forbidden = plan["forbidden_surfaces"]
    for field in (
        "host_python_espefuse",
        "host_espefuse_full_summary",
        "host_espefuse_json",
        "host_raw_value",
        "hmac_self_test",
        "esp_efuse_get_key",
        "esp_efuse_read_block_for_key_blocks",
        "esp_efuse_read_field_blob_for_key_blocks",
        "all_efuse_write_burn_or_protection_apis",
        "raw_key_material",
        "raw_block_content",
        "raw_floor_bitmap",
    ):
        require(forbidden[field] is True, f"{field} must be forbidden")


def test_no_reader_authority_or_operation(plan):
    require(plan["coarse_key_roster_leaf_present"] is True,
            "coarse key-roster leaf must be recorded")
    require(plan["complete_inventory_reader_orchestrator_present"] is False,
            "complete inventory reader/orchestrator must remain absent")
    require(all(value is False for value in plan["authority"].values()),
            "every authority must remain false")
    policy = plan["future_connection_policy"]
    require(policy["maximum_connections"] == 1, "connection count must be one")
    require(policy["maximum_attempts"] == 1, "attempt count must be one")
    require(policy["connect_mode"] == "detecting", "connect mode drift")
    require(policy["before_reset"] == "no_reset", "before reset must be no_reset")
    require(policy["after_reset"] == "no_reset", "after reset must be no_reset")
    require(policy["policy_is_execution_authority"] is False,
            "connection policy must not grant authority")
    for field in (
        "stub_permitted", "retry_permitted", "automatic_reset_permitted",
        "manual_reset_permitted", "boot_permitted", "cleanup_may_mutate_device"
    ):
        require(policy[field] is False, f"{field} must remain false")

    operation = plan["operation_state"]
    require(operation["commands_defined"] is False, "commands must be absent")
    require(operation["attempts"] == 0 and operation["connections"] == 0,
            "no operation may be claimed")
    require(operation["device_observed"] is False, "device observation must be false")
    for field in (
        "unit_identifier", "port_identifier", "operation_identifier", "private_output_path"
    ):
        require(operation[field] is None, f"{field} must remain null")


def test_metadata_privacy_and_cleanup(plan):
    metadata = plan["normalized_private_metadata"]
    require(set(metadata) == {
        "six_key_slots", "nvs_protection", "rollback_floor", "security_state"
    }, "normalization surface drift")
    slots = metadata["six_key_slots"]
    require(slots["slot_count"] == 6, "six slots required")
    require(slots["allowed_fields"] == EXPECTED_SIX_SLOT_FIELDS,
            "six-slot allowed field schema drift")
    require(slots["raw_key_material_permitted"] is False,
            "raw key material must deny")
    require(slots["raw_block_content_permitted"] is False,
            "raw block content must deny")
    floor = metadata["rollback_floor"]
    require(floor["device_read_available"] is False, "floor read must be unavailable")
    require(floor["exact_descriptor_selected"] is False, "descriptor must be absent")
    require(floor["raw_bitmap_permitted"] is False, "raw bitmap must deny")
    require(floor["efuse_block"] is None and floor["first_bit"] is None
            and floor["capacity_bits"] is None, "floor descriptor must remain null")

    privacy = plan["privacy"]
    for field, value in privacy.items():
        if field.startswith("public_") and field.endswith("_permitted"):
            require(value is False, f"{field} must remain false")
    require(privacy["sanitizer_is_fixed_and_fail_closed"] is True, "sanitizer drift")
    require(len(privacy["fixed_public_results"]) == 3, "public result set drift")
    require(privacy["route_sanitizer_results"] == [
        "OFFLINE-METADATA-INTERFACE-ACCEPTED-EXECUTION-NOT-AUTHORIZED",
        "DENY-READER-ROUTE",
    ], "route sanitizer result set must match the C++ contract")

    cleanup = plan["cleanup_contract"]
    require(cleanup["close_transport_on_success"] is True, "success must close")
    require(cleanup["close_transport_on_denial"] is True, "denial must close")
    require(cleanup["close_transport_on_error"] is True, "error must close")
    require(cleanup["reset_on_close"] is False, "close must not reset")
    require(cleanup["retry_on_close"] is False, "close must not retry")
    require(cleanup["publish_private_evidence"] is False,
            "private evidence must stay private")


def test_offline_truth(plan):
    assertions = plan["offline_assertions"]
    require(assertions["route_schema_accepted"] is True, "route must be accepted")
    require(assertions["accepted_source_identity_frozen"] is True,
            "accepted source must be frozen")
    require(assertions["unsafe_host_route_rejected"] is True,
            "unsafe host route must be rejected")
    require(assertions["target_side_key_roster_adapter_build_compiled"] is True,
            "key-roster adapter build evidence must be accepted")
    require(assertions["complete_inventory_reader_orchestrator_exists"] is False,
            "complete inventory reader/orchestrator must remain absent")
    require(assertions["execution_authorized"] is False, "execution must remain false")
    require(assertions["hardware_observed"] is False, "hardware must remain unobserved")
    require(assertions["runtime_changed"] is False, "runtime must remain unchanged")
    require(assertions["score_change_supported"] is False, "score change must deny")


def main():
    plan = json.loads(PLAN_PATH.read_text(encoding="utf-8"))
    tests = (
        test_identity_and_accepted_sources,
        test_rejected_host_route,
        test_exact_api_surface,
        test_no_reader_authority_or_operation,
        test_metadata_privacy_and_cleanup,
        test_offline_truth,
    )
    for test in tests:
        test(plan)
    print(f"protected-root inventory target-side reader plan PASS ({len(tests)} groups)")


if __name__ == "__main__":
    main()
