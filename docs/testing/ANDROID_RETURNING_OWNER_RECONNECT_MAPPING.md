# Android Returning-Owner Reconnect Mapping

Status: mapped, diagnosed on the dedicated Note20, corrected, flashed
application-only to one verified Heltec, and physically retested on 2026-09-03.
The post-correction saved-bond attempt reproduced the same service-discovery
failure, so the correction is not accepted.

## Physical evidence boundary

The clean Note20 and Heltec run already passed the required first-connection
baseline: passkey bond, claim, mandatory same-session Snapshot, Android Ready,
and a stable connected panel.

The separate immediate saved-bond test then failed after the user selected
Bluetooth device mode again:

- Android did not request another PIN;
- the app ended at BleRuntimeState.Failed(CONNECTION_START_FAILED);
- the public text was "The selected companion connection ended or could not start.";
- Bluetooth remained enabled;
- Android''s bond inventory contained one bonded LE device advertising as
  "nimble"; and
- no reset, re-pair, source change, reflash, or retry followed that failure.

This was an immediate in-app returning-owner attempt, not yet the distinct
power-cycle persistence test.

A later diagnostic APK preserved the saved bond and reproduced the attempt
exactly once. Android admitted one returning-owner candidate, created and
started the GATT lease, connected at the link layer, and called service
discovery. The app then reported the identity-free support code
`BLE-GATT-PLATFORM-FAILURE`. Address-redacted Android system logs showed ATT
opcode `0x1d` (Handle Value Indication) arrive while discovery was active,
followed by an incorrect-discovery-opcode warning, an empty GATT database, and
the app's fail-closed close. This excludes scan admission, lease creation,
synchronous `connectGatt()` rejection, and ordinary pre-profile disconnect.

## Exact returning-owner path

1. BleCompanionRuntime.onLifecycleStart() asks the Android facade for a
   returning-owner scan when no in-memory selection exists.
2. AndroidBluetoothGattFacade admits only a D0 advertisement, rejects D1,
   requires the device to exist in both the scan-start and current bonded-device
   snapshots, and requires BOND_BONDED.
3. The runtime waits for scan completion and accepts exactly one candidate.
   Zero candidates publish Idle; multiple candidates fail with
   RETURNING_OWNER_AMBIGUOUS.
4. One candidate enters beginConnection with isReconnect=false and purpose
   EXISTING_OWNER.
5. createConnection() constructs a PlatformGattLease; start() then checks the
   platform prerequisites and calls connectGatt() with AUTO_CONNECT=false.
6. A successful open must proceed through protected ProtocolInfo, MTU,
   indication subscription, the mandatory Snapshot, and Ready without a new
   authorization claim.

The observed terminal CONNECTION_START_FAILED is therefore not the zero-match
scan result; that path is explicitly Idle. It proves only that connection
creation or the early GATT transport failed. The current public error collapses
several internal boundaries and is not sufficient to select a production fix.

## Characterized failure boundaries

| Boundary | Current result | Selection | Retry |
|---|---|---|---|
| No admitted D0 bonded candidate | Idle | cleared | none |
| createConnection() returns null | immediate Failed(CONNECTION_START_FAILED) | cleared | none |
| Lease exists but start() returns false | Reconnecting, attempt 1 | retained | 1 s, then bounded 2 s and 4 s delays |
| GATT opens, then disconnects before profile completion | Reconnecting, attempt 1 | retained | 1 s, bounded |
| Transient GATT failure | Reconnecting until exhausted | retained until exhausted | bounded |
| Platform or permission GATT failure | Failed(CONNECTION_START_FAILED) | cleared | none |

The default maximum is three retry attempts. Exhausting transient retries ends
as RECONNECT_EXHAUSTED, not CONNECTION_START_FAILED.

## Tests added before any runtime change

BleCompanionRuntimeTest now proves:

- returningOwnerNullConnectionFailsWithoutRetry: an admitted candidate whose
  facade cannot create a lease fails immediately, creates no GATT lease, and
  schedules no retry;
- returningOwnerStartFailureSchedulesBoundedRetry: a created lease whose
  start() returns false is closed and schedules exactly one 1-second retry,
  followed by the existing 2-second backoff; and
- returningOwnerEarlyDisconnectCancelsProfileTimerAndSchedulesRetry: an early
  disconnect closes the active GATT lease, cancels the 45-second profile timer,
  and leaves exactly one 1-second reconnect timer.

The pre-existing tests continue to prove zero-candidate Idle, ambiguous
candidate failure, wrong-owner rejection, the returning-owner happy path
through Snapshot and Ready, and bounded later reconnect behavior.

Focused validation command:

    gradlew.bat :app:testDebugUnitTest --tests "io.github.nbjelanovic.otclient.BleCompanionRuntimeTest"

Result: BUILD SUCCESSFUL in 40 seconds; 24 actionable tasks, 2 executed and 22
up-to-date. The first attempt did not reach compilation because the shell lacked
an Android SDK location. The passing rerun used the existing local Android SDK
through a process-only `ANDROID_HOME`; no `local.properties` file was added.

## Validated correction and exact next step

Pinned ESP-IDF 6.0.2 source establishes the conditional race. Its standard
Service Changed characteristic is indication-capable. When bonded encryption
is restored, NimBLE restores persisted CCCDs and synchronously emits an
indication for any record whose one-shot `value_changed` bit is still set. That
can occur before Android completes service discovery and does not require an
OpenTrail command. OpenTrail's explicit custom indication path remains
command-driven and is not reachable at this boundary.

After the test image was flashed and independently verified, the app and its
foreground service were force-stopped without clearing app data or the Android
bond. A fresh service-owned returning-owner scan then ran for the full 30-second
window, admitted one candidate, opened the BLE link, and immediately entered
service discovery. Address-redacted Android logs again recorded ATT opcode
`0x1d`, the incorrect-discovery-opcode warning, an empty GATT database, and the
app's fail-closed disconnect. No authorization, Snapshot, normal command,
reset, re-pair, or second-device work followed. This proves that clearing the
persisted CCCD `value_changed` fields at the current startup location was
insufficient; it does not prove whether another path restores or recreates the
pending indication.

The Heltec startup now clears only persisted CCCD `value_changed` bits after
the NimBLE store is initialized and before it is exposed to the host. It does
not clear bonds, delete CCCDs, alter subscription flags, remove the standard
GATT service, change handles, modify Android timing, or change authorization.
Any read/write inconsistency fails startup closed.

The new pinned-source regression first failed on the missing safeguard and now
passes. All 16 Heltec target-admission groups pass. The complete host matrix ran
through all groups; its sole initial failure was this document's local SDK
path, which was removed, after which both publication-safety gates and all
remaining matrix checks passed. ESP-IDF 6.0.2 built the ESP32-S3 application
without compiler or linker warnings.

The 551,584-byte candidate was written only to the selected test Heltec's
application partition at `0x10000` and independently verified. Its one allowed
saved-bond attempt failed as described above. The exact next action is a
read-only source/runtime diagnosis of why the pending indication still exists;
no additional hardware attempt is justified until a new narrow hypothesis and
test fail first.

## Accepted finished-app reconnect experience

For an already paired and authorized device, opening the finished Android app
must automatically enter the production Bluetooth path, start or attach the
connected-device service from the visible app, discover the saved owner, and
reconnect through Ready with zero additional user interaction. The current
mode chooser and explicit service-start button are development scaffolding,
not the finished product flow. Local test mode must not remain in the production
UI; retaining it in a debug-only build is a separate implementation decision.
Add Device remains an explicit recovery/enrollment path and must not replace
automatic saved-owner reconnect.

## Acceptance gates

- Existing clean first-connection acceptance remains valid and must not regress.
- The saved bond and app state are retained for the first corrected run.
- Service discovery completes with a nonempty exact OpenTrail GATT profile and
  no incorrect-opcode/empty-database failure.
- The returning-owner connection reaches protected ProtocolInfo, mandatory
  Snapshot, and Ready without a PIN or claim.
- Only after immediate saved-bond reconnect passes should the Heltec
  power-cycle persistence test be attempted.
- A finished production launch with an existing authorized bond begins and
  completes reconnect without a mode-selection or service-start tap.
