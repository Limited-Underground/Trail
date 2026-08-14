# Windows USB candidate discovery v0

Status: read-only discovery plus runtime-evidence host adapters with
deterministic privacy/fail-closed evidence and a current three-device Windows
bench snapshot; no exact board profile or flash permission exists.

## Purpose

The future Windows loader needs a safe first screen before it can perform any
low-level probe or firmware write. `tools/Get-OpenTrailUsbCandidates.py`
enumerates USB serial runtimes and reduces them to non-authoritative candidate
records. `tools/Get-OpenTrailRuntimeEvidence.py` may then issue only MeshCore
version, board, and runtime-role queries and reduce their replies through strict
allowlists. Neither tool treats transport or installed runtime identity as
proof of an exact OpenTrail board/target profile.

Default output omits local COM names, serial numbers, hardware-instance IDs,
device locations, device identity, pairing data, raw runtime replies, and every
raw Windows enumerator record. An explicit `--include-local-ports` option is
available only for local troubleshooting; it does not change flash permission.

## Recognized runtime families

| USB VID:PID | Coarse interpretation | What it does not prove |
| --- | --- | --- |
| `303A:0002` | Espressif application USB | ESP32 variant, Heltec model/revision, memory, target role, bootloader, or image compatibility |
| `2886:0059` | Seeed TinyUSB serial runtime | SenseCAP model/revision, processor, memory, target role, bootloader, or image compatibility |
| `2886:1667` | Seeed Wio Tracker L1 TinyUSB serial runtime | Exact Pro SKU/revision, processor/memory measurements, RF variant, target role, bootloader, or image compatibility |

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

This is discovery/runtime evidence only. Installed MeshCore companion/repeater
role is not the unresolved OpenTrail target role. The snapshot is not the complete
`FirmwareInstallPreflight` decision and has no bundle, signature, partition,
erase, write, reset, DFU, recovery, or authorization capability.

## Current connected-bench result

On 2026-08-14, the default privacy-safe discovery/runtime path returned exactly
three candidates: one Espressif application USB runtime, one Seeed SenseCAP
TinyUSB runtime, and one Seeed Wio Tracker L1 TinyUSB runtime. Strict read-only
follow-up identified one Heltec V4 OLED MeshCore companion, one SenseCAP Solar
MeshCore repeater, and one Wio Tracker L1 MeshCore companion. All three were
runtime-identified and all three remained blocked from flashing. The Wio
reported `v1.17.0-727fc05`; the Heltec and SenseCAP retained their independently
recorded runtime versions. No local port or private identity was emitted.

Three consecutive current-tree built-in refreshes reproduced this exact public
roster with zero ready. The replacement source-free package then passed three
external UI Automation cycles that independently required the same exact public
display-name roster and zero-ready state. This remains runtime/loader evidence,
not an exact-board or compatibility result.

## Earlier connected-bench result

The default privacy-safe run on 2026-08-12 returned exactly three candidates:
one Seeed TinyUSB serial runtime and two Espressif application USB runtimes.
Strict read-only follow-up identified the former as a Seeed SenseCAP Solar
MeshCore repeater and the latter two as Heltec V4 OLED MeshCore companions, all
on `v1.16.0-07a3ca9` built 06-Jun-2026. All three were inspectable and all three
remained blocked from flashing. See the
[dated evidence record](../../tests/hardware/OT-019D-2026-08-12.md).

## Host evidence

Four discovery groups cover deterministic recognition/order, omission of
sensitive enumerator fields and local ports, explicit local-port inclusion,
and bounded unknown-device handling, including the Wio family. Five runtime
groups cover Heltec companion-field reduction, non-echoing rejection, exact
Wio companion reduction, repeater grammar/role reduction, and four-candidate
fixture integration with redacted failures.
The updated Python checks and publication-safety scan pass.

The runtime command grammar was checked against MeshCore's official
[`CommonCLI.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/CommonCLI.cpp).

## What remains

- query low-level processor and memory data without mutating the device;
- resolve an owner-approved exact hardware profile and received revision;
- bind exact target role, bootloader/partition schema, and signed image;
- feed independently verified evidence into the pure firmware-install
  preflight;
- retain fail-closed Windows UI authority and require explicit destructive/
  recovery consent in any future mutation path;
- validate wrong-target, interruption, reconnect, readback, trial, rollback,
  and physical recovery before any supported-board claim.
