"""Generate a narrowly edited benchmark derivative; never modify frozen input."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

FROZEN_SHA256 = "612c892ecf649657e9a1f5fb68f8e24d088c2d4a94841f2ac5a8fff93d5b4240"

READY_HANDLER = r'''
void handle_ready(const char* challenge) {
    if (std::strlen(challenge) != 32U) { reject("ready", "syntax"); return; }
    for (size_t index = 0; index < 32U; ++index) {
        const char value = challenge[index];
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) {
            reject("ready", "syntax"); return;
        }
    }
    // Inspect raw permit state: status_receipt's permit_live may clear expiry.
    // A readiness query must not normalize or wipe any prior attempt state.
    const bool idle = !g_attempt.active && !g_attempt.permit.armed &&
        !g_attempt.rx_deadline_active && g_attempt.noise.stage == 0 &&
        !g_ledger.valid && !g_last_attempt_valid && !g_packet_received;
    const bool counters_zero = g_tx_attempted == 0 && g_tx_sent == 0 &&
        g_tx_failed == 0 && g_rx_accepted == 0 && g_rx_rejected == 0 &&
        g_lost == 0 && g_duplicates == 0 && g_corrupt == 0 &&
        g_unexpected == 0 && g_forced_timeouts == 0;
    if (!g_ready_boot_selftest || !g_radio_ready || g_last_radio_error != 0 ||
        !idle || !counters_zero) {
        reject("ready", "not_idle"); return;
    }
    ESP_LOGI(kTag, "%s READY schema=OTNXREADY1 challenge=%s accepted=yes stale_selftest=yes radio_ready=yes idle=yes tx=no",
             kReceipt, challenge);
    profile_receipt();
    status_receipt();
}

'''

REPLACEMENTS = (
    ("bool g_radio_ready = false;", "bool g_radio_ready = false;\nbool g_ready_boot_selftest = false;"),
    ("void cli_task(void*) {", READY_HANDLER + "void cli_task(void*) {"),
    ("        if (count == 5U && std::strcmp(tokens[0], \"prepare\") == 0) {",
     "        if (count == 2U && std::strcmp(tokens[0], \"ready\") == 0) {\n"
     "            handle_ready(tokens[1]);\n"
     "        } else if (count == 5U && std::strcmp(tokens[0], \"prepare\") == 0) {"),
    ("commands=prepare,arm-tx,send,abort,end,profile,status,restart",
     "commands=prepare,arm-tx,send,abort,end,profile,status,restart,ready"),
    ("    const bool stale_selftest_passed = stale_replay_selftest();",
     "    const bool stale_selftest_passed = stale_replay_selftest();\n"
     "    g_ready_boot_selftest = stale_selftest_passed;"),
)


class SourceError(ValueError):
    pass


def replace_once(source: str, before: str, after: str) -> str:
    if source.count(before) != 1:
        raise SourceError("source_anchor_mismatch")
    return source.replace(before, after, 1)


def generate(raw: bytes) -> bytes:
    if type(raw) is not bytes or hashlib.sha256(raw).hexdigest() != FROZEN_SHA256:
        raise SourceError("frozen_source_mismatch")
    source = raw.decode("utf-8").replace("\r\n", "\n")
    for before, after in REPLACEMENTS:
        source = replace_once(source, before, after)
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
