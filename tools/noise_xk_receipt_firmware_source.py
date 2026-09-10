"""Add lock-independent receipts to the immutable console derivative."""
import argparse
from pathlib import Path
import noise_xk_console_firmware_source as console

SourceError = console.SourceError

def generate(raw):
    source = console.generate(raw).decode("utf-8")
    for level in "IW":
        source = source.replace("ESP_LOG" + level + "(", "OT_RECEIPT_" + level + "(")
    if "ESP_LOG" in source:
        raise SourceError("unexpected_log_path")
    # Receipt output never uses stdout, including the explicit restart command.
    source = console.replace_once(source, "            std::fflush(stdout);\n", "")
    source = console.replace_once(source,
        '    OT_RECEIPT_I(kTag, "%s READY schema=',
        '    if (!ot_console_begin_session()) return;\n    OT_RECEIPT_I(kTag, "%s READY schema=')
    source = console.replace_once(source,
        '        } else if (count == 5U && std::strcmp(tokens[0], "prepare") == 0) {',
        '        } else if (!ot_console_session_started()) {\n            // No command authority before a validated readiness challenge.\n        } else if (count == 5U && std::strcmp(tokens[0], "prepare") == 0) {')
    source = console.replace_once(source,
        '        if (!console_guard() || !g_radio_ready || !g_packet_received) {',
        '        if (!ot_console_session_started()) {\n            (void)console_guard();\n            xSemaphoreGive(g_radio_mutex);\n            vTaskDelay(pdMS_TO_TICKS(1));\n            continue;\n        }\n        if (!console_guard() || !g_radio_ready || !g_packet_received) {')
    return source.encode("utf-8")

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    if a.source.resolve() == a.output.resolve():
        raise SourceError("output_overwrites_source")
    data = generate(a.source.read_bytes())
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_bytes(data)

if __name__ == "__main__":
    main()
