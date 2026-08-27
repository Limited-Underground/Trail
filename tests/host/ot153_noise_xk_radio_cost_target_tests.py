from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost"
SOURCE = (TARGET / "main/app_main.cpp").read_text(encoding="utf-8")
MAIN_CMAKE = (TARGET / "main/CMakeLists.txt").read_text(encoding="utf-8")
PROJECT_CMAKE = (TARGET / "CMakeLists.txt").read_text(encoding="utf-8")
DEFAULTS = (TARGET / "sdkconfig.defaults").read_text(encoding="utf-8")
LOCK = (TARGET / "dependencies.lock").read_text(encoding="utf-8")
RADIOLIB_MANIFEST = (
    ROOT / "firmware/targets/heltec_v4_radio_diag/managed_components/"
    "jgromes__radiolib/idf_component.yml"
).read_text(encoding="utf-8")


class Tests(unittest.TestCase):
    def test_target_is_complete_and_locally_pinned(self) -> None:
        for relative in (
            "CMakeLists.txt",
            "dependencies.lock",
            "sdkconfig.defaults",
            "main/CMakeLists.txt",
            "main/idf_component.yml",
            "main/app_main.cpp",
        ):
            self.assertTrue((TARGET / relative).is_file(), relative)
        self.assertIn("version: 6.0.2", LOCK)
        self.assertIn('set(IDF_TARGET "esp32s3"', PROJECT_CMAKE)
        self.assertIn("version: 7.7.1", RADIOLIB_MANIFEST)
        self.assertIn(
            'jgromes/radiolib: "==7.7.1"',
            (TARGET / "main/idf_component.yml").read_text(encoding="utf-8"),
        )
        self.assertIn("CONFIG_APP_REPRODUCIBLE_BUILD=y", DEFAULTS)

    def test_exact_accepted_sources_are_composed_without_copying(self) -> None:
        self.assertIn("libsodium_noise_xk_v0/noise_xk_libsodium.c", MAIN_CMAKE)
        self.assertIn("heltec_v4_radio_diag/main/esp32_radiolib_hal.cpp", MAIN_CMAKE)
        self.assertIn("espressif_libsodium_1_0_22/managed_components", PROJECT_CMAKE)
        self.assertIn("espressif__libsodium", MAIN_CMAKE)
        self.assertIn("jgromes__radiolib", MAIN_CMAKE)

    def test_radio_profile_matches_accepted_ot114_binding(self) -> None:
        expected = (
            "kNss = 8, kDio1 = 14, kReset = 12, kBusy = 13",
            "kSck = 9, kMiso = 11, kMosi = 10",
            "kFemPower = 7, kFemEnable = 2, kFemTxEnable = 46",
            "kFrequencyMhz = 915.0F, kBandwidthKhz = 125.0F",
            "kSpreadingFactor = 7, kCodingRate = 5",
            "kSyncWord = 0x12",
            "kPowerDbm = 2",
            "kPreambleSymbols = 8",
            "kTcxoVoltage = 1.8F",
            "g_radio.explicitHeader()",
            "g_radio.setCRC(2)",
            "g_radio.forceLDRO(false)",
        )
        for item in expected:
            self.assertIn(item, SOURCE)

    def test_boot_is_receive_only_and_never_auto_transmits(self) -> None:
        app_main = SOURCE[SOURCE.index('extern "C" void app_main()') :]
        before_cli = app_main[: app_main.index("xTaskCreate(cli_task")]
        self.assertIn("g_radio_ready = arm_receive() == 0", before_cli)
        self.assertIn("tx_at_boot=no", before_cli)
        self.assertNotIn(".transmit(", before_cli)
        self.assertNotIn("startTransmit", SOURCE)

    def test_command_grammar_is_exact_and_bounded(self) -> None:
        self.assertIn('"prepare"', SOURCE)
        self.assertIn('"arm-tx"', SOURCE)
        self.assertIn('"send"', SOURCE)
        self.assertIn('"abort"', SOURCE)
        self.assertIn('"end"', SOURCE)
        self.assertIn('"profile"', SOURCE)
        self.assertIn('"status"', SOURCE)
        self.assertIn('"restart"', SOURCE)
        self.assertIn("count == 5U", SOURCE)
        self.assertIn("count == 4U", SOURCE)
        self.assertIn("count == 3U", SOURCE)
        self.assertIn("std::strlen(token) != 16U", SOURCE)
        self.assertIn("current >= 'a' && current <= 'f'", SOURCE)

    def test_role_and_scenario_sequence_are_explicit(self) -> None:
        for token in ("I", "R", "baseline", "retry-m2-withheld", "retry-restart"):
            self.assertIn(f'"{token}"', SOURCE)
        self.assertIn('reason = "baseline_required"', SOURCE)
        self.assertIn('reason = "withheld_attempt_required"', SOURCE)
        self.assertIn('reason = "scenario_consumed"', SOURCE)
        self.assertIn("bool baseline[2]", SOURCE)
        self.assertIn("bool retry_withheld[2]", SOURCE)
        self.assertIn("bool retry_restart[2]", SOURCE)

    def test_noise_messages_are_exact_raw_48_48_64_bytes(self) -> None:
        self.assertIn("OT_NOISE_XK_MESSAGE_1_BYTES == 48U", SOURCE)
        self.assertIn("OT_NOISE_XK_MESSAGE_2_BYTES == 48U", SOURCE)
        self.assertIn("OT_NOISE_XK_MESSAGE_3_BYTES == 64U", SOURCE)
        self.assertIn("OT_NOISE_XK_TOTAL_HANDSHAKE_BYTES == 160U", SOURCE)
        self.assertNotIn("OTD1", SOURCE)
        self.assertNotIn("OTA1", SOURCE)
        self.assertNotIn("Packet V1", SOURCE)

    def test_deterministic_keys_and_fresh_attempt_prologue_are_bound(self) -> None:
        for seed in ("0x31U", "0x51U", "0x71U", "0x91U"):
            self.assertIn(seed, SOURCE)
        self.assertIn('kPrologueMagic[] = "OT153FW0"', SOURCE)
        self.assertIn("encode_u64(prologue.data() + 8U, session)", SOURCE)
        self.assertIn("encode_u64(prologue.data() + 16U, attempt)", SOURCE)
        self.assertRegex(SOURCE, r"ot_noise_xk_init_initiator[\s\S]+prologue\.data\(\), prologue\.size\(\)")
        self.assertRegex(SOURCE, r"ot_noise_xk_init_responder[\s\S]+prologue\.data\(\), prologue\.size\(\)")

    def test_stale_attempts_fail_closed(self) -> None:
        self.assertIn("g_last_attempt_digest", SOURCE)
        self.assertIn('reject("prepare", "stale_attempt")', SOURCE)
        self.assertIn('reason = "stale_attempt"', SOURCE)
        self.assertIn('"auth_or_state"', SOURCE)
        stale_rx = SOURCE[SOURCE.index('duplicate ? "duplicate" : "auth_or_state"') :]
        self.assertIn("wipe_attempt();", stale_rx[: stale_rx.index("return;")])
        self.assertIn("stale_replay_selftest", SOURCE)
        self.assertIn("STALE_SELFTEST passed=%s stale_rejected=%s radio_frames=0", SOURCE)
        selftest = SOURCE[SOURCE.index("bool stale_replay_selftest") : SOURCE.index("size_t role_index")]
        self.assertNotIn("g_radio.", selftest)

    def test_tx_authority_is_exact_one_use(self) -> None:
        self.assertIn("uses=1 expires_ms=30000", SOURCE)
        send = SOURCE[SOURCE.index("void handle_send") : SOURCE.index("void handle_abort")]
        consume = send.index("g_attempt.permit = {}")
        crypto = send.index("ot_noise_xk_write_message")
        radio = send.index("g_radio.transmit")
        self.assertLess(consume, crypto)
        self.assertLess(consume, radio)
        self.assertIn("permit_consumed=yes", send)

    def test_forced_message_two_withholding_never_calls_radio(self) -> None:
        send = SOURCE[SOURCE.index("void handle_send") : SOURCE.index("void handle_abort")]
        withheld = send[send.index("Scenario::retry_withheld") : send.index("++g_tx_attempted")]
        self.assertIn("message == Message::m2", withheld)
        self.assertIn("transmitted=no", withheld)
        self.assertIn("return;", withheld)
        self.assertNotIn("g_radio.transmit", withheld)

    def test_receipts_have_exact_per_frame_hash_and_timing(self) -> None:
        self.assertIn("%s TX_START", SOURCE)
        for field in (
            "payload_sha256=%s",
            "start_us=%lld",
            "done_us=%lld",
            "measured_us=%lld",
            "wire=%u",
            "rx_restart=%d",
        ):
            self.assertIn(field, SOURCE)
        self.assertIn("done_us - start_us", SOURCE)
        self.assertIn("payload_digest_hex", SOURCE)

    def test_profile_receipt_exposes_every_radio_command_result(self) -> None:
        for field in (
            "begin_result=%d",
            "explicit_header_result=%d",
            "crc_result=%d",
            "ldro_result=%d",
            "configured=%s",
        ):
            self.assertIn(field, SOURCE)
        self.assertIn('std::strcmp(tokens[0], "profile") == 0', SOURCE)
        self.assertIn("profile_receipt();", SOURCE)

    def test_expected_receive_stages_and_deadlines_are_enforced(self) -> None:
        self.assertIn("kMessage2DeadlineMs = 2'196", SOURCE)
        self.assertIn("kMessage3DeadlineMs = 2'216", SOURCE)
        self.assertIn("%s RX_START", SOURCE)
        self.assertIn("%s STAGE_ACCEPT", SOURCE)
        self.assertIn("%s TIMEOUT", SOURCE)
        self.assertIn("start_expected_rx(Message::m2, done_us, kMessage2DeadlineMs)", SOURCE)
        self.assertIn("start_expected_rx(Message::m3, done_us, kMessage3DeadlineMs)", SOURCE)
        self.assertIn("check_rx_timeout();", SOURCE)
        self.assertIn("g_attempt.rx_deadline_active = false", SOURCE)

    def test_status_has_loss_duplicate_corrupt_and_unexpected_counters(self) -> None:
        for counter in (
            "lost=%lu",
            "duplicates=%lu",
            "corrupt=%lu",
            "unexpected=%lu",
            "forced_timeouts=%lu",
        ):
            self.assertIn(counter, SOURCE)
        self.assertIn("++g_lost", SOURCE)
        self.assertIn("++g_duplicates", SOURCE)
        self.assertIn("++g_corrupt", SOURCE)
        self.assertIn("++g_unexpected", SOURCE)
        forced = SOURCE[SOURCE.index("const bool forced") : SOURCE.index("wipe_attempt();", SOURCE.index("const bool forced"))]
        self.assertIn("++g_forced_timeouts", forced)
        self.assertRegex(forced, r"if \(forced\)[\s\S]+else[\s\S]+\+\+g_lost")

    def test_cli_owns_radio_mutex_without_recursive_send_lock(self) -> None:
        cli = SOURCE[SOURCE.index("void cli_task") : SOURCE.index("void handle_received_payload")]
        send = SOURCE[SOURCE.index("void handle_send") : SOURCE.index("void handle_abort")]
        self.assertIn("xSemaphoreTake(g_radio_mutex", cli)
        self.assertNotIn("xSemaphoreTake(g_radio_mutex", send)
        tx_start = send.index("%s TX_START")
        transmit = send.index("g_radio.transmit")
        self.assertLess(tx_start, transmit)

    def test_complete_end_hashes_split_keys_then_wipes(self) -> None:
        end = SOURCE[SOURCE.index("void handle_end") : SOURCE.index("void cli_task")]
        split = end.index("ot_noise_xk_split")
        receipt = end.index("tx_key_sha256=%s rx_key_sha256=%s")
        wipe = end.rindex("wipe_attempt()")
        self.assertLess(split, receipt)
        self.assertLess(receipt, wipe)
        self.assertIn("complete=yes wiped=yes tx=no", end)
        self.assertIn("reason=incomplete", end)
        self.assertIn("complete=no wiped=yes tx=no", end)

    def test_abort_restart_and_error_paths_wipe_sensitive_state(self) -> None:
        self.assertIn("ot_noise_xk_abort(&g_attempt.noise)", SOURCE)
        self.assertIn("sodium_memzero(payload.data(), payload.size())", SOURCE)
        self.assertIn("sodium_memzero(transmit_key, sizeof transmit_key)", SOURCE)
        self.assertIn("sodium_memzero(receive_key, sizeof receive_key)", SOURCE)
        abort = SOURCE[SOURCE.index("void handle_abort") : SOURCE.index("void handle_end")]
        self.assertIn("wipe_attempt();", abort)
        restart = SOURCE[SOURCE.index('std::strcmp(tokens[0], "restart")') :]
        self.assertIn("if (g_attempt.active) wipe_attempt();", restart)

    def test_receipts_never_format_raw_identity_payload_or_keys(self) -> None:
        format_literals = re.findall(r'ESP_LOG[IW]\([^,]+,\s*"([^"]*)"', SOURCE)
        joined = "\n".join(format_literals)
        self.assertNotIn("session=%", joined)
        self.assertNotIn("attempt=%", joined)
        self.assertNotIn("payload=%", joined)
        self.assertNotIn("key=%", joined)
        self.assertNotIn("secret=%", joined)
        self.assertIn("session_hash=%s", SOURCE)
        self.assertIn("attempt_hash=%s", SOURCE)
        self.assertIn("payload_sha256=%s", SOURCE)
        self.assertIn("tx_key_sha256=%s", SOURCE)


if __name__ == "__main__":
    unittest.main()
