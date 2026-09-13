# Receipt-boundary synchronization candidate

This OT-203 software target adds `SEC_BEGIN1 <challenge>` immediately before the
unchanged `SEC_EVAL1` receipt in one `ot_policy_send` call. Both lines contain the
same validated 32-character lowercase hexadecimal challenge. The combined maximum
is 121 logical bytes / 123 CRLF wire bytes, within the existing 128-byte cap and
20 ms send budget. A host must use the matching bounded BEGIN parser and preserve
strict receipt and silence checks after that marker.

The OT-200 input parser, evaluation, quiet console, NVS stages and packed input
record are reused. `send_return` reports the combined send, not host acceptance or
the overwritten evaluation outcome. Older operators and grants do not admit this
new image. No physical compatibility, policy pass or product readiness is claimed.

## Firmware-porting preflight

- **Target:** inherited Heltec WiFi LoRa32 V4.2 / ESP32-S3, 16 MB QIO flash at
  80 MHz; application offset `0x10000`, the unchanged partition table, USB
  Serial/JTAG and explicit existing reset handling. No PSRAM dependency is added.
  Radio, display, battery, GNSS, phone and optional services are not exercised;
  no pin or regional radio change is applicable to this receipt-only target.
- **Bytes/build:** explicit version `ot203-receipt-sync-v1`; reuse ESP-IDF v6.0.2,
  Xtensa `esp-15.2.0_20251204`, offline dependencies and the absent-directory dual
  build wrapper. Final build/config/ELF/hash and checkout-byte admission remain
  the parent's software gate. No existing hash-authoritative file is edited.
- **Startup/transport:** old FIFO output may precede the challenge-bound marker;
  bounded host preamble scanning must establish this exact transition. No fixed
  sleep, larger receipt limit or changed handle lifecycle is introduced.
- **Ordering/persistence:** one command, one combined send, unchanged sequential
  NVS commits and independent restoration. No callback, concurrency, radio or
  persistence behavior is added. Existing uncertain-close/reset gates remain.
- **Validation:** focused tests exercise the actual formatter, writer and FIFO
  adapter with retained startup bytes and export delivered bytes for the actual
  host parser. Full composed host checks and dual target builds remain required.
- **Hardware:** skipped for this software increment. Any later attempt needs new
  exact-image authority, fresh original/protected readbacks, explicit per-role
  reset/restoration and current hardware admission. This target is not covered by
  consumed OT-202 authority.
