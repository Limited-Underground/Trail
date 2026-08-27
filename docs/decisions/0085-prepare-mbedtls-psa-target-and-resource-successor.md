# Decision 0085: Prepare mbedTLS/PSA target and resource successor

Date: 2026-08-26

## Decision

Accept OT-149 as the host-only preparation checkpoint for the missing
mbedTLS/PSA comparison and for deterministic matched-resource accounting.
This decision accepts source, compile, parser, and contract evidence only. It
does not authorize or claim a benchmark or hardware execution.

## mbedTLS/PSA target

The separate candidate and no-candidate control share one common harness,
partition layout, full ESP-IDF component graph, and generated configuration.
They inherit the accepted Heltec defaults, reproducibility settings, and
OT-109 mbedTLS/PSA crypto overlay, then apply the same seven-setting
OT-139-derived quiet-console delta. Both resolve to the same 106,890-byte
sdkconfig with SHA-256
`00fd8a75e1df36e7cb4d4aa2275492297e1300383e15cbbf2d4a6284dd99d85e`.
This successor digest is deliberately distinct from the accepted API/config
baseline and is not presented as the baseline's whole-file identity.

The target exposes exactly five admitted comparison operations in order:
X25519, SHA-256, HKDF-SHA256, ChaCha20-Poly1305 encrypt, and
ChaCha20-Poly1305 decrypt. Fixed known-answer and exact negative gates run
before timing. Cleanup aggregates key-destruction status, always zeroizes
sensitive buffers, and contributes to terminal pass/fail. The proven
START/READY transport precedes an exact 1,015-frame result stream.

Fresh complete ESP32-S3 candidate and control builds each complete 1,199
targets with zero compiler warnings. The preparation record binds their
application BIN, ELF, map, common bootloader/partition artifacts, private build
log digests, source inputs, parser, and schema.

## Matched-resource successor

Accept the separate `OTMRAC1/v1` and `OTMRAR1/v1` boundary rather than
weakening the historical validator. It reads bounded ESP-IDF size JSON,
requires matched candidate/control metadata and two identical fresh builds per
side for a future result, derives linked flash from `total_size`, and derives
static RAM from DIRAM data/bss/noinit plus every tdata/tbss part.

Linked-flash and static-RAM deltas are exact signed 64-bit integers. Positive,
zero, and negative deltas are admitted without absolute-value conversion,
forced positivity, or substitution of application-file length.

## Remaining gates

OT-149 does not produce the two-node mbedTLS/PSA result, matched size reports,
resource deltas, Noise XK radio-cost result, eight-gate reconciliation, or
private trace-custody admission. Phase 2 and Phase 3 remain incomplete, and no
candidate, suite, handshake/KDF, or Packet-v1 wire format is selected.

The next task must separately freeze the exact executable/resource bundle.
Any hardware execution still requires fresh explicit owner approval and exact
Trail restoration boundaries.

## Authority and publication

No Heltec device, phone, serial endpoint, flash, radio, or private identifier
was accessed. OT-149 grants no execution authority and changes no capability,
readiness, or score. The public website remains on the normal batched update
cadence.
