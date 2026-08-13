# Windows USB candidate discovery v0

Status: read-only host adapter with four deterministic privacy/fail-closed
scenario groups and one successful three-device Windows bench snapshot; no
exact board profile or flash permission exists.

## Purpose

The future Windows loader needs a safe first screen before it can perform any
low-level probe or firmware write. `tools/Get-OpenTrailUsbCandidates.py`
enumerates USB serial runtimes and reduces them to non-authoritative candidate
records. It never treats a USB identifier as proof of an exact board.

Default output omits local COM names, serial numbers, hardware-instance IDs,
device locations, runtime identity, and every raw Windows enumerator record.
An explicit `--include-local-ports` option is available only for local
troubleshooting; it does not change flash permission.

## Recognized runtime families

| USB VID:PID | Coarse interpretation | What it does not prove |
| --- | --- | --- |
| `303A:0002` | Espressif application USB | ESP32 variant, Heltec model/revision, memory, target role, bootloader, or image compatibility |
| `2886:0059` | Seeed TinyUSB serial runtime | SenseCAP model/revision, processor, memory, target role, bootloader, or image compatibility |

Unknown USB serial devices are excluded by default. They may be displayed only
with `--include-unknown`, and remain equally blocked.

## Output contract

The `ot_windows_usb_candidates_v0` snapshot states that discovery is read-only
and made no state changes. Each candidate has:

- a call-local ordinal rather than a persistent device identity;
- normalized USB VID:PID and coarse transport family;
- `inspection_available: true`;
- `usb_identity_authoritative_for_flash: false`;
- unresolved low-level probe, exact profile, target role, board revision, and
  bootloader evidence; and
- `flashing_allowed: false`.

This is discovery evidence only. It is not the complete
`FirmwareInstallPreflight` decision and has no bundle, signature, partition,
erase, write, reset, DFU, recovery, or authorization capability.

## Connected-bench result

The default privacy-safe run on 2026-08-12 returned exactly three candidates:
one Seeed TinyUSB serial runtime and two Espressif application USB runtimes.
All three were inspectable and all three remained blocked from flashing. See
the [dated evidence record](../../tests/hardware/OT-019D-2026-08-12.md).

## Host evidence

Four groups cover deterministic recognition/order, omission of sensitive
enumerator fields and local ports, explicit local-port inclusion, and bounded
unknown-device handling. The publication-safety scan also passes.

## What remains

- query low-level processor and memory data without mutating the device;
- resolve an owner-approved exact hardware profile and received revision;
- bind exact target role, bootloader/partition schema, and signed image;
- feed independently verified evidence into the pure firmware-install
  preflight;
- build the Windows UI and require explicit destructive/recovery consent; and
- validate wrong-target, interruption, reconnect, readback, trial, rollback,
  and physical recovery before any supported-board claim.
