# Wio Tracker L1 Pro Arrival and Bring-up

Status: prepared procedure only, 2026-08-09. The ordered unit has not arrived
and no OpenTrail or MeshCore compatibility result is claimed.

## Candidate role

The Wio Tracker L1 Pro is being evaluated first as a self-contained MeshCore
Bluetooth Companion with GNSS, not as an OpenTrail firmware target. The vendor
currently identifies its main controller as an nRF52840 and its LoRa module as
a Wio-SX1262. That makes it useful for network/GNSS interoperability evidence,
but it does not validate the ESP32 OpenTrail target architecture.

The vendor's MeshCore retail page lists SKU `100030144` and says that variant
ships with MeshCore Bluetooth Companion firmware. A related Wio Tracker L1 Pro
datasheet identifies SKU `114993649` for a Meshtastic-flashed variant. The
Amazon package label and device revision must resolve which exact unit arrived.

Official references:

- [MeshCore product page](https://www.seeedstudio.com/Wio-Tracker-L1-Pro-for-Meshcore-p-6717.html)
- [Seeed MeshCore bring-up and recovery guide](https://wiki.seeedstudio.com/get_started_with_other_mesh_firmware/)
- [Wio Tracker L1 Pro datasheet](https://files.seeedstudio.com/Bazaar/product_pdf/114993649.pdf)

## Safety and privacy rules

- Attach the supplied LoRa antenna before intentional transmission.
- Charge from the laptop or a conservative 5 V USB source first. Seeed's
  MeshCore guide specifically advises against a fast-charging charger during
  initial recovery/troubleshooting.
- Do not disconnect USB during erase, bootloader, or firmware transfer.
- Do not publish the bottom-label MAC, Bluetooth PIN, MeshCore public key,
  precise GPS coordinates, private channels, or device serial number.
- Do not erase or flash until the shipping firmware, visible version, USB
  state, and recovery entry have been recorded.
- Use the US region only after the exact RF variant and attached antenna are
  confirmed; reboot after changing the region.

## Phase A: unopened and power inspection

1. Photograph the box label, seals, accessories, and antenna. Keep identity
   labels private; record only model/SKU/revision in the public result.
2. Confirm the antenna connector is undamaged and the included antenna is
   labeled for the intended band.
3. Connect a known data-capable USB-C cable to the laptop and allow the battery
   to charge before troubleshooting a blank screen.
4. Record screen text, boot behavior, firmware role/version, battery indication,
   and whether the power switch must be raised.

## Phase B: read-only USB preflight

From the repository root, run:

```powershell
.\tools\Get-WioTrackerL1Preflight.ps1 | Format-List
```

The tool reads current serial-port names, matching Windows Plug-and-Play display
names when Windows permits that query, redacted matching USB registry display
names as a fallback, and any mounted `TRACKER L1` DFU volume. It performs no
reset, pairing, serial open, erase, flash, or file write. A PnP query failure is
reported rather than treated as evidence that the device is absent; registry
key/instance paths are never output because they may contain serial numbers.

If normal runtime is healthy, do not enter DFU yet. If recovery enumeration
must be proven, double-press `RST`; the official procedure says the yellow LED
stays solid and a `TRACKER L1` drive appears after roughly 10-15 seconds. Press
`User` once to exit DFU without copying or deleting anything.

## Phase C: preserve and inspect shipping MeshCore

1. Use the MeshCore browser application over BLE. The pairing passkey must be
   read from the device screen; never paste it into a test record.
2. Record only redacted model, board/firmware version, role, battery, radio
   preset, and GNSS enable/fix state.
3. If Windows BLE pairing fails, forget only this device and retry; do not erase
   firmware as the first Bluetooth remedy.
4. If the unit arrived on a non-US or unset region, set the US preset with the
   user present, apply, reboot as the vendor requires, and re-read settings.
5. Confirm the browser disconnects cleanly before any CLI or second-client
   attempt. BLE and USB availability must be recorded separately.

## Phase D: GNSS evidence

1. Enable GNSS through the device GPS page or MeshCore position settings.
2. Move outdoors with a clear sky view; do not judge first-fix performance from
   inside the house or vehicle.
3. Record cold-start time to first valid fix, satellite/fix-quality indicators,
   update cadence, and loss/recovery behavior. Redact latitude/longitude by
   replacing it with a coarse statement such as `fix acquired at test area`.
4. Disable/re-enable GNSS once and verify that a missing/stale fix is visibly
   distinct from a current fix.

## Phase E: bounded MeshCore interoperability

With both existing Heltec antennas and the Wio antenna attached:

1. Match the already recorded USA/Canada radio parameters.
2. Confirm mutual adverts without adding permanent private contacts.
3. Create an ephemeral private test channel only if cleanup can be observed.
4. Send a small numbered sample in each direction and record exact send,
   receive, duplicate, error, latency, SNR/RSSI, and airtime evidence available.
5. Remove the temporary channel/contact state and verify cleanup.

Do not run this phase unattended. Existing hardware scripts intentionally keep
cleanup under observation because a failed test must not leave private channel
state behind.

## Phase F: flashing decision and recovery

Keep the shipping Bluetooth Companion firmware if it provides the intended
GNSS and MeshCore path. USB enumeration alone is not a reason to reflash.

If a different role is needed:

1. Confirm the exact `Seeed Studio Wio Tracker L1 Pro` flasher target and
   available role for that firmware release.
2. Download the erase/application/bootloader artifacts first and record their
   filenames, versions, source URLs, and SHA-256 hashes.
3. Prove non-destructive DFU entry/exit before erasing.
4. Flash with stable USB power and never disconnect during transfer.
5. Re-run Phases B-E and record whether rollback/recovery remains possible.

If application firmware becomes unresponsive, the vendor documents a UF2
bootloader recovery path through the `TRACKER L1` drive. Treat bootloader
replacement as recovery, not routine bring-up.

## Acceptance record to create after arrival

Create `tests/hardware/OT-020-YYYY-MM-DD.md` containing:

- exact public-safe model/SKU/revision and acquisition state
- cable, host OS, browser/flasher/tool versions
- shipping firmware and role before any write
- USB runtime and DFU enumeration results
- BLE connection result and redacted settings
- GNSS current/stale/loss/recovery observations
- bounded Heltec interoperability counters and cleanup evidence
- recovery result if flashing was actually necessary
- final evidence classification: identified, experimented, or validated

Do not mark OT-020 done from a successful browser connection alone.
