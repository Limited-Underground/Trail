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
HAL_CPP = TARGET / "main" / "esp32_radiolib_hal.cpp"
HAL_HPP = TARGET / "main" / "esp32_radiolib_hal.hpp"
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


def test_isolated_target_and_exact_dependencies() -> None:
    expected = {
        ".gitignore", "CMakeLists.txt", "README.md", "dependencies.lock",
        "main/CMakeLists.txt", "main/app_main.cpp", "main/esp32_radiolib_hal.cpp",
        "main/esp32_radiolib_hal.hpp", "main/idf_component.yml", "sdkconfig.defaults",
    }
    files = {
        p.relative_to(TARGET).as_posix() for p in TARGET.rglob("*")
        if p.is_file() and "build-radio-diag" not in p.parts
        and "managed_components" not in p.parts and p.name != "sdkconfig"
    }
    require(files == expected, "diagnostic public source set changed")
    require("project(opentrail_heltec_v4_radio_diag)" in
            (TARGET / "CMakeLists.txt").read_text(encoding="utf-8"),
            "target identity changed")
    require('jgromes/radiolib: "==7.7.1"' in MANIFEST.read_text(encoding="utf-8"),
            "RadioLib manifest is not exactly pinned")
    lock = LOCK.read_text(encoding="utf-8")
    require(re.search(r"(?ms)^  jgromes/radiolib:.*?^    version: 7\.7\.1$", lock) is not None,
            "RadioLib lock changed")
    require("component_hash: 024269f750d7eb181d07bd57ccec5e5e4e276dbb262d681a6501c8e2244e80f0" in lock,
            "RadioLib component identity changed")


def test_exact_board_binding_and_hal_lifecycle() -> None:
    text = source()
    for token in (
        "kNss = 8", "kDio1 = 14", "kReset = 12", "kBusy = 13",
        "kSck = 9", "kMiso = 11", "kMosi = 10",
        "kFemPower = 7", "kFemEnable = 2", "kFemTxEnable = 46",
        "Module g_module{&g_hal, kNss, kDio1, kReset, kBusy};",
        "{Module::MODE_RX, {1, 1, 0, 0, 0}}",
        "{Module::MODE_TX, {1, 1, 1, 0, 0}}",
    ):
        require(token in text, f"board binding changed: {token}")
    declaration = HAL_HPP.read_text(encoding="utf-8")
    implementation = HAL_CPP.read_text(encoding="utf-8")
    require("void init() override;" in declaration and "void term() override;" in declaration,
            "HAL lifecycle declarations missing")
    require(function_body(implementation, "void Esp32RadioLibHal::init()").strip() == "spiBegin();",
            "HAL init must own SPI begin")
    require(function_body(implementation, "void Esp32RadioLibHal::term()").strip() == "spiEnd();",
            "HAL term must own SPI end")


def test_profile_chain_and_receipt_are_exact_and_fail_closed() -> None:
    text = source()
    for token in (
        "kFrequencyMhz = 915.0F", "kBandwidthKhz = 125.0F",
        "kSpreadingFactor = 7", "kCodingRate = 5",
        "kSyncWord = 0x12;  // Packet discriminator only; not encryption.",
        "kPowerDbm = 2", "kPreambleSymbols = 8", "kTcxoVoltage = 1.8F",
    ):
        require(token in text, f"profile changed: {token}")
    configure = function_body(text, "int16_t configure_radio()")
    calls = ("g_radio.begin(", "g_radio.explicitHeader()", "g_radio.setCRC(2)",
             "g_radio.forceLDRO(false)")
    require(all(call in configure for call in calls), "explicit profile call missing")
    require([configure.index(call) for call in calls] == sorted(configure.index(call) for call in calls),
            "profile command order changed")
    require("g_module.setRfSwitchState(Module::MODE_IDLE)" in configure,
            "configuration failure no longer idles RF")
    profile = function_body(text, "void print_profile()")
    require("configured=%s begin=%d header=%d crc=%d ldro=%d" in profile,
            "per-command profile result receipt missing")
    require("scope=driver_command_acceptance calibrated=no" in profile,
            "profile receipt overclaims calibrated readback")


def test_boot_restart_and_run_marker_are_receive_only() -> None:
    text = source()
    boot = function_body(text, 'extern "C" void app_main()')
    require("g_run = esp_random();" in boot, "privacy-safe per-run nonce missing")
    require("g_radio_ready = arm_receive(false) == 0;" in boot,
            "boot must enter receive without charging restart counter")
    require("g_radio.transmit(" not in boot, "boot must not directly transmit")
    require("OTD BOOT run=%lu reset=%d tx_at_boot=no" in boot,
            "stable boot receipt missing")
    require(text.count("g_radio.transmit(") == 1,
            "all authorized TX paths must share one accounting helper")
    cli = function_body(text, "void cli_task(void*)")
    restart = cli[cli.index('std::strcmp(line.data(), "restart")'):]
    require("xSemaphoreTake(g_radio_mutex, portMAX_DELAY);" in restart and
            "xSemaphoreGive(g_radio_mutex);" in restart and
            "clear_tx_permits();" in restart and
            "g_session_active = false;" in restart and "g_control_session = 0;" in restart and
            "OTD RESTART accepted=yes tx=no" in restart and "esp_restart();" in restart,
            "restart must clear permits/session, receipt, then software restart")


def test_serial_sessions_replay_profile_status_without_tx() -> None:
    text = source()
    parser = function_body(text, "bool parse_session_command(")
    require("parsed_session == 0" in parser and "parsed_session > UINT32_MAX" in parser and
            "input[consumed] != '\\0'" in parser,
            "session command must require one exact nonzero uint32")
    cli = function_body(text, "void cli_task(void*)")
    start_at = cli.index('std::strncmp(line.data(), "session-start ", 14)')
    end_at = cli.index('std::strncmp(line.data(), "session-end ", 12)')
    status_at = cli.index('std::strcmp(line.data(), "status")')
    start = cli[start_at:end_at]
    end = cli[end_at:status_at]
    require(start.index("clear_tx_permits();") <
            start.index("OTD SESSION_START run=%lu session=%lu accepted=yes tx=no permits_cleared=yes") <
            start.index("print_profile();") < start.index("print_status();"),
            "session-start must clear permits then emit SESSION_START/PROFILE/STATUS in order")
    require("transmit_locked(" not in start and "g_radio.transmit(" not in start and
            "arm_receive(" not in start,
            "session-start must not access or authorize radio transmission")
    require("!g_session_active || session != g_control_session" in end and
            "reason=session_mismatch transmitted=no" in end,
            "session-end must reject missing or mismatched session")
    require(end.index("clear_tx_permits();") <
            end.index("OTD SESSION_END run=%lu session=%lu accepted=yes tx=no permits_cleared=yes"),
            "matching session-end must clear permits before receipt")
    require("transmit_locked(" not in end and "g_radio.transmit(" not in end and
            "arm_receive(" not in end,
            "session-end must not access or authorize radio transmission")
    require(cli.count("fields.session != g_control_session") == 2,
            "DATA and probe must reject a non-active session before arm use")
    ack_at = cli.index('std::strncmp(line.data(), "ack-arm ", 8)')
    probe_at = cli.index('std::strncmp(line.data(), "probe ", 6)')
    ack = cli[ack_at:probe_at]
    require("session != g_control_session" in ack and
            "reason=session_mismatch transmitted=no" in ack,
            "ACK permit must bind to the active control session")
    require("if (ack_permit_live())" in ack and
            "reason=permit_already_live transmitted=no" in ack and
            ack.index("if (ack_permit_live())") < ack.index("g_ack = {true, role, session, count"),
            "a live ACK permit must reject rather than be overwritten")
    boot = function_body(text, 'extern "C" void app_main()')
    require(boot.index("OTD BOOT run=%lu reset=%d tx_at_boot=no") <
            boot.index("print_profile();") < boot.index("print_status();"),
            "unsolicited boot/profile/status compatibility sequence changed")


def test_total_wire_codec_probe_and_256_rejection() -> None:
    text = source()
    for token in (
        "constexpr size_t kFrameHeaderBytes = 16;",
        "constexpr size_t kMinStructuredWireBytes = 17;",
        "constexpr size_t kMaxWireBytes = 255;",
        "constexpr size_t kOversizeReceiptBytes = 256;",
        "constexpr uint8_t kProbeByte = 0xA5;",
        'std::memcpy(output, fields.kind == FrameKind::ack ? "OTA1" : "OTD1", 4);',
    ):
        require(token in text, f"wire contract missing: {token}")
    encode = function_body(text, "size_t encode_frame(")
    require("return fields.wire_bytes;" in encode and
            "i < fields.wire_bytes" in encode,
            "structured size must mean total radio wire bytes")
    decode = function_body(text, "bool decode_and_validate_frame(")
    require("length >= kMinStructuredWireBytes" in decode and
            "length == kFrameHeaderBytes" in decode and "length > kMaxWireBytes" in decode,
            "DATA/ACK receive bounds changed")
    cli = function_body(text, "void cli_task(void*)")
    probe = cli[cli.index('std::strncmp(line.data(), "probe ", 6)'):]
    require("frame[0] = kProbeByte;" in probe and "transmit_locked(frame.data(), 1)" in probe,
            "probe must be exactly one fixed 0xA5 radio byte")
    send = cli[cli.index('std::strncmp(line.data(), "send ", 5)'):]
    reject = send.index("if (parsed == ParseResult::oversize_256)")
    arm = send.index("if (!tx_arm_live())")
    radio = send.index("transmit_locked(frame.data(), length)")
    require(reject < arm < radio, "256 rejection must precede arm consumption and radio access")
    require("requested=256 transmitted=no arm_consumed=no" in send,
            "stable 256-byte no-transmit receipt missing")


def test_one_use_arm_and_bounded_ack_authority() -> None:
    text = source()
    require("kArmLifetimeMs = 30'000" in text and
            "kAckPermitLifetimeMs = 120'000" in text and "kMaxAckCount = 255" in text,
            "TX permit bounds changed")
    cli = function_body(text, "void cli_task(void*)")
    require("OTD ARM accepted=yes session=%lu uses=1 expires_ms=30000" in cli and
            "reason=no_active_session transmitted=no" in cli,
            "one-use arm must be bound to the active control session")
    require(cli.count("g_tx_armed = false;  // One use, consumed before any radio operation.") == 2,
            "DATA and probe must each consume one-use authorization")
    require("OTD ACK_ARM accepted=yes role=%c session=%lu remaining=%u expires_ms=120000" in cli,
            "bounded ACK permit receipt missing")
    receive = function_body(text, 'extern "C" void app_main()')
    consume = receive.index("--g_ack.remaining;  // Permit is consumed before any ACK radio operation.")
    ack_tx = receive.index("transmit_locked(ack_frame.data(), ack_length)")
    require(consume < ack_tx, "ACK budget must decrement before ACK radio access")
    ack_match = function_body(text, "bool ack_matches(")
    require("incoming.session != g_ack.session" in ack_match and
            "incoming.direction == Direction::b_to_a" in ack_match and
            "incoming.direction == Direction::a_to_b" in ack_match,
            "ACK permit must be exact-session and reverse-direction scoped")
    deadline = function_body(text, "bool deadline_live(")
    require("static_cast<int32_t>(deadline - g_hal.millis()) >= 0" in deadline,
            "permit deadline must be wrap-safe")


def test_machine_receipts_counters_and_privacy() -> None:
    text = source()
    for field in (
        "attempted=%lu", "sent=%lu", "tx_fail=%lu", "rx_valid=%lu",
        "rx_invalid=%lu", "rx_read_error=%lu", "rx_restart_fail=%lu",
    ):
        require(field in text, f"status counter missing: {field}")
    for receipt in (
        "OTD STATUS ", "OTD PROFILE ", "OTD ARM ", "OTD ACK_ARM ",
        "OTD TX ", "OTD RX ", "OTD REJECT ", "OTD RX_ERROR ",
        "OTD RX_ARM ", "OTD RESTART ",
    ):
        require(receipt in text, f"machine receipt missing: {receipt}")
    tx = function_body(text, "int16_t transmit_locked(")
    require("++g_tx_attempted;" in tx and "++g_tx_sent" in tx and "++g_tx_fail" in tx,
            "TX accounting is incomplete")
    arm_rx = function_body(text, "int16_t arm_receive(")
    require("++g_rx_restart_fail" in arm_rx, "RX restart failure accounting missing")
    require("++g_rx_valid" in text and "++g_rx_invalid" in text and "++g_rx_read_error" in text,
            "RX accounting is incomplete")
    require("payload_hash(" in text and "text=%" not in text and
            "reinterpret_cast<const char*>(payload.data())" not in text,
            "receipts must hash rather than log raw payload")
    require("OTD TX kind=%s result=%d mono_us=%lld" in text and
            "valid=yes mono_us=%lld" in text and "valid=no mono_us=%lld" in text and
            "OTD RX_ERROR result=%d mono_us=%lld" in text,
            "TX/RX receipts must carry device-monotonic timestamps")
    require(text.count("esp_timer_get_time()") >= 4,
            "TX completion, ACK completion, and RX receipts need monotonic captures")
    require("device_mac" not in text and "serial_port" not in text,
            "receipt source contains private identity fields")


def test_existing_bench_target_is_untouched() -> None:
    require(BENCH_TARGET.is_dir(), "existing bench target missing")
    for path in TARGET.rglob("*"):
        if not path.is_file() or "build-radio-diag" in path.parts or "managed_components" in path.parts:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        require("firmware/targets/heltec_v4_bench" not in text and "../heltec_v4_bench" not in text,
                f"diagnostic reaches into bench target: {path.name}")
    for args in (["git", "diff", "--quiet", "--", "firmware/targets/heltec_v4_bench"],
                 ["git", "diff", "--cached", "--quiet", "--", "firmware/targets/heltec_v4_bench"]):
        require(subprocess.run(args, cwd=ROOT, check=False).returncode == 0,
                "existing heltec_v4_bench has writes")


def main() -> int:
    tests = (
        test_isolated_target_and_exact_dependencies,
        test_exact_board_binding_and_hal_lifecycle,
        test_profile_chain_and_receipt_are_exact_and_fail_closed,
        test_boot_restart_and_run_marker_are_receive_only,
        test_serial_sessions_replay_profile_status_without_tx,
        test_total_wire_codec_probe_and_256_rejection,
        test_one_use_arm_and_bounded_ack_authority,
        test_machine_receipts_counters_and_privacy,
        test_existing_bench_target_is_untouched,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} Heltec V4 radio diagnostic source groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
