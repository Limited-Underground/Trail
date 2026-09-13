# OT-217 single-device BLE confirmation trial preparation

2026-09-13. Preparation only; no trial authority. The accepted OT-216 firmware and
Android builds are unchanged. The first trial is one explicit local confirmation
on one Heltec, followed by independent restoration. The other Heltec stays unchanged.

## Exact inputs and recovery boundary

Use [OT-216 build evidence](../../tests/benchmarks/crypto/OT-216-BLE-CONFIRMATION-BUILD-2026-09-13.json)
and the retained Android artifact audit. The offline verifier under
`.private/ot217-ble-trial/verify-inputs.py` checks the pinned reports, 148 source
pins, all 21 firmware artifacts and all four APKs. Its `inputs.json` has no grant
and explicitly marks execution unavailable. It never accesses a device.

| Input | Verified value |
|---|---|
| Firmware | `build/ot216-ble-final-a/opentrail_heltec_v4_bench.bin`, 730736 bytes |
| Application start | `0x10000` |
| Actual image end, exclusive | `0xc2670` |
| Sector-aligned capture/write/restoration span | `0xb3000` / 733184 bytes |
| Aligned end, exclusive | `0xc3000` |
| Candidate padding within span | 2448 bytes, if the new operator writes a padded image |
| Newly covered tail beyond old custody | `[0xa0000,0xc3000)`, 143360 bytes / 35 sectors |
| Full NVS | `0xd000`, 12288 bytes |
| Protected readback | Boot `0/32768`; partition `0x8000/4096`; OTA metadata `0x9000/8192` |
| Phone APK | `build/android-ot216/app/outputs/apk/v1Test/app-v1Test.apk`, 11958234 bytes |
| Separate Android package | `io.github.nbjelanovic.otclient.v1test` |

Capture the **actual full 733184-byte original application span**, including its
extended tail, before any write. The old 589824-byte snapshots cannot establish
those extra bytes. Never synthesize an original tail by padding an old backup.
Preserve full NVS, independently verify all restoration readbacks, restart the
original application, and close custody on every completed trial path. Do not
write bootloader, partition table, OTA metadata, other application slots, gap or
`ot_state` regions. Do not use generated all-partition flash commands.

## Pre-execution gates

1. A new narrowly scoped operator must bind this exact image and larger span,
   fresh capture, one-use grant, readback, BLE observation and restoration. The
   frozen OT-212/213 runner is ineligible: it hard-codes 589824 bytes and OT-208,
   and expects serial `RUN SEC_EVAL1`/BEGIN/receipt traffic absent from this target.
   Reuse its custody principles, not its consumed requests or authority.
2. Identify the selected current Heltec, board revision, MCU/flash, region and
   power/cable conditions. Freshly enumerate and independently correlate its port
   before each operation; serial opening can reset a board. Historical role/port
   mappings are stale. Verify the preserved bootloader/OTA metadata will select
   the factory candidate at `0x10000` and is compatible with it. Stop if uncertain;
   do not change protected metadata to force boot. Keep the second board untouched.
3. Verify the selected phone's model/OS, V1-Test package, installed APK and signing
   certificate. Ordinary OpenTrail has a separate package and must be preserved.
   An APK copy does not back up app-private data. Do not uninstall, clear data,
   replace an incompatible signature or change bonds to force admission.
4. Preserve originals and verify all seven evaluation namespaces are absent:
   `ot216_boot`, `ot216_ia`, `ot216_ib`, `ot216_ta`, `ot216_tb`, `ot216_ra`, `ot216_rb`.
   Even an empty retained namespace is ineligible. A namespace present at fresh
   capture is a stop condition, not permission to erase it.
5. Bind the selected device, exact original capture, candidate/APK hashes, one case,
   all mutation/restoration bounds, phone handling and abort path into a current
   explicit hardware authorization. No earlier grant is reusable.

## Verified phone choice

Read-only ADB inspection found V1-Test on both connected phones: SM-N986U/Android13
and SM-S928U/Android16. Neither installed APK matches OT-216, although both report
version1 / `1.0.0-v1test`. Both signing certificates match the candidate, and both
have Bluetooth scan/connect and notification permissions granted. Installed APKs
were hash-verified and retained privately; app-private data was not read or copied.
No app was installed, started, cleared, stopped or uninstalled.

Select SM-N986U/Android13 for the first trial and keep the other phone unchanged.
This is a test-sequencing choice, not a claim of incompatibility with Android16.
Re-identify the live selected handset before any later operation. An in-place
update is signature-compatible, but still requires the agreed installation/data
plan. Matching certificates do not prove app-data rollback compatibility.

## First case: explicit confirmation, then restoration

1. With the new operator and authority in place, write/readback only the admitted
   application extent; boot the candidate through the approved reset path.
2. In Trail V1-Test, remain on **Device** while checking discovery, system bond,
   protected application authorization, profile127, MTU/indications and Ready.
   Existing ownership must match; evaluation factory reset is disabled. Stop on
   owner ambiguity rather than resetting or changing bonds.
3. Open **Group** only when ready to decide. Entering Group automatically sends
   READ and consumes the one-shot attempt. The invitation window is at most60s;
   setup and READ-send anchoring shorten the remaining phone window.
4. Observe **Confirm nearby peer**, a full peer fingerprint and confirmation
   transcript rendered as64 hexadecimal characters each, and the decision controls.
   Do not retain their raw contents. The counterpart is synthesized locally:
   this checks presentation and an explicit decision, not intended-peer comparison.
5. Tap **Confirm matching details** once. Accept only the terminal text:
   **Your device confirmed this step. Group joining is not complete.**
   Decision buttons must no longer be actionable. A submitted message alone,
   timeout, refusal, disconnect or missing response is not success.
6. End the observed case without retrying. Restore full original application span
   and NVS, verify protected regions independently, restart the original, verify
   the agreed original state and close custody. Preserve the phone's app/data;
   any cleanup of trial-only phone bond/state needs its separately agreed scope.

Record fixed categories and monotonic timings: boot/discovery/bond/authorization/
Ready/offer-render/decision/local-result/restoration. Never collect PINs, addresses,
USB serials, keys, private payloads or unredacted offer screenshots. A physical
pass demonstrates this phone-to-local-device flow only, not joining or traffic.

## Separate subsequent cases

| Case | Expected observation | Freshness requirement |
|---|---|---|
| Cancel | `Your device cancelled this confirmation.` | Separate controlled fresh baseline and grant |
| Disconnect after offer | Old offer unusable after reconnect; no spontaneous confirmed result or new usable offer | Separate controlled fresh baseline and grant |
| Connected expiry | Wait beyond original deadline; offer closes and buttons disappear | Separate controlled fresh baseline and grant |

Restoration to captured fresh NVS, not reconnect/reboot/factory reset, separates
cases. Generic UI reconnect guidance does not reset this evaluation's eligibility.
Manual disconnect timing is not deterministic late-indication injection; existing
software tests cover late results. Do not claim a physical late-response test from
this matrix without specific injection tooling.

## Result and remaining gate

Procedure and immutable inputs are prepared. No hardware execution, app installation,
firmware change, radio result, V1 progress or public website status change is added.
The required next implementation is the exact-span BLE trial operator/admission
path; current phone verification is retained privately. Request physical execution
approval only after that path and its restoration checks are concrete and validated.
