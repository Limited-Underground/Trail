# OT-171 clock refresh correction preflight â€” 2026-09-07

Status: all applicable clock-correction gates passed; exact second-only install,
healthy runtime, retained-owner readbacks and owner-observed advancing clock are
accepted. The [installed checkpoint](../../docs/testing/OT-171-CLOCK-REFRESH-2026-09-07.md)
records the current `ot171-clock-v1` artifact and unchanged original control.

At preflight, the second application was `ot171-setup-v1`, SHA-256
`54E46380E038059A0F4C0AA280875CC8848751B93F046B9309B75F067A6083A1`.
That exact image supplied the recovery/prewrite boundary. The approved second-only
reset was paused during correction, then resumed after clock acceptance. The app
now reports Factory reset verified; old S24 bond removal and fresh pairing remain
pending. This does not establish full reset-domain cleanup acceptance.

## Applicable firmware-porting gates

| Gate | Applied result or pending requirement |
| --- | --- |
| Board and toolchain | Reuse the validated Heltec V4-family ESP32-S3 target, IDF 6.0.2 commit `7101770dc6db2667b3c477cc31365dd1acd6db4e`, Xtensa `esp-15.2.0_20251204`, offline pinned dependencies, DIO 80 MHz / 16 MB. |
| Pin and boot configuration | No planned changes to OLED GPIO 17/18, reset 21, Vext 36, USB, bootloader, partitions or board configuration. Final bootloader, table and SDK configuration comparisons passed. |
| Composition regression | Actual owner-to-OLED stale-minute regression reproduced before correction; all ten OLED groups now pass, including 599 same-minute ticks with no I/O, minute/name/region/format/validity changes, overlay restoration and failed-redraw containment. |
| Build and source lock | All thirteen affected native groups and seventeen structural test sets pass. Two initially absent builds with version `ot171-clock-v1` pass: 373 source hashes unchanged and all six artifacts identical. Offline image inspection and unchanged bootloader/table/config comparison pass. |
| Identity and recovery | Fresh one-use second-only installer passed exact recovery/artifact binding, unique endpoint/ROM identity, original exclusion and immediate prewrite bytes. No retry or recovery was needed. |
| Write scope | Application-only write at `0x10000`, aligned span 589,824 bytes, passed partition bounds and exact new-image/erased-tail readback. The installer did not mutate NVS or registry; the later approved protected factory reset was a separate action. |
| Runtime and visual acceptance | Healthy runtime (30,907 to 35,927 ms; stack 3,920 bytes), retained-owner Ready, automatic sync and US915 readback pass. Owner confirmed matching time and two further minute changes without Sync. |
| Unchanged subsystems | Radio, GNSS, battery, storage schema, entropy and power paths are unchanged; reuse their existing evidence rather than repeat unrelated testing. No radio transmission. Saved US915 is configuration only. |
| Deferred physical work | Cold-power/disassembly remains owner-deferred. Second-only reset resumed after clock acceptance and is app-verified; old S24 bond removal/fresh pairing remain pending. Original remains unchanged as control. |

All applicable build, identity, recovery and readback gates passed before execution. The investigation does not establish oscillator drift, fresh
pairing, radio delivery, field readiness or a completion-score increase. PIN entry
remains directly in Android; identifiers and pairing secrets stay out of public
evidence.
