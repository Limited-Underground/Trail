# OT-177 corrected-firmware reconnect acceptance - 2026-09-06

The corrected firmware was installed application-only on the identity-verified retained Heltec and independently read back. The existing isolated V1-Test app on the Note20 passed four saved-owner connections: initial startup, full app restart, and two explicit service disconnect/reconnect cycles. Each completed fresh authorization and Snapshot before Ready; the console recorded no response-path error. This accepts the bounded single-pair correction, not cold-power, factory-reset, two-pair or production-release behavior. Website updates remain owner-deferred.

## Exact setup and installation

Source/publication checkpoint `611b51abcdd441c6b2c12cc98f077520284f5439` passed
GitHub Host validation run 34013493218 before this physical attempt. Reused its
accepted host matrix, independent review, firmware preflight and two matching
builds; no firmware or Android source changed during this acceptance increment.

Retained Heltec WiFi LoRa 32 V4 / V4.2, ESP32-S3, role OT-DEV-001, matched its
existing private identity registry after fresh sole native USB enumeration.
No identifiers were added to the registry or retained in the diagnostic output.
The phone was the retained Samsung Note20 Ultra SM-N986U, Android 13.

Candidate application: 563,776 bytes, version `ot177-reconnect-v1`, SHA-256
`9ACDC90EEA9D0489ABABDD9B6F4E3C7AFF97162D03DDC09F8E4965D5A1D12784`.
Exact retained restoration application: 563,824 bytes, SHA-256
`91D4CEB48CCFBCD21AC97CE604C48FBCCA04D70408D2BF749C90CB053AD04824`.
Installed V1-Test APK readback matched the unchanged 11,663,306-byte artifact
SHA-256 `CB07BCFFB15082EA9980D2BDC1D97811E22C360FCEF492E0B9CB247A44B19431`.
No APK installation was performed.

The reviewed one-use installer reused the existing no-stub ROM procedure with
esptool 5.3.1 at 115200 baud. Preflight independently read and verified the exact
partition table, erased OTA selection (factory boot), and 565,248-byte application
sector span against the old image plus its 1,424-byte FF tail. Identity and the
candidate hash were checked again before writing.

Exactly one uncompressed application write used offset 0x10000 with flash
size/mode/frequency kept. The erase span was 0x10000 through 0x99FFF. The new
563,776 bytes plus all 1,472 FF tail bytes were independently read back and
matched. No tool retry was reported. The image is 48 bytes smaller, so its longer
FF tail intentionally replaces the final 48 old-image bytes. Bootloader,
partition table, OTA selection, NVS and every other partition were outside the
write range. No owner/bond/data clear or restoration write occurred.

Identity was matched again before normal reset. Reset command success was
followed by two observed application heartbeats at device uptime 16,101 and
21,101 ms; runtime return was therefore observed independently of esptool.
The runtime reader wrote zero serial bytes. Radio frequency profile and board
bindings remained unchanged; no LoRa, GNSS or battery capability is accepted here.

## Bounded phone sequence and observed result

1. Start the existing V1-Test app, choose Bluetooth mode, and start its visible
   service. Require fresh protected setup, authorization, Snapshot and Ready.
2. Force-stop the app, relaunch it, choose Bluetooth mode and start its service.
   This is a full process restart; it is not a zero-tap background-launch test.
3. Select Disconnect Bluetooth device, then Disconnect and change mode; choose
   Bluetooth mode and start the service again.
4. Select Disconnect and change mode, then choose Bluetooth mode and start the
   service again. No pairing scan or new bond was requested.

All four returning-owner discoveries completed in approximately 30 seconds.
The following timings start at the relevant protocol stage and exclude discovery:

| Connection | Authorization ms | Snapshot ms | Connection attempt to Ready ms | Result |
| --- | ---: | ---: | ---: | --- |
| Initial connection | 194 | 99 | 1401 | PASS |
| Full app restart | 198 | 94 | 1759 | PASS |
| Service cycle 1 | 199 | 94 | 1372 | PASS |
| Service cycle 2 | 191 | 98 | 1376 | PASS |

The format-3 trace uses process session 9 for initial connection and session 10
for the restart and two later cycles. Each distinct transaction records
AUTHORIZATION_ACCEPTED, SNAPSHOT_ACCEPTED and READY_REACHED. A final fresh UI
inspection also showed Connected to Authorized device 1. The UI continued to
label radio unavailable, GNSS/power unknown and position stopped; quick-status,
position and reset actions were not invoked.

Receive-only console capture retained 16 allowlisted rows: four successful
attribute admissions for Claim and four for Snapshot, with four pending Claim
results (disposition 1, error 0) and four normal response results (disposition 2,
error 0). All admitted attributes had refresh 0, accepted 1, info/sub 1, pending 0.
No busy-slot error or boot marker was observed. Capture stopped by operator after
the final successful cycle. No generic console output, identifiers, keys, PINs
or private traffic were retained.

The earlier failures remain historical observations. The new run demonstrates
that the corrected exact artifact passes this bounded sequence; it does not
recover the missing historical nonce values or prove every past failure's cause.

## Evidence fingerprints and handback

- Installation receipt: `510DDCADB87A110F3AFC9239BC597DEE59CB1D23959AD7F7ACB608B3FF4732CE`.
- Runtime heartbeat receipt: `17D16C9B087CFD05CE9B62F8EAED51D22E29660B565440D324081EA412A7D4B5`.
- Allowlisted console record: `7C728DC427D9398FBC82E99FB9607772791E4DF0C58C18167CB2628F5E62D593`.
- Final format-3 trace: `EBD49510707D680967649C9367209FE9318A3A431E2886A3AC8A4DAF64F24B9F`.
- Phone handback receipt: `412C0665BE7055F9BBB28930A93F7AACB5BC62F3F0FD33857FE200165110DC60`.

Both phone apps were force-stopped and verified stopped. Their installation
versions/timestamps, display size/density, font scale and rotation settings were
unchanged. The temporary UI dump was removed and all serial captures closed.
The verified corrected firmware remains installed; the old image remains available
for recovery. No further device connection or firmware attempt followed handback.

Prepare a bounded cold-power saved-owner recovery test with the exact installed firmware and current V1-Test APK. Verify how to remove all device power safely (USB and battery), preserve the current owner/bond, and define fresh Ready/Snapshot and handback gates before requesting the necessary physical power action. Do not repeat installation, reset ownership, or start destructive factory-reset testing as part of that preparation.

This restores evidence within already credited single-pair capability. Milestone
values remain unchanged: Android 60%, Heltec target 25%, core firmware 65%,
weighted V1 43.75% / displayed 44%. Cold-power persistence, factory-reset erasure,
coherent two-pair operation, authenticated LoRa and signed production release
remain open. Website synchronization/deployment is deferred to the owner's bulk
update; no public completion percentage changes.

Canonical validation passed: governance 10 groups, Android release-admission 23,
publication-safety scan, unchanged milestone/history checks and diff checks.
Independent review matched every receipt fingerprint and all four timing rows.
