# Heltec V4.2 direct-radio diagnostic

This is a separate, identical-node ESP-IDF diagnostic for the two Heltec WiFi
LoRa 32 V4.2 bench boards. It does not change or share application code with
`heltec_v4_bench`.

The image initializes the SX1262 in receive mode and never transmits at boot.
The USB Serial/JTAG console accepts:

- `session-start <nonzero_session>`
- `session-end <same_nonzero_session>`
- `status`
- `rx`
- `arm` (one transmission, expires after 30 seconds)
- `probe <A|B> <session> <A>B|B>A> <sequence>` (exactly one `0xA5` radio byte)
- `send <A|B> <session> <A>B|B>A> <sequence> <wire_bytes:17..255>`
- `ack-arm <A|B> <session> <count:1..255>` (matching ACKs only, expires after 120 seconds)
- `restart` (software restart without transmission)

Startup still emits `BOOT`, `PROFILE`, and `STATUS`, but a host must not depend
on retaining that early serial output. After each initial open or bounded
post-restart reconnect, `session-start` clears every volatile transmit permit,
emits `SESSION_START`, and then replays the current `PROFILE` and `STATUS`.
`session-end` requires the matching active session and clears every permit before its `SESSION_END` receipt. Ordinary arm, DATA, probe, and ACK permits are accepted only for the active session; an unexpired ACK permit cannot be overwritten. Neither session command accesses the radio or grants transmit authority. ACK authority still expires after 120 seconds if a host disconnect prevents an orderly session end.

The fixed close-bench profile is 915.000 MHz, 125 kHz bandwidth, SF7, CR 4/5,
private sync word `0x12`, explicit headers, CRC, LDRO disabled, eight preamble symbols, and 2 dBm radio output. The sync word is only a packet discriminator; it is not encryption. Structured DATA frames carry a nonzero session, consistent sender role/direction, sequence, and a total radio-wire size of 17..255 bytes. `send ... 256` produces a stable local no-transmit receipt before checking or consuming the one-use arm. ACK authority is volatile, session-scoped, count-limited, and consumed before each ACK radio operation. Machine receipts contain structured fields and a short deterministic hash, never raw payload bytes. TX and RX receipts include device-monotonic microseconds; RTT uses the successful DATA TX timestamp and matching ACK RX timestamp on the same sender run rather than host serial-arrival timing. The
target pins match Heltec's `WIFI_LORA_32_V4` GC1109 configuration. RadioLib is
pinned through the ESP-IDF component manager to version 7.7.1.

The profile receipt records successful driver command acceptance, not calibrated RF measurement or emitted-power readback. RTT is measured by a host runner from a successful DATA TX receipt to the matching ACK RX receipt on that same sender, including receiver turnaround.

This OT-110 extension is source/build evidence only until installed and exercised on both physical boards. It is not a supported-hardware or regulatory-acceptance claim.
