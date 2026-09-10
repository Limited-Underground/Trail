"""Add console-fault containment to the immutable contained derivative."""
import argparse
from pathlib import Path
import re

import noise_xk_contained_firmware_source as contained

SourceError = contained.SourceError
replace_once = contained.ready.replace_once


def function(source, signature):
    if source.count(signature) != 1:
        raise SourceError("function_anchor_mismatch")
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


def generate(raw):
    source = contained.generate(raw).decode("utf-8")
    source = '#include "opentrail_console.h"\n' + source
    guard = '''// Caller holds g_radio_mutex, or runs before any worker is started.
bool console_guard() {
    if (ot_console_healthy()) return true;
    wipe_attempt();
    g_radio_ready = false;
    return false;  // No logging through a failed transport; no physical-idle claim.
}

'''
    source = replace_once(source, "int16_t arm_receive() {", guard + "int16_t arm_receive() {\n    if (!console_guard()) return RADIOLIB_ERR_CHIP_NOT_FOUND;")
    source = replace_once(source, "    g_last_radio_error = g_radio.startReceive();",
                          "    g_last_radio_error = g_radio.startReceive();\n    if (!console_guard()) return RADIOLIB_ERR_CHIP_NOT_FOUND;")
    source = replace_once(source, "int16_t configure_radio() {",
                          "int16_t configure_radio() {\n    if (!console_guard()) return RADIOLIB_ERR_CHIP_NOT_FOUND;")
    config = function(source, "int16_t configure_radio() {")
    for anchor in ("    if (g_profile.begin == 0)", "    if (g_profile.header == 0)",
                   "    if (g_profile.crc == 0)", "    if (g_profile.begin != 0)"):
        config = replace_once(config, anchor,
            "    if (!console_guard()) return RADIOLIB_ERR_CHIP_NOT_FOUND;\n" + anchor)
    source = replace_once(source, function(source, "int16_t configure_radio() {"), config)
    # Every entry is admitted under the radio mutex; direct helpers retain guards.
    for signature in ("void handle_prepare(char* const* tokens) {",
                      "void handle_arm_tx(char* const* tokens) {",
                      "void handle_send(char* const* tokens) {",
                      "void start_expected_rx(Message message, int64_t start_us, uint32_t policy_ms) {"):
        source = replace_once(source, signature, signature + "\n    if (!console_guard()) return;")
    source = replace_once(source, "    rx_start_receipt(message, start_us, policy_ms);",
                          "    rx_start_receipt(message, start_us, policy_ms);\n    (void)console_guard();")
    for signature, cleanup, expected in (
        ("void handle_prepare(char* const* tokens) {", "if (!console_guard()) goto cleanup;", 1),
        ("void handle_arm_tx(char* const* tokens) {", "if (!console_guard()) return;", 1),
        ("void handle_send(char* const* tokens) {",
         "if (!console_guard()) { sodium_memzero(payload.data(), payload.size()); sodium_memzero(payload_hash, sizeof payload_hash); return; }", 5),
    ):
        before = function(source, signature)
        after, count = re.subn(r"ESP_LOGI\(.*?\);", lambda m: m[0] + "\n    " + cleanup,
                               before, flags=re.S)
        if count != expected:
            raise SourceError("log_anchor_mismatch")
        source = replace_once(source, before, after)
    # Stop queued commands before dispatch and latch any failure of a diagnostic-only command.
    source = replace_once(source,
        "        xSemaphoreTake(g_radio_mutex, portMAX_DELAY);\n        if (count == 2U",
        "        xSemaphoreTake(g_radio_mutex, portMAX_DELAY);\n        if (!console_guard()) { xSemaphoreGive(g_radio_mutex); continue; }\n        if (count == 2U")
    cli = function(source, "void cli_task(void*) {")
    after = replace_once(cli, "        xSemaphoreGive(g_radio_mutex);\n    }",
                         "        (void)console_guard();\n        xSemaphoreGive(g_radio_mutex);\n    }")
    source = replace_once(source, cli, after)
    source = replace_once(source, 'extern "C" void app_main() {',
                          'extern "C" void app_main() {\n    if (!ot_console_install()) return;')
    source = replace_once(source, "    g_module.setRfSwitchTable(kRfSwitchPins, kRfSwitchTable);",
        "    xSemaphoreTake(g_radio_mutex, portMAX_DELAY);\n    if (!console_guard()) { xSemaphoreGive(g_radio_mutex); return; }\n    g_module.setRfSwitchTable(kRfSwitchPins, kRfSwitchTable);")
    source = replace_once(source, '    xTaskCreate(cli_task, "ot153_cli", 8192, nullptr, 5, nullptr);',
        '    if (!console_guard()) { xSemaphoreGive(g_radio_mutex); return; }\n    xSemaphoreGive(g_radio_mutex);\n    xTaskCreate(cli_task, "ot153_cli", 8192, nullptr, 5, nullptr);')
    source = replace_once(source, "        if (!g_radio_ready || !g_packet_received) {",
                          "        if (!console_guard() || !g_radio_ready || !g_packet_received) {")
    source = replace_once(source, "        if (g_radio_ready) arm_receive();",
                          "        if (console_guard() && g_radio_ready) arm_receive();")
    source = replace_once(source, "        const size_t length = g_radio.getPacketLength();",
        "        const size_t length = g_radio.getPacketLength();\n        if (!console_guard()) { xSemaphoreGive(g_radio_mutex); continue; }")
    source = replace_once(source, "        const int64_t mono_us = esp_timer_get_time();",
        "        if (!console_guard()) { sodium_memzero(payload.data(), payload.size()); xSemaphoreGive(g_radio_mutex); continue; }\n        const int64_t mono_us = esp_timer_get_time();")
    return source.encode("utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        raise SourceError("output_overwrites_source")
    generated = generate(args.source.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(generated)


if __name__ == "__main__":
    main()
