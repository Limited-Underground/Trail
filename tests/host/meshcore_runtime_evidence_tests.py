from __future__ import annotations

import importlib.util
import json
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = PROJECT_ROOT / "tools" / "Get-OpenTrailRuntimeEvidence.py"
SPEC = importlib.util.spec_from_file_location("meshcore_runtime_evidence", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("MeshCore runtime evidence tool could not be loaded")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def test_companion_response_reduction() -> None:
    payload = json.dumps(
        {
            "fw ver": 13,
            "fw_build": "06-Jun-2026",
            "model": "Heltec V4 OLED",
            "ver": "v1.16.0-07a3ca9",
            "repeat": False,
            "ble_pin": 123456,
            "node_name": "private-node-name",
            "public_key": "private-public-key",
        }
    )
    result = MODULE.parse_companion_version_json(payload)
    serialized = json.dumps(result)
    expect(result["runtime_role"] == "meshcore_companion", "companion role required")
    expect(result["runtime_protocol_version"] == 13, "protocol version required")
    for private in ("123456", "private-node-name", "private-public-key", "ble_pin"):
        expect(private not in serialized, f"companion private field leaked: {private}")


def test_companion_rejects_unrecognized_without_echo() -> None:
    secret = "private-unrecognized-model"
    try:
        MODULE.parse_companion_version_json(
            json.dumps(
                {
                    "fw ver": 13,
                    "fw_build": "06-Jun-2026",
                    "model": secret,
                    "ver": "v1.16.0-07a3ca9",
                }
            )
        )
    except ValueError as error:
        expect(secret not in str(error), "unrecognized model must not be echoed")
    else:
        raise AssertionError("unrecognized companion model must fail closed")


def test_repeater_response_reduction() -> None:
    result = MODULE.reduce_repeater_runtime(
        {
            "board": "board\r\n-> Seeed SenseCap Solar\r\n",
            "ver": "ver\r\n-> v1.16.0-07a3ca9 (Build: 06-Jun-2026)\r\n",
            "get role": "get role\r\n-> > repeater\r\n",
        }
    )
    expect(result["runtime_role"] == "meshcore_repeater", "repeater role required")
    expect(
        result["runtime_board_family"] == "seeed_sensecap_solar",
        "repeater family required",
    )
    try:
        MODULE.reduce_repeater_runtime(
            {
                "board": "board\r\n-> Seeed SenseCap Solar\r\nprivate-key-value\r\n",
                "ver": "ver\r\n-> v1.16.0-07a3ca9 (Build: 06-Jun-2026)\r\n",
                "get role": "get role\r\n-> > repeater\r\n",
            }
        )
    except ValueError as error:
        expect("private-key-value" not in str(error), "unexpected raw reply must not echo")
    else:
        raise AssertionError("extra repeater output must fail closed")


def test_three_candidate_runtime_integration_and_errors() -> None:
    records = [
        {"device": "TEST_CLIENT_B", "vid": 0x303A, "pid": 0x0002},
        {"device": "TEST_REPEATER", "vid": 0x2886, "pid": 0x0059},
        {"device": "TEST_CLIENT_A", "vid": 0x303A, "pid": 0x0002},
    ]

    def companion_query(port: str) -> dict[str, object]:
        if port == "TEST_CLIENT_B":
            raise RuntimeError("private failure detail")
        return {
            "runtime_query_succeeded": True,
            "runtime_board_family": "heltec_v4_oled",
            "runtime_firmware": "v1.16.0-07a3ca9",
            "runtime_build_date": "06-Jun-2026",
            "runtime_protocol_version": 13,
            "runtime_role": "meshcore_companion",
            "runtime_identity_authoritative_for_flash": False,
        }

    def repeater_query(_port: str) -> dict[str, object]:
        return {
            "runtime_query_succeeded": True,
            "runtime_board_family": "seeed_sensecap_solar",
            "runtime_firmware": "v1.16.0-07a3ca9",
            "runtime_build_date": "06-Jun-2026",
            "runtime_protocol_version": None,
            "runtime_role": "meshcore_repeater",
            "runtime_identity_authoritative_for_flash": False,
        }

    result = MODULE.collect_runtime_evidence(
        records, companion_query=companion_query, repeater_query=repeater_query
    )
    expect(result["candidate_count"] == 3, "three candidates required")
    expect(result["runtime_query_success_count"] == 2, "one reduced failure required")
    expect(result["flashing_allowed_count"] == 0, "runtime evidence cannot allow flash")
    serialized = json.dumps(result)
    expect("private failure detail" not in serialized, "runtime failure detail leaked")
    expect("TEST_CLIENT" not in serialized, "local port leaked")
    expect(
        all(item["flashing_allowed"] is False for item in result["devices"]),
        "every runtime candidate must stay blocked",
    )


def main() -> None:
    test_companion_response_reduction()
    test_companion_rejects_unrecognized_without_echo()
    test_repeater_response_reduction()
    test_three_candidate_runtime_integration_and_errors()
    print("PASS: 4 redacted MeshCore runtime evidence scenario groups")


if __name__ == "__main__":
    main()
