"""Generate bounded-driver diagnostics over the frozen readiness derivative.

The driver timeout itself belongs to the separately pinned RadioLib overlay.
These two finite receipts expose call boundaries, not a physical root cause.
"""
from __future__ import annotations

import argparse
from pathlib import Path

import noise_xk_ready_firmware_source as ready

SourceError = ready.SourceError
SCHEMA = "OTNXTXDIAG1"
REPLACEMENTS = (
    (
        "    g_last_radio_error = g_radio.startReceive();\n    return g_last_radio_error;",
        "    g_last_radio_error = g_radio.startReceive();\n"
        "    if (g_last_radio_error == RADIOLIB_ERR_SPI_CMD_TIMEOUT) g_radio_ready = false;\n"
        "    return g_last_radio_error;",
    ),
    (
        '''    while (true) {
        if (!g_radio_ready || !g_packet_received) {
            xSemaphoreTake(g_radio_mutex, portMAX_DELAY);
            check_rx_timeout();
            xSemaphoreGive(g_radio_mutex);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        xSemaphoreTake(g_radio_mutex, portMAX_DELAY);''',
        '''    while (true) {
        // Read readiness under the same mutex as CLI containment and radio calls.
        xSemaphoreTake(g_radio_mutex, portMAX_DELAY);
        if (!g_radio_ready || !g_packet_received) {
            check_rx_timeout();
            xSemaphoreGive(g_radio_mutex);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }''',
    ),
    (
        "    const int16_t rx_restart = g_radio_ready ? arm_receive() : result;",
        '''    // A failed SPI command leaves radio state unconfirmed. Contain until reboot.
    if (result == RADIOLIB_ERR_SPI_CMD_TIMEOUT) g_radio_ready = false;
    ESP_LOGI(kTag, "%s TX_RETURN schema=OTNXTXDIAG1 role=%s scenario=%s message=%s result=%d start_us=%lld done_us=%lld measured_us=%lld",
             kReceipt, role_token(g_attempt.role), scenario_token(g_attempt.scenario),
             message_token(message), result, static_cast<long long>(start_us),
             static_cast<long long>(done_us), static_cast<long long>(done_us - start_us));
    const bool rx_rearm_attempted = g_radio_ready;
    const int64_t rx_rearm_start_us = esp_timer_get_time();
    const int16_t rx_restart = g_radio_ready ? arm_receive() : result;
    const int64_t rx_rearm_done_us = esp_timer_get_time();
    ESP_LOGI(kTag, "%s RX_REARM_RETURN schema=OTNXTXDIAG1 role=%s scenario=%s message=%s result=%d attempted=%s start_us=%lld done_us=%lld measured_us=%lld",
             kReceipt, role_token(g_attempt.role), scenario_token(g_attempt.scenario),
             message_token(message), rx_restart, rx_rearm_attempted ? "yes" : "no",
             static_cast<long long>(rx_rearm_start_us), static_cast<long long>(rx_rearm_done_us),
             static_cast<long long>(rx_rearm_done_us - rx_rearm_start_us));''',
    ),
)


def generate(raw: bytes) -> bytes:
    source = ready.generate(raw).decode("utf-8")
    for before, after in REPLACEMENTS:
        source = ready.replace_once(source, before, after)
    return source.encode("utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        raise SourceError("output_overwrites_source")
    result = generate(args.source.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result)


if __name__ == "__main__":
    main()
