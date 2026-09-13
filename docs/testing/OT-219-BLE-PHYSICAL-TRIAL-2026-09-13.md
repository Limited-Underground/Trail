# OT-219 physical BLE confirmation attempt

Status: **confirmation not reached; originals restored and protected Ready verified**.
This is local, uncommitted engineering evidence. No V1 completion or public website
status change; no commit, publication, remote operation or deployment.

## Observed result

The selected Samsung SM-N986U (Android 13, SDK 33) received the exact OT-216 V1-Test
APK through `adb install -r`. Installed SHA-256 matched
`9f91a3f361221d19089e685b492b5e772e5cf7d658298ff955c4feb2508694a2`.
The ordinary OpenTrail package and comparison SM-S928U packages remained unchanged.
There was no data clear, uninstall, bond deletion or factory reset. Existing app
connection history remained visible. The updated app reached protected Ready on
the original Trail Bench firmware before the trial.

One Heltec WiFi LoRa32 V4.2 / ESP32-S3 / 16 MB, role A, was matched to its accepted
USB identity and original-image/protected hashes. The runtime rechecked USB/ROM
identity and flash geometry around its operations. Trail Bench 2 was left unchanged.
Radio transmission was outside scope; saved configuration was preserved.

The runner captured the actual 733184-byte application range at 0x10000, full
12288-byte NVS at 0xd000 and protected bootloader/partition/OTA regions. Admission
verified the accepted original prefix, protected hashes and storage conditions.
It wrote/read back the exact OT-216 candidate, then completed its boot command.
That command alone does **not** establish application boot.

The phone never reached protected Ready during the 180-second observation window.
The initial connection failure offered an enrollment scan, which found no pairable
device; the app was then restarted without clearing its data to use returning-owner
discovery. Its typed trace recorded `DISCONNECTED_BEFORE_PROFILE` at approximately
5.0, 6.0, 7.0 and 9.0 seconds per attempt, with no `GATT_LINK_ESTABLISHED` in the
retained candidate observation. Group was never entered and no confirmation was
requested. The runner recorded `timeout`; a later unavailable input did not change
that terminal result.

Automatic restoration wrote and independently verified the original application
and NVS, verified every protected region, performed a final sweep and reset the
original firmware. Custody closed; no separate recovery attempt was needed. The
user observed Trail Bench's screen come back on. The unchanged app/bond then
reached protected ProtocolInfo, subscription, authorization, Snapshot and Ready.

## Host correction made before board custody

The initial launcher attempt failed before any used marker, custody journal or
board access. ADB's read-only phone commands inherited the operator's stdin and
consumed the prewritten identity input. The eventual `Lines.read` timeout obscured
that cause behind the CLI's fixed refusal category.

The narrow correction sets `stdin=subprocess.DEVNULL` on every phone-check ADB
subprocess. All 11 operator tests pass, including the new stdin-isolation assertion.
An actual CLI/ADB reproduction with hardware execution replaced by an inert
sentinel timed out before the fix and reached admission after it. This distinguishes
the host failure from the subsequent physical candidate attempt.

The updated eight-source operator binding is privately retained with SHA-256
`a404264bfb3364f0a85fb15be1b94126fc4f93dde8cdd067c87440e9eb6ce0c5`.
OT-218's older binding and the unused initial grant were not reused. The existing
firmware/Android artifacts and runtime capsule were unchanged; no rebuild or
unrelated test-matrix rerun was needed.

## What this establishes and what comes next

The same upgraded app and bond reached Ready before and after the candidate.
This narrows the investigation to candidate boot/BLE startup, including its boot
delivery path; it does not establish a unique firmware defect. Confirmation's
crypto/storage work begins on READ after Ready and was never exercised here.

The next bounded diagnostic should capture the candidate's **existing** USB boot
categories: boot self-check PASS/FAIL, runtime-start error, security stage/detail,
runtime-started and heartbeat. This can distinguish failure to boot from runtime
containment or BLE startup failure without rebuilding or erasing phone history.
Use fresh exact diagnostic authority and preserve original restoration; this
consumed physical grant does not permit another candidate attempt.

The user's report that one screen was off had unknown onset. Trail Bench 2 was
reported on during restoration; Trail Bench came on when restoration completed.
Do not treat the blank-screen observation during ROM custody as proof of candidate
boot failure. Board markings/cable condition were not newly visually inspected.

Machine-readable [physical result](../../tests/hardware/OT-219-BLE-CONFIRMATION-2026-09-13.json).
Detailed private evidence is under `.private/ot219-physical-ble/`: exact grants,
source binding, launcher reproduction, phone package checks, typed traces and
result. Raw identities and the binding key stayed in controller memory. Exact
captured originals remain under the private custody filenames.
