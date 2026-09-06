# OT-177 authorization rejection investigation - 2026-09-06

Host-only authorization investigation confirms that Android GATT insufficient authorization collapses several firmware admission and request failures. Two new adapter tests compare normal restored subscription with a deliberately dropped event, and reject a wrong-session claim before any authority decision. These demonstrate possible paths, not the physical root cause. Existing diagnostic strings and INFO USB-console configuration were verified in the exact retained firmware artifact. No production code, firmware, phone or device changed.

## Source findings

Android `AndroidGattStatusPolicy.failure` maps platform status 8 (insufficient
authorization) to `BleGattFailure.AUTHORIZATION_REJECTED`; runtime and format-3
trace preserve `GATT_AUTHORIZATION_REJECTED`. The trace captures neither exact
callback-operation provenance nor raw numeric platform status. Successful setup
followed by authorization failure makes the command-write response a hypothesis,
not proof of the originating callback.

Firmware `companion_nimble_gatt.cpp` has two relevant rejection stages:

- `BLE_GAP_EVENT_AUTHORIZE`: current security/binding refresh and exact attribute
  admission. Command admission requires ProtocolInfo read, subscription recorded,
  no pending response, and provisional or promoted phase.
- `command_access`: any non-pending `service_command` result returns ATT
  insufficient authorization. Wrong session, malformed request, stale exchange,
  unavailable response/correlation, reset containment and other failures can
  collapse into this response. It does not establish an app-level Denied decision.

The adapter and lifecycle explicitly clear session/binding/pending state on
disconnect. No missing-disconnect-cleanup defect was established.

## Restored subscription hypothesis

Read-only inspection of installed ESP-IDF 6.0.2 NimBLE found that
`ble_gatts.c` suppresses the subscribe callback for unchanged CCCD flags.
`ble_gap.c` delivers encryption change before bonded subscription restoration.
The target SUBSCRIBE handler refreshes security and opens the provisional session
before recording subscription. Thus ordinary successful restoration is not erased
by a later initial session open; that broad ordering hypothesis was rejected.

A narrower conditional sequence remains possible: an early restored SUBSCRIBE
event fails security/binding refresh and is dropped; a later ProtocolInfo refresh
succeeds; Android writes an unchanged enabled CCCD, producing no replacement
subscribe event. Firmware can then report subscription absent despite Android
write success. The physical trace does not prove the prerequisite dropped event.

## Deterministic validation

`companion_gatt_authorization_adapter_tests.cpp` adds two groups:

1. Compare successful early subscription with deliberately failed early security
   refresh/event omission, followed by successful protected read and MTU increase.
   Normal claim is pending; omitted event leaves admission blocked. No authorization
   decision is invoked. This executes the adapter, not NimBLE callbacks.
2. After complete setup, a wrong-session claim passes attribute admission but
   fails request validation without invoking authority or reserving a response.

Focused C++17 UCRT64 build used `-Wall -Wextra -Wpedantic -Werror -O2` and the
same eight adapter-suite sources as `tools/Test-Host.ps1`: PASS 14 groups.
Unchanged previously built lifecycle and durable-owner executables were reused:
PASS 20 and 12 groups. Target admission source suite: PASS 16 groups. Total
focused coverage: 62 groups. Independent review found no weakening or claim
that these models reproduce the actual physical cause. No Android or firmware
source changed, so no new APK or board build was required. Full prior checkpoint
CI `df87b72` passed GitHub run 34010494317; this is separate from these new tests.

## Existing device-side diagnostic capability

Retained `build/targets/heltec_v4_returning_owner_repro_a/opentrail_heltec_v4_bench.bin`
is 563,824 bytes with SHA-256
`91D4CEB48CCFBCD21AC97CE604C48FBCCA04D70408D2BF749C90CB053AD04824`.
Its matching sdkconfig enables USB Serial/JTAG console and INFO default/max
level 3. The exact binary contains existing `claim authorize` and `claim command`
format strings. This verifies compiled capability, not current device identity,
live output availability, or a safe-open result.

| Future observed log | Distinguishing branch |
| --- | --- |
| nonzero authorize refresh | security/binding refresh |
| refresh=0, accepted=0, sub=0 | subscription admission absent |
| refresh=0, accepted=0, pending=1 or incompatible phase | lifecycle admission |
| accepted=1 followed by command disposition/error | later request handling |

Capture only fixed diagnostic fields. Do not retain identifiers, keys, raw
traffic or generic console output. No device access occurred in this increment.

## Next gate and progress

Obtain separately authorized bounded USB-console receive evidence during one phone connection, without flash, reset, ROM entry or ownership changes. Use existing claim authorize refresh/accepted/phase/info/sub/pending and claim command disposition/error fields to distinguish the rejection branch. Verify exact device, current console availability and no-reset open behavior first. Select a correction only after the branch is established; website updates remain owner-deferred.

No physical root cause, fix, reconnect acceptance or release credit is claimed.
Every milestone remains unchanged: Android 60%; V1 exact 43.75% / displayed 44%.
The prior initial authorization failure and older Snapshot-phase failure remain
separate unresolved observations. Website synchronization remains deferred for
the owner-requested bulk update.
