# Firmware install preflight v0

Status: pure fail-closed host policy with thirteen deterministic scenario groups;
no USB discovery, signed bundle verifier, firmware writer, Windows UI, approved
board profile, or physical recovery result exists.

## Purpose

The future Windows firmware loader needs to inspect a connected board without
turning a partial match into permission to flash it. `FirmwareInstallPreflight`
therefore separates two outcomes:

- **Inspection available** means a connected device may be examined and its
  public-safe characteristics displayed.
- **Flashing allowed** means every required compatibility and local
  authorization fact supplied to the policy is present and coherent.

A runtime name such as `Heltec V4 OLED`, a USB VID/PID, or an ESP32-S3 ROM
response is useful evidence, but none is an exact board profile by itself.

## Required evidence

The policy compares one immutable install requirement set with one copied probe
snapshot. Flashing remains blocked unless all applicable facts agree:

- a low-level probe completed;
- processor family matches;
- flash capacity is known and sufficient;
- required PSRAM is known and sufficient;
- an exact nonzero hardware-profile identifier and supported board revision
  come from either explicit operator confirmation or an authenticated device
  descriptor;
- the firmware target role exactly matches the profile's bench client,
  complete client, or packaged repeater role;
- bootloader schema is known and new enough; and
- the candidate image fits the target profile's maximum application image.

Unknown processor values, profile-evidence values, install modes, or malformed
requirements fail closed. The result accumulates every blocking issue so an
operator can correct the complete problem set without repeatedly attempting a
write.

## Install modes

| Mode | Additional local gate |
| --- | --- |
| Normal update | Complete compatibility evidence |
| Clean install | Separate explicit destructive-erase confirmation |
| Recovery install | Destructive-erase confirmation plus separate physical recovery authorization |

Recovery authorization is local input to the future adapter. It is not a
remote command, group message, server instruction, or permission inferred from
a failed boot.

## Current bench boundary

- The purchase record for `OT-DEV-001` and `OT-DEV-002` is the two-unit
  Meshnology V4 GPS bundle, Amazon ASIN `B0FS1WQWKF`: two ESP32-S3R2/SX1262
  boards, L76 GNSS modules, 3000 mAh batteries, N39 cases, and 915 MHz antennas.
  `OT-DEV-001` independently has ESP32-S3, 16 MB flash, and 2 MB PSRAM ROM
  evidence. Exact received minor revision and RF power variant remain
  unresolved. These assembled units are bench clients for USB, flash/recovery,
  radio, GNSS, and protocol testing; they are not the board-level hardware for
  the first complete touchscreen client.
- `OT-DEV-002` has only runtime-level board evidence and still needs its own
  low-level memory/security probe plus exact-profile resolution.
- The purchase record for `OT-DEV-003` is the SenseCAP Solar Node P1-Pro,
  Amazon ASIN `B0FMDHBWX8`. Seeed's current MeshCore product identifies the
  P1-Pro as SKU `100023690` with XIAO nRF52840 Plus, Wio-SX1262, and L76K GNSS.
  The exact received label/revision and low-level target profile remain open.
  Unlike the Heltec bench clients, this integrated solar unit is the physical
  packaged-repeater candidate and may be validated in that role.

GPS/GNSS is connected to both Heltec clients and the SenseCAP. A later redacted
USB check proved firmware detection/activation on both Heltecs and a live fix on
the SenseCAP, but that remains post-flash capability evidence—not
flash-identity evidence. Exact configured pins/transport, complete-client
binding, repeatable fix/accuracy/loss behavior, and no-fix degradation belong
to post-flash board-profile validation.

## Host evidence

Thirteen scenario groups cover disconnected and inspect-only states, runtime-name
insufficiency, exact normal-update admission, probe/processor mismatch, unknown
and undersized flash/PSRAM, profile/revision/target-role mismatch, bootloader
schema, clean-install confirmation, dual recovery authorization, oversized
images, and unknown/malformed input. The fixed result contains only an issue
mask and three Booleans; it holds no serial number, MAC, key, path, location, or
device name.

## What remains

- freeze exact, owner-approved Heltec and later SenseCAP hardware profiles;
- implement privacy-safe Windows USB/serial/DFU probe adapters;
- define and cryptographically verify the signed firmware-bundle manifest;
- bind exact partition tables, security settings, image lengths, and recovery
  procedures to each profile;
- build the Windows UI and packaged offline toolchain;
- prove normal update, clean install, and recovery on each physical revision;
- test wrong-target, interruption, reconnect, readback, trial, and rollback
  behavior before publishing any supported-board claim.

This policy performs no I/O and cannot erase, write, reboot, or claim that a
board is supported.

## Public product references

- [Two-unit V4 GPS purchase listing](https://www.amazon.com/dp/B0FS1WQWKF)
- [Heltec WiFi LoRa 32 V4 family](https://heltec.org/project/wifi-lora-32-v4/)
- [SenseCAP P1-Pro purchase listing](https://www.amazon.com/dp/B0FMDHBWX8)
- [Seeed SenseCAP P1-Pro for MeshCore](https://www.seeedstudio.com/SenseCAP-Solar-Node-P1-Pro-for-Meshcore-p-6741.html)
- [Seeed MeshCore setup and recovery guide](https://wiki.seeedstudio.com/get_started_with_meshcore_solar_node/)
