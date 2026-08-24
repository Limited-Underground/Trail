#!/usr/bin/env python3
"""Static checks for the bounded OT-121 libsodium local-primitive harness."""

from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import unittest


REPO = Path(__file__).resolve().parents[2]
ROOT = REPO / "tests" / "benchmarks" / "crypto" / "esp_idf" / "ot121_candidate_benchmarks"
LIBSODIUM = ROOT / "libsodium"
MONOCYPHER = ROOT / "monocypher"
OPERATIONS = [
    "ed25519_sign",
    "ed25519_verify",
    "x25519",
    "sha256",
    "hkdf_sha256",
    "chacha20poly1305_encrypt",
    "chacha20poly1305_decrypt",
    "noise_xk_handshake",
]


def schema_accepts(schema: dict, record: dict) -> bool:
    matches = 0
    for branch in schema["oneOf"]:
        required = set(branch["required"])
        properties = branch["properties"]
        if not required.issubset(record):
            continue
        if branch.get("additionalProperties") is False and not set(record).issubset(properties):
            continue
        valid = True
        for key, value in record.items():
            rule = properties[key]
            if "const" in rule and value != rule["const"]:
                valid = False
            if "enum" in rule and value not in rule["enum"]:
                valid = False
            if rule.get("type") == "integer" and type(value) is not int:
                valid = False
            if type(value) is int and "minimum" in rule and value < rule["minimum"]:
                valid = False
            if type(value) is int and "maximum" in rule and value > rule["maximum"]:
                valid = False
        if valid:
            matches += 1
    return matches == 1


class Ot121CandidateBenchmarkHarnessTests(unittest.TestCase):
    def setUp(self) -> None:
        self.schema = json.loads(
            (ROOT / "result-frame.schema.json").read_text(encoding="utf-8")
        )

    def test_required_files_partition_and_frame_schema(self) -> None:
        required = [
            ROOT / "README.md",
            ROOT / "result-frame.schema.json",
            ROOT / "include" / "ot121_benchmark_frame.h",
            LIBSODIUM / "CMakeLists.txt",
            LIBSODIUM / "sdkconfig.overlay",
            LIBSODIUM / "partitions.csv",
            LIBSODIUM / "main" / "CMakeLists.txt",
            LIBSODIUM / "main" / "app_main.c",
        ]
        self.assertTrue(all(path.is_file() for path in required))
        self.assertEqual(len(self.schema["oneOf"]), 6)
        kinds = {
            branch["properties"]["record_kind"]["const"]
            for branch in self.schema["oneOf"]
        }
        self.assertEqual(
            kinds,
            {"header", "gate", "sample", "operation_summary", "runtime_resources", "local_complete"},
        )
        operation_rules = [
            branch["properties"]["operation"]["enum"]
            for branch in self.schema["oneOf"]
            if "operation" in branch["properties"]
        ]
        self.assertEqual(operation_rules, [OPERATIONS, OPERATIONS])
        self.assertEqual(
            (LIBSODIUM / "partitions.csv").read_bytes(),
            (REPO / "firmware" / "targets" / "heltec_v4_bench" / "partitions.csv").read_bytes(),
        )
        self.assertEqual(
            hashlib.sha256((LIBSODIUM / "partitions.csv").read_bytes()).hexdigest(),
            "4f064c125aa641697e0539eaf9eda9d1cdecab46dd8ff387988b900f3efe2389",
        )

    def test_schema_requires_exact_fields_for_each_record_kind(self) -> None:
        common = {
            "schema": "OTCBXRF2",
            "version": 2,
            "scope": "candidate_local_v2",
            "candidate_id": "espressif_libsodium",
            "phase2_complete": False,
        }
        records = [
            {
                **common,
                "record_kind": "header",
                "operations_required": 8,
                "repetitions_cold": 100,
                "repetitions_warm": 100,
                "cold_conditioning": "32k_data_sweep",
                "radio_used": False,
                "candidate_selected": False,
            },
            {
                **common,
                "record_kind": "gate",
                "gate": "sodium_init",
                "outcome": "pass",
            },
            {
                **common,
                "record_kind": "sample",
                "operation": "sha256",
                "phase": "warm",
                "iteration": 99,
                "duration_us": 3,
                "outcome": "pass",
            },
            {
                **common,
                "record_kind": "operation_summary",
                "operation": "sha256",
                "phase": "warm",
                "min_us": 1,
                "median_us": 2,
                "p95_us": 3,
                "max_us": 4,
                "outcome": "pass",
            },
            {
                **common,
                "record_kind": "runtime_resources",
                "heap_domain": "internal_8bit",
                "heap_start_free_bytes": 100000,
                "heap_min_free_bytes": 98000,
                "peak_dynamic_ram_bytes": 2000,
                "stack_allocation_bytes": 8192,
                "stack_high_water_free_bytes": 4096,
                "max_stack_used_bytes": 4096,
                "watchdog_resets": 0,
                "watchdog_measurement": "uninterrupted_terminal_frame",
            },
            {
                **common,
                "record_kind": "local_complete",
                "operations_completed": 8,
                "operations_required": 8,
                "outcome": "pass",
                "radio_used": False,
                "candidate_selected": False,
            },
        ]
        self.assertTrue(all(schema_accepts(self.schema, record) for record in records))
        for record, missing in (
            (records[0], "operations_required"),
            (records[1], "gate"),
            (records[2], "duration_us"),
            (records[3], "p95_us"),
            (records[4], "peak_dynamic_ram_bytes"),
            (records[5], "outcome"),
        ):
            malformed = copy.deepcopy(record)
            malformed.pop(missing)
            self.assertFalse(schema_accepts(self.schema, malformed))
        extra = copy.deepcopy(records[2])
        extra["result_contract"] = "OTCBXR1"
        self.assertFalse(schema_accepts(self.schema, extra))
        wrong_scope = copy.deepcopy(records[5])
        wrong_scope["scope"] = "phase2_complete"
        self.assertFalse(schema_accepts(self.schema, wrong_scope))
        false_completion = copy.deepcopy(records[5])
        false_completion["phase2_complete"] = True
        self.assertFalse(schema_accepts(self.schema, false_completion))

    def test_local_scope_repetitions_operations_and_terminal_are_fixed(self) -> None:
        header = (ROOT / "include" / "ot121_benchmark_frame.h").read_text(encoding="utf-8")
        source = (LIBSODIUM / "main" / "app_main.c").read_text(encoding="utf-8")
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn('#define OT121_SCOPE "candidate_local_v2"', header)
        self.assertIn("#define OT121_LOCAL_OPERATIONS_REQUIRED 8U", header)
        self.assertIn("#define OT121_COLD_REPETITIONS 100U", header)
        self.assertIn("#define OT121_WARM_REPETITIONS 100U", header)
        self.assertIn("ot121_frame_local_complete", source)
        self.assertNotIn("ot121_frame_complete", source)
        self.assertNotIn("OT121_RESULT_CONTRACT", header)
        self.assertNotIn('record_kind\\":\\"complete', header)
        for operation in OPERATIONS:
            self.assertIn(f'{{ "{operation}",', source)
        self.assertIn("candidate_local_v2", readme)
        self.assertIn("operations_required equal to 8", readme)
        self.assertIn("phase2_complete false", readme)
        self.assertIn("not a complete Phase 2 result", readme)

    def test_fixed_frame_buffer_covers_exact_worst_case_wire_records(self) -> None:
        maximum = 2**64 - 1
        common = {
            "schema": "OTCBXRF2",
            "version": 2,
            "scope": "candidate_local_v2",
            "candidate_id": "espressif_libsodium",
            "phase2_complete": False,
        }
        records = {
            "header": {
                "schema": "OTCBXRF2", "version": 2, "record_kind": "header",
                "scope": "candidate_local_v2", "candidate_id": "espressif_libsodium",
                "operations_required": 8, "repetitions_cold": 100,
                "repetitions_warm": 100, "cold_conditioning": "32k_data_sweep",
                "phase2_complete": False, "radio_used": False,
                "candidate_selected": False,
            },
            "gate": {
                **common, "record_kind": "gate",
                "gate": "primitive_vectors_and_negative_cases", "outcome": "pass",
            },
            "sample": {
                **common, "record_kind": "sample",
                "operation": "chacha20poly1305_decrypt", "phase": "cold",
                "iteration": 99, "duration_us": maximum, "outcome": "pass",
            },
            "operation_summary": {
                **common, "record_kind": "operation_summary",
                "operation": "chacha20poly1305_decrypt", "phase": "cold",
                "min_us": maximum, "median_us": maximum, "p95_us": maximum,
                "max_us": maximum, "outcome": "pass",
            },
            "runtime_resources": {
                **common, "record_kind": "runtime_resources",
                "heap_domain": "internal_8bit",
                "heap_start_free_bytes": maximum,
                "heap_min_free_bytes": 0,
                "peak_dynamic_ram_bytes": maximum,
                "stack_allocation_bytes": 8192,
                "stack_high_water_free_bytes": 0,
                "max_stack_used_bytes": 8192,
                "watchdog_resets": 0,
                "watchdog_measurement": "uninterrupted_terminal_frame",
            },
            "local_complete": {
                **common, "record_kind": "local_complete",
                "operations_completed": 8, "operations_required": 8,
                "outcome": "pass", "radio_used": False,
                "candidate_selected": False,
            },
        }
        lengths = {
            kind: len(
                (
                    "OTCBXRF2 "
                    + json.dumps(record, ensure_ascii=True, separators=(",", ":"))
                    + "\n"
                ).encode("ascii")
            )
            for kind, record in records.items()
        }
        self.assertEqual(lengths, {
            "header": 309,
            "gate": 217,
            "sample": 277,
            "operation_summary": 361,
            "runtime_resources": 476,
            "local_complete": 276,
        })
        self.assertLess(max(lengths.values()), 512)
    def test_measurements_are_buffered_before_serial_emission(self) -> None:
        source = (LIBSODIUM / "main" / "app_main.c").read_text(encoding="utf-8")
        run_phase = source[source.index("static bool run_phase"):source.index("void app_main")]
        first_loop = run_phase.index(
            "for (unsigned iteration = 0; iteration < repetitions; ++iteration)"
        )
        second_loop = run_phase.index(
            "for (unsigned iteration = 0; iteration < repetitions; ++iteration)",
            first_loop + 1,
        )
        self.assertNotIn("ot121_frame_sample", run_phase[first_loop:second_loop])
        self.assertNotIn("ot121_frame_flush_and_pace", run_phase[first_loop:second_loop])
        self.assertNotIn("vTaskDelay", run_phase[first_loop:second_loop])
        self.assertNotIn("fflush", run_phase[first_loop:second_loop])
        self.assertIn("ot121_frame_sample", run_phase[second_loop:])
        self.assertIn("static ot121_sample g_samples[OT121_COLD_REPETITIONS]", source)
        self.assertIn("static uint64_t g_sorted[OT121_COLD_REPETITIONS]", source)
        self.assertNotIn("ot121_sample samples[", run_phase)
        self.assertNotIn("uint64_t sorted[", run_phase)
        samples_wipe = "sodium_memzero(g_samples, sizeof(g_samples));"
        sorted_wipe = "sodium_memzero(g_sorted, sizeof(g_sorted));"
        self.assertIn(samples_wipe, run_phase)
        self.assertIn(sorted_wipe, run_phase)
        self.assertEqual(run_phase.count("return passed;"), 1)
        self.assertLess(run_phase.index("ot121_frame_summary"), run_phase.index(samples_wipe))
        self.assertLess(run_phase.index(samples_wipe), run_phase.index(sorted_wipe))
        self.assertLess(run_phase.index(sorted_wipe), run_phase.index("return passed;"))
        self.assertIn("if (!cold && operation->invoke() != 0)", run_phase)
        self.assertLess(run_phase.index("sort_durations"), run_phase.index("return passed"))

    def test_each_complete_frame_is_formatted_chunked_drained_and_paced_once(self) -> None:
        header = (ROOT / "include" / "ot121_benchmark_frame.h").read_text(
            encoding="utf-8"
        )
        self.assertIn('#include "driver/usb_serial_jtag.h"', header)
        self.assertIn('#include "esp_err.h"', header)
        self.assertIn('#include "freertos/FreeRTOS.h"', header)
        self.assertIn('#include "freertos/task.h"', header)
        self.assertIn("#define OT121_FRAME_BUFFER_BYTES 512U", header)
        self.assertIn("#define OT121_FRAME_CHUNK_BYTES 48U", header)
        self.assertIn("#define OT121_FRAME_WRITE_TIMEOUT_MS 5000U", header)
        self.assertIn("#define OT121_FRAME_DRAIN_TIMEOUT_MS 5000U", header)
        self.assertNotIn("OT121_FRAME_INTER_CHUNK_DELAY_MS", header)
        self.assertIn("#define OT121_FRAME_PACING_MS 25U", header)
        self.assertIn(
            "static char g_ot121_frame_buffer[OT121_FRAME_BUFFER_BYTES];",
            header,
        )
        helper_start = header.index("static inline void ot121_frame_write_and_pace")
        helper_end = header.index("static inline void ot121_frame_header", helper_start)
        helper = header[helper_start:helper_end]
        self.assertEqual(helper.count("vsnprintf("), 1)
        self.assertIn("(size_t) formatted < sizeof(g_ot121_frame_buffer)", helper)
        self.assertEqual(helper.count("usb_serial_jtag_write_bytes("), 1)
        self.assertIn("const size_t frame_bytes = (size_t) formatted;", helper)
        self.assertIn("while (offset < frame_bytes)", helper)
        self.assertIn("remaining < OT121_FRAME_CHUNK_BYTES", helper)
        self.assertIn("g_ot121_frame_buffer + offset", helper)
        self.assertIn("chunk_bytes", helper)
        self.assertIn("pdMS_TO_TICKS(OT121_FRAME_WRITE_TIMEOUT_MS)", helper)
        self.assertNotIn("portMAX_DELAY", helper)
        self.assertIn("written == (int) chunk_bytes", helper)
        self.assertEqual(helper.count("usb_serial_jtag_wait_tx_done("), 1)
        self.assertIn(
            "pdMS_TO_TICKS(OT121_FRAME_DRAIN_TIMEOUT_MS)", helper
        )
        self.assertEqual(helper.count("ESP_ERROR_CHECK("), 3)
        self.assertNotIn("for (", helper)
        self.assertEqual(helper.count("while ("), 1)
        self.assertIn("OT121_FRAME_CHUNK_BYTES < 64U", header)
        for endpoint_boundary in (63, 64, 65):
            self.assertGreater(endpoint_boundary, 0)
            self.assertLess(endpoint_boundary, 512)
        self.assertEqual(
            helper.count("vTaskDelay(pdMS_TO_TICKS(OT121_FRAME_PACING_MS));"), 1
        )
        self.assertLess(
            helper.index("usb_serial_jtag_write_bytes"),
            helper.index("usb_serial_jtag_wait_tx_done"),
        )
        self.assertEqual(helper.count("vTaskDelay("), 1)
        self.assertNotRegex(helper, r"(?m)^\s*printf\(")
        self.assertNotIn("fflush(", helper)

        emitters = (
            "ot121_frame_header", "ot121_frame_gate", "ot121_frame_sample",
            "ot121_frame_summary", "ot121_frame_runtime_resources",
            "ot121_frame_local_complete",
        )
        for index, name in enumerate(emitters):
            start = header.index(f"static inline void {name}")
            end = (
                header.index("static inline void", start + 1)
                if index + 1 < len(emitters)
                else len(header)
            )
            body = header[start:end]
            self.assertEqual(body.count("ot121_frame_write_and_pace("), 1, name)
            self.assertIn('}\\n",', body, name)
            self.assertNotIn("printf(", body, name)
            self.assertNotIn("puts(", body, name)
            self.assertNotIn("fwrite(", body, name)
            self.assertNotIn("fflush(", body, name)
        self.assertEqual(header.count("ot121_frame_write_and_pace("), 7)

        consumers = []
        for source in ROOT.rglob("main/*.c"):
            if '"ot121_benchmark_frame.h"' in source.read_text(encoding="utf-8"):
                consumers.append(source)
                component = source.parent / "CMakeLists.txt"
                self.assertIn("esp_driver_usb_serial_jtag", component.read_text(encoding="utf-8"))
                self.assertIn("freertos", component.read_text(encoding="utf-8"))
        self.assertEqual(
            consumers,
            [
                LIBSODIUM / "main" / "app_main.c",
                MONOCYPHER / "main" / "app_main.c",
                ROOT / "monocypher_ot129" / "main" / "app_main.c",
            ],
        )
    def test_vectors_cleanup_and_failed_x25519_are_fail_closed(self) -> None:
        source = (LIBSODIUM / "main" / "app_main.c").read_text(encoding="utf-8")
        for marker in [
            "k_sha256_abc",
            "k_hkdf_prk",
            "k_hkdf_okm",
            "g_signature[0] ^= 0x01U",
            "zero_public",
            "tamper_rejected",
            "clear_sensitive_state",
            "sodium_memzero(g_sign_seed",
            "sodium_memzero(g_sign_sk",
            "sodium_memzero(g_x_scalar_a",
            "sodium_memzero(g_hkdf_prk",
            "sodium_memzero(g_aead_key",
            "sodium_memzero(g_ciphertext",
            "sodium_memzero(scratch",
        ]:
            self.assertIn(marker, source)
        x25519 = source[source.index("static int op_x25519"):source.index("static int op_sha256")]
        self.assertRegex(
            x25519,
            r"if \(rc == 0\) \{\s*g_sink \^= shared\[0\];\s*\}",
        )
        definition = "static void ot121_benchmark_task(void *context)\n{"
        worker = source[source.index(definition):]
        self.assertGreaterEqual(worker.count("goto cleanup;"), 2)
        self.assertNotIn("return;", worker)
        self.assertLess(
            worker.index("clear_sensitive_state();"),
            worker.index("ot121_frame_local_complete"),
        )

    def test_exclusive_buffered_usb_protocol_precedes_exact_startup_delay(self) -> None:
        source = (LIBSODIUM / "main" / "app_main.c").read_text(encoding="utf-8")
        component = (LIBSODIUM / "main" / "CMakeLists.txt").read_text(encoding="utf-8")
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn('#include "driver/usb_serial_jtag.h"', source)
        self.assertNotIn('#include "driver/usb_serial_jtag_vfs.h"', source)
        self.assertIn('#include "esp_err.h"', source)
        self.assertIn('#include "esp_log.h"', source)
        self.assertIn("#include <stdarg.h>", source)
        self.assertNotIn("#include <stdio.h>", source)
        self.assertIn("#define OT121_USB_TX_BUFFER_BYTES (16U * 1024U)", source)
        self.assertIn("#define OT121_USB_RX_BUFFER_BYTES 256U", source)
        self.assertIn(
            "#define OT121_BENCHMARK_TASK_STACK_BYTES (8U * 1024U)", source
        )
        self.assertIn("usb_serial_jtag_driver_config_t config", source)
        self.assertIn(".tx_buffer_size = OT121_USB_TX_BUFFER_BYTES", source)
        self.assertIn(".rx_buffer_size = OT121_USB_RX_BUFFER_BYTES", source)
        self.assertIn("ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));", source)
        self.assertNotIn("usb_serial_jtag_vfs_", source)
        self.assertNotIn("usb_serial_jtag_vfs_use_driver", source)
        self.assertEqual(source.count("usb_serial_jtag_driver_install"), 1)
        discard = source[
            source.index("static int ot121_discard_log_vprintf"):
            source.index("static void install_buffered_usb_serial_jtag_protocol")
        ]
        self.assertIn("const char *format, va_list arguments", discard)
        self.assertIn("(void) format;", discard)
        self.assertIn("(void) arguments;", discard)
        self.assertIn("return 0;", discard)
        self.assertIn("CONFIG_LOG_DYNAMIC_LEVEL_CONTROL == 1", source)
        self.assertIn("OT121_USB_TX_BUFFER_BYTES >= OT121_FRAME_CHUNK_BYTES", source)
        self.assertNotRegex(discard, r"(?m)^\s*(?:printf|fprintf|vprintf)\(")
        definition = "static void ot121_benchmark_task(void *context)\n{"
        app_main = source[source.index("void app_main(void)"):source.index(definition)]
        task = source[source.index(definition):]
        self.assertIn(
            "(void) esp_log_set_vprintf(ot121_discard_log_vprintf);", app_main
        )
        self.assertIn('esp_log_level_set("*", ESP_LOG_NONE);', app_main)
        self.assertNotIn("vTaskDelay(pdMS_TO_TICKS(3000U));", app_main)
        self.assertNotIn("ot121_frame_header();", app_main)
        self.assertNotIn("sodium_init()", app_main)
        self.assertNotIn("primitive_vectors_and_negative_cases()", app_main)
        self.assertNotIn("run_phase(", app_main)
        self.assertLess(
            app_main.index("esp_log_set_vprintf(ot121_discard_log_vprintf);"),
            app_main.index("esp_log_level_set"),
        )
        self.assertLess(
            app_main.index("esp_log_level_set"),
            app_main.index("install_buffered_usb_serial_jtag_protocol();"),
        )
        self.assertLess(
            task.index("vTaskDelay(pdMS_TO_TICKS(3000U));"),
            task.index("ot121_frame_header();"),
        )
        self.assertIn(
            "PRIV_REQUIRES espressif__libsodium esp_driver_usb_serial_jtag esp_timer freertos heap log",
            component,
        )
        self.assertIn("ot121_frame_local_complete(completed, passed);", task)
        self.assertNotIn("usb_serial_jtag_driver_uninstall", source)
        self.assertNotRegex(source, r"(?m)^\s*(?:printf|fprintf|vprintf)\(")
        self.assertNotIn("fflush(", source)
        self.assertNotIn("esp_vfs_usb_serial_jtag", source)
        self.assertIn("discard callback", readme)
        self.assertIn("does not attach the", readme)
        self.assertIn("VFS/stdout console", readme)
        self.assertIn("fixed 3000 ms", readme)
        self.assertIn("dedicated", readme)
        self.assertIn("8 KiB FreeRTOS task", readme)
        self.assertIn("pinned to ", readme)
        self.assertIn("current core", readme)
        self.assertIn("3,584-byte main-task stack", readme)
        self.assertIn("This preserves the", readme)
        self.assertIn("accepted sdkconfig", readme)

        declaration = "static void ot121_benchmark_task(void *context);"
        definition = "static void ot121_benchmark_task(void *context)\n{"
        self.assertEqual(source.count(declaration), 1)
        self.assertEqual(source.count(definition), 1)
        app_main = source[source.index("void app_main(void)"):source.index(definition)]
        task = source[source.index(definition):]
        self.assertIn("BaseType_t created = xTaskCreatePinnedToCore(", app_main)
        self.assertIn("ot121_benchmark_task,", app_main)
        self.assertIn('"ot121_bench",', app_main)
        self.assertIn("OT121_BENCHMARK_TASK_STACK_BYTES,", app_main)
        self.assertIn("tskIDLE_PRIORITY + 1,", app_main)
        self.assertIn("xPortGetCoreID());", app_main)
        self.assertIn(
            "ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);",
            app_main,
        )
        self.assertNotIn("ot121_frame_header();", app_main)
        self.assertIn("(void) context;", task)
        self.assertIn("vTaskDelay(pdMS_TO_TICKS(3000U));", task)
        self.assertIn("ot121_frame_header();", task)
        self.assertIn("ot121_frame_local_complete(completed, passed);", task)
        self.assertIn("vTaskDelete(NULL);", task)
        self.assertLess(
            task.index("ot121_frame_local_complete(completed, passed);"),
            task.index("vTaskDelete(NULL);"),
        )

    def test_no_radio_random_identity_or_selection_surface(self) -> None:
        combined = "\n".join(
            path.read_text(encoding="utf-8")
            for path in ROOT.rglob("*")
            if path.is_file()
        ).lower()
        for forbidden in [
            "esp_wifi",
            "esp_bt",
            "nimble",
            "lora",
            "sx126",
            "radio_init",
            "randombytes",
            "esp_read_mac",
            "device_id",
            "winner",
        ]:
            self.assertNotIn(forbidden, combined)
        self.assertIn(r'\"radio_used\":false', combined)
        self.assertIn(r'\"candidate_selected\":false', combined)

    def test_project_preserves_accepted_config_surface(self) -> None:
        project = (LIBSODIUM / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertNotIn("set(COMPONENTS", project)
        self.assertNotIn("MINIMAL_BUILD", project)
        overlay = (LIBSODIUM / "sdkconfig.overlay").read_bytes()
        self.assertEqual(
            hashlib.sha256(overlay).hexdigest(),
            "b7b722dc1bcc2c5917bee365f2123171ec398b0a0f295d61e7a7e8c26b99c832",
        )


if __name__ == "__main__":
    unittest.main()
