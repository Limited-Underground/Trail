#!/usr/bin/env python3
"""Focused source checks for the isolated Heltec V4 SX1262 diagnostic."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "firmware" / "targets" / "heltec_v4_radio_diag"
BENCH_TARGET = ROOT / "firmware" / "targets" / "heltec_v4_bench"
APP = TARGET / "main" / "app_main.cpp"
MANIFEST = TARGET / "main" / "idf_component.yml"
LOCK = TARGET / "dependencies.lock"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def source() -> str:
    return APP.read_text(encoding="utf-8")


def function_body(text: str, signature: str) -> str:
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


def test_is_one_isolated_identical_node_firmware_target() -> None:
    expected = {
        ".gitignore", "CMakeLists.txt", "README.md", "dependencies.lock",
        "main/CMakeLists.txt", "main/app_main.cpp", "main/esp32_radiolib_hal.cpp",
        "main/esp32_radiolib_hal.hpp", "main/idf_component.yml", "sdkconfig.defaults",
    }
    public_files = {
        path.relative_to(TARGET).as_posix()
        for path in TARGET.rglob("*")
        if path.is_file()
        and "build-radio-diag" not in path.parts
        and "managed_components" not in path.parts
        and path.name != "sdkconfig"
    }
    require(public_files == expected, "diagnostic public source set changed")
    root_cmake = (TARGET / "CMakeLists.txt").read_text(encoding="utf-8")
    component = (TARGET / "main" / "CMakeLists.txt").read_text(encoding="utf-8")
    app = source()
    require("project(opentrail_heltec_v4_radio_diag)" in root_cmake,
            "diagnostic target identity changed")
    require(component.count('"app_main.cpp"') == 1,
            "diagnostic must build one shared application source")
    for forbidden in ("OT-DEV-001", "OT-DEV-002", "node_a", "node_b", "NODE_A", "NODE_B"):
        require(forbidden not in app, f"unit-specific firmware branch present: {forbidden}")


def test_exact_heltec_v4_gc1109_pin_binding() -> None:
    text = source()
    expected = {
        "kNss": 8, "kDio1": 14, "kReset": 12, "kBusy": 13,
        "kSck": 9, "kMiso": 11, "kMosi": 10,
        "kFemPower": 7, "kFemEnable": 2, "kFemTxEnable": 46,
    }
    for name, value in expected.items():
        require(re.search(rf"constexpr\s+(?:uint32_t|int)\s+{name}\s*=\s*{value}\s*;", text) is not None,
                f"Heltec V4/GC1109 pin changed: {name}")
    require("Module g_module{&g_hal, kNss, kDio1, kReset, kBusy};" in text,
            "SX1262 module pin order changed")
    require("kFemPower, kFemEnable, kFemTxEnable, RADIOLIB_NC, RADIOLIB_NC" in text,
            "GC1109 RF-switch pin table changed")
    require("{Module::MODE_RX,   {1, 1, 0, 0, 0}}" in text and
            "{Module::MODE_TX,   {1, 1, 1, 0, 0}}" in text,
            "GC1109 receive/transmit RF-switch modes changed")


def test_radiolib_dependency_is_exactly_pinned() -> None:
    manifest = MANIFEST.read_text(encoding="utf-8")
    lock = LOCK.read_text(encoding="utf-8")
    require('jgromes/radiolib: "==7.7.1"' in manifest,
            "RadioLib manifest must use the exact 7.7.1 constraint")
    require(re.search(r"(?ms)^  jgromes/radiolib:.*?^    version: 7\.7\.1$", lock) is not None,
            "RadioLib lock must resolve exactly 7.7.1")
    require("component_hash: 024269f750d7eb181d07bd57ccec5e5e4e276dbb262d681a6501c8e2244e80f0" in lock,
            "RadioLib locked component identity changed")


def test_fixed_us915_close_bench_profile_is_exact() -> None:
    text = source()
    exact_constants = (
        "constexpr float kFrequencyMhz = 915.0F;",
        "constexpr float kBandwidthKhz = 125.0F;",
        "constexpr uint8_t kSpreadingFactor = 7;",
        "constexpr uint8_t kCodingRate = 5;",
        "constexpr uint8_t kSyncWord = 0x12;  // Packet discriminator only; not encryption.",
        "constexpr int8_t kPowerDbm = 2;",
        "constexpr uint16_t kPreambleSymbols = 8;",
        "constexpr float kTcxoVoltage = 1.8F;",
    )
    for value in exact_constants:
        require(value in text, f"fixed US915 profile changed: {value}")
    require(
        "g_radio.begin(kFrequencyMhz, kBandwidthKhz,\n"
        "        kSpreadingFactor, kCodingRate, kSyncWord, kPowerDbm,\n"
        "        kPreambleSymbols, kTcxoVoltage, false);" in text,
        "SX1262 begin call no longer consumes the exact fixed profile",
    )


def test_boot_is_receive_only_with_no_automatic_transmit() -> None:
    text = source()
    boot = function_body(text, 'extern "C" void app_main()')
    require("g_last_error = configure_radio();" in boot, "boot no longer initializes the radio")
    require("g_radio.setPacketReceivedAction(packet_received);" in boot,
            "boot no longer installs receive handling")
    require("g_radio_ready = arm_receive() == RADIOLIB_ERR_NONE;" in boot,
            "boot must enter receive mode")
    require("transmit(" not in boot and "send(" not in boot,
            "boot must never transmit automatically")
    require('"OpenTrail identical-node radio diagnostic; no automatic TX; sync word is not encryption"' in boot,
            "receive-only boot boundary is no longer explicit")
    require(text.count("g_radio.transmit(") == 1,
            "transmit surface must remain one explicit console path")
    cli = function_body(text, "void cli_task(void*)")
    require(cli.index('std::strncmp(line.data(), "send ", 5) == 0') <
            cli.index("g_radio.transmit(frame.data(), length)"),
            "transmit is not gated by the explicit send command")


def test_phy_is_explicit_crc_protected_and_fail_closed() -> None:
    text = source()
    configure = function_body(text, "int16_t configure_radio()")
    required = ("g_radio.explicitHeader()", "g_radio.setCRC(2)", "g_radio.forceLDRO(false)")
    for call in required:
        require(call in configure, f"missing explicit PHY configuration: {call}")
    require(configure.index(required[0]) < configure.index(required[1]) < configure.index(required[2]),
            "PHY configuration order changed")
    require("if (state == RADIOLIB_ERR_NONE)" in configure and
            "g_module.setRfSwitchState(Module::MODE_IDLE)" in configure,
            "PHY configuration must fail closed to RF idle")
    require("sync word is not encryption" in text and "Packet discriminator only; not encryption" in text,
            "sync word must not be represented as encryption")


def test_structured_frames_are_bounded_private_and_reconcilable() -> None:
    text = source()
    require("constexpr size_t kMaxFillBytes = 163;" in text,
            "diagnostic deterministic-fill limit changed")
    require("constexpr size_t kMaxWireBytes = kFrameHeaderBytes + kMaxFillBytes;" in text,
            "wire bound must include the structured header")
    for field in ("NodeRole role", "Direction direction", "uint32_t session",
                  "uint32_t sequence", "uint16_t fill_bytes"):
        require(field in text, f"structured field missing: {field}")
    require("uint8_t fill_byte(" in text and "encode_frame(" in text and
            "decode_and_validate_frame(" in text,
            "deterministic frame codec missing")
    decode = function_body(text, "bool decode_and_validate_frame(")
    require("fields.session == 0" in decode,
            "received zero session must reject")
    require("fields.fill_bytes < 1 || fields.fill_bytes > kMaxFillBytes" in decode,
            "received deterministic-fill bounds must reject")
    require("fields.role == NodeRole::a && fields.direction != Direction::a_to_b" in decode and
            "fields.role == NodeRole::b && fields.direction != Direction::b_to_a" in decode,
            "received role-direction mismatch must reject")
    parse = function_body(text, "bool parse_send(")
    require("session == 0" in parse,
            "console zero session must reject")
    require("fill < 1 || fill > kMaxFillBytes" in parse,
            "console deterministic-fill bounds must reject")
    require("fields.role == NodeRole::a && fields.direction != Direction::a_to_b" in parse and
            "fields.role == NodeRole::b && fields.direction != Direction::b_to_a" in parse,
            "console role-direction mismatch must reject")
    require("text=%" not in text and "reinterpret_cast<const char*>(payload.data())" not in text,
            "raw received payload must never be logged")
    require('"rx valid=yes bytes=%u rssi=%.1f snr=%.1f role=%c session=%lu dir=%s seq=%lu fill=%u"' in text,
            "sanitized structured RX reconciliation log missing")


def test_transmit_requires_one_use_volatile_arm() -> None:
    text = source()
    require("bool g_tx_armed = false;" in text and "constexpr RadioLibTime_t kArmLifetimeMs = 30'000;" in text,
            "volatile bounded TX arm missing")
    cli = function_body(text, "void cli_task(void*)")
    require('std::strcmp(line.data(), "arm") == 0' in cli,
            "explicit arm command missing")
    require("if (!tx_arm_live())" in cli and
            "g_tx_armed = false;  // One use, consumed before any radio operation." in cli,
            "send must require and consume a live one-use arm")
    require(cli.index("if (!tx_arm_live())") < cli.index("g_tx_armed = false;  // One use, consumed before any radio operation.") <
            cli.index("g_radio.transmit(frame.data(), length)"),
            "arm gate and one-use consumption must precede transmit")
    require("if (state == RADIOLIB_ERR_NONE) ++g_tx_count;" in cli,
            "TX count must advance only after RadioLib success")

def test_existing_heltec_v4_bench_target_is_untouched_and_unreferenced() -> None:
    require(BENCH_TARGET.is_dir(), "existing Heltec V4 bench target is missing")
    for path in TARGET.rglob("*"):
        if not path.is_file() or "build-radio-diag" in path.parts or "managed_components" in path.parts:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        require("firmware/targets/heltec_v4_bench" not in text and "../heltec_v4_bench" not in text,
                f"diagnostic source reaches into current bench target: {path.name}")
    unstaged = subprocess.run(
        ["git", "diff", "--quiet", "--", "firmware/targets/heltec_v4_bench"],
        cwd=ROOT, check=False,
    )
    staged = subprocess.run(
        ["git", "diff", "--cached", "--quiet", "--", "firmware/targets/heltec_v4_bench"],
        cwd=ROOT, check=False,
    )
    require(unstaged.returncode == 0 and staged.returncode == 0,
            "current heltec_v4_bench contains diagnostic-related writes")


def main() -> int:
    tests = (
        test_is_one_isolated_identical_node_firmware_target,
        test_exact_heltec_v4_gc1109_pin_binding,
        test_radiolib_dependency_is_exactly_pinned,
        test_fixed_us915_close_bench_profile_is_exact,
        test_boot_is_receive_only_with_no_automatic_transmit,
        test_phy_is_explicit_crc_protected_and_fail_closed,
        test_structured_frames_are_bounded_private_and_reconcilable,
        test_transmit_requires_one_use_volatile_arm,
        test_existing_heltec_v4_bench_target_is_untouched_and_unreferenced,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} Heltec V4 radio diagnostic source groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
