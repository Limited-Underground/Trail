from __future__ import annotations

import importlib.util
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = PROJECT_ROOT / "tools" / "Get-MeshCoreGnssStatus.py"
SPEC = importlib.util.spec_from_file_location("meshcore_gnss_status", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("GNSS status tool could not be loaded")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def test_companion_custom_status() -> None:
    expect(
        MODULE.companion_custom_status({"other": "value"})
        == {"gps_detected": False, "gps_active": None},
        "missing GPS setting must remain not detected",
    )
    expect(
        MODULE.companion_custom_status({"gps": "0"})
        == {"gps_detected": True, "gps_active": False},
        "disabled detected GPS must remain distinct",
    )
    expect(
        MODULE.companion_custom_status({"gps": "1"})
        == {"gps_detected": True, "gps_active": True},
        "enabled detected GPS must be reported active",
    )
    try:
        MODULE.companion_custom_status({"gps": "secret-value"})
    except ValueError as error:
        expect(
            "secret-value" not in str(error),
            "invalid custom values must not be echoed",
        )
    else:
        raise AssertionError("invalid GPS setting must fail closed")


def test_companion_telemetry_reduction() -> None:
    sensitive_payload = {
        "pubkey_pre": "private-device-prefix",
        "lpp": [
            {"channel": 0, "type": "voltage", "value": 4.1},
            {
                "channel": 0,
                "type": "gps",
                "value": {
                    "latitude": 1.0,
                    "longitude": -1.0,
                    "altitude": 1.0,
                },
            },
        ],
    }
    reduced = MODULE.companion_telemetry_has_gps(sensitive_payload)
    expect(reduced is True, "GPS LPP presence must reduce to true")
    expect(isinstance(reduced, bool), "telemetry reduction must return only a boolean")
    expect(
        MODULE.companion_telemetry_has_gps({"lpp": [{"type": "voltage"}]})
        is False,
        "non-GPS telemetry must reduce to false",
    )


def test_repeater_status_parsing() -> None:
    off = MODULE.parse_repeater_gps_response("gps", "gps\r\n-> off\r\n")
    expect(
        off["gps_active"] is False and off["satellites"] is None,
        "off response must remain distinct from active no-fix",
    )
    waiting = MODULE.parse_repeater_gps_response(
        "gps", "gps\r\n-> on, active, no fix, 0 sats\r\n"
    )
    expect(
        waiting["gps_active"] is True
        and waiting["gps_fix"] is False
        and waiting["satellites"] == 0,
        "active no-fix response must parse exactly",
    )
    fixed = MODULE.parse_repeater_gps_response(
        "gps", "gps\r\n-> on, active, fix, 4 sats\r\n"
    )
    expect(
        fixed["gps_fix"] is True and fixed["satellites"] == 4,
        "fixed response must retain only status and satellite count",
    )


def test_repeater_rejects_unexpected_output() -> None:
    for response in (
        "gps\r\n-> on, active, fix, 999 sats\r\n",
        "gps\r\n-> on, private identity, fix, 4 sats\r\n",
        "gps\r\n-> private_key=do-not-echo\r\n",
        "gps\r\n-> off\r\nextra identity\r\n",
    ):
        try:
            MODULE.parse_repeater_gps_response("gps", response)
        except ValueError as error:
            expect(
                "private_key" not in str(error) and "identity" not in str(error),
                "unexpected serial output must not be echoed",
            )
        else:
            raise AssertionError("unexpected repeater response must fail closed")


def main() -> None:
    test_companion_custom_status()
    test_companion_telemetry_reduction()
    test_repeater_status_parsing()
    test_repeater_rejects_unexpected_output()
    print("PASS: 4 redacted MeshCore GNSS status scenario groups")


if __name__ == "__main__":
    main()
