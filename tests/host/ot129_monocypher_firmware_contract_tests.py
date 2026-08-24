#!/usr/bin/env python3
"""Static ordering/boundary checks for the isolated OT-129 firmware target."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TARGET = (
    ROOT
    / "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129"
)
source = (TARGET / "main/app_main.c").read_text(encoding="utf-8")
control = (TARGET / "main/ot129_control_protocol.c").read_text(encoding="utf-8")
cmake = (TARGET / "main/CMakeLists.txt").read_text(encoding="utf-8")
runner = (ROOT / "tools/ot129_monocypher_protocol_runner.py").read_text(encoding="utf-8")

assert source.index("usb_serial_jtag_driver_install") < source.index("xTaskCreatePinnedToCore")
wait_start = source.index("static void ot129_wait_for_start")
read_loop = source.index("while (!state.started)", wait_start)
control_feed = source.index("ot129_control_feed", read_loop)
ready_write = source.index("usb_serial_jtag_write_bytes", control_feed)
ready_drain = source.index("usb_serial_jtag_wait_tx_done", ready_write)
wait_call = source.index("ot129_wait_for_start();", ready_drain)
frame_header = source.index("ot121_frame_header();", wait_call)
assert read_loop < control_feed < ready_write < ready_drain
assert ready_drain < wait_call < frame_header
assert "vTaskDelay(pdMS_TO_TICKS(3000U))" not in source
assert '"ot129_control_protocol.c"' in cmake
assert "state->started = true" in control
assert "OT129_CONTROL_LINE_REJECTED" in control
assert "ARMED" not in runner and "armed_timeout" not in runner

print("PASS: OT-129 isolated firmware ordering and boundary contract")
