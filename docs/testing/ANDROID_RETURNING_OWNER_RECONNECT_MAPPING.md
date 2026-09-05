# Android Returning-Owner Reconnect Mapping

Current successor (2026-09-04): Periodic saved-owner recovery passes on the retained Heltec/Note20 pair: three quick retries, then five-second scans separated by a 15-second wait. A 65.866-second ROM absence recovered automatically through fresh authorization and Snapshot; 385 Android tests/lint/build pass. See tests/hardware/OT-168-PERIODIC-2026-09-04.md. Cold-power, factory-reset, and two-pair acceptance remain open.
Earlier terminal-exhaustion observations below are historical.

Status: mapped and physically reproduced on the dedicated Note20 and one
verified Heltec on 2026-09-03. The first correction failed physical retest. A
narrow successor was host-validated, written application-only, independently
read back, visibly returned to runtime, and passed one immediate saved-owner
reconnect, app-process-death persistence, and one warm physical-reset reconnect
through Ready on 2026-09-04. Cold power-removal persistence remains untested.

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
   requires nonempty bonded-device inventories at scan start and callback time,
   and requires Android to report the scanned peer as BOND_BONDED. Android's
   private-address resolution makes scan-object membership in the earlier
   snapshot unreliable, so this discovery gate does not by itself prove that
   the same peer was bonded at scan start. Protected session admission still
   requires the existing application authorization secret.
3. The runtime waits for scan completion and accepts exactly one candidate.
   Zero candidates publish Idle; multiple candidates fail with
   RETURNING_OWNER_AMBIGUOUS.
4. One candidate enters beginConnection with isReconnect=false and purpose
   EXISTING_OWNER.
5. createConnection() constructs a PlatformGattLease; start() then checks the
   platform prerequisites and calls connectGatt() with AUTO_CONNECT=false.
6. A successful open must complete a fresh connection-local authorization
   claim using the retained owner secret, then proceed through protected
   ProtocolInfo, MTU, indication subscription, the mandatory Snapshot, and
   Ready without requesting a new PIN.

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

## Installed successor immediate reconnect acceptance

The follow-up source review found that pinned NimBLE performs a second store
initialization while the host starts. That later initialization can reload the
persisted CCCD state after the rejected correction cleared it. The successor
moves the bounded `value_changed` sanitation after host synchronization and
before advertising begins. It keeps bonds and subscription flags intact and
fails startup closed on any read/write inconsistency.

The target now compiles the pinned NimBLE central/GATT-client receive path so it
can accept and confirm an incoming standard Service Changed indication. Static
admission forbids central discovery, connection, subscription, and write
operations; the application remains a peripheral. Android permits exactly one
same-GATT service rediscovery after a failed initial discovery, with a 250 ms
grace for Service Changed callback/result ordering. The single retry also runs
if no Service Changed callback arrives. It does not use hidden cache refresh,
rebonding, bond removal, a new connection, or an unbounded rediscovery loop.

Returning-owner discovery no longer compares address-shaped
`BluetoothDevice` objects across private-address rotation. It requires a bonded
inventory at scan start and callback time plus the scanned peer's live
`BOND_BONDED` state. This is a bounded Android discovery compromise, not proof
that the same peer occupied the scan-start snapshot; every connection still
requires a fresh application authorization claim using the retained owner
secret, so a newly or unrelated bonded D0 advertiser cannot enter the protected
session without that secret.

The focused Android policy/runtime/manifest tests pass. All 16 Heltec target
admission groups, the pinned NimBLE-order regression, the authoritative
291-input raw-byte audit, and the complete Windows Host matrix pass. A
test-first preflight then exposed that reproducible ESP-IDF application builds
were disabled. `CONFIG_APP_REPRODUCIBLE_BUILD=y` is now an exact target-profile
requirement, and two initially absent, cache-disabled ESP-IDF 6.0.2 ESP32-S3
builds reproduce the application, ELF, map, bootloader, partition table,
generated configuration, and partition-source tuple exactly. The canonical
application is 563,824 bytes with SHA-256
`91D4CEB48CCFBCD21AC97CE604C48FBCCA04D70408D2BF749C90CB053AD04824`
and 89% of the smallest application partition free.

The installed Android application was read-only verified against the local
debug APK without reinstalling or changing app data. The first hardware
preflight stopped before any write because neither anonymous generic USB
serial endpoint admitted an application read; both endpoints were reset toward
normal runtime.

On a second prewrite gate, the owner placed the intended retained Heltec in ROM
download mode and Windows exposed exactly one anonymous generic USB serial
endpoint. The conservative no-reset, no-stub path read the complete 5,177,344-
byte factory application range at `0x10000`. Its first 551,584 bytes did not
match the recorded installed test-unit application, so qualification stopped
before any write. The temporary nonqualifying read was removed and a hard-
reset/run command was attempted. After one requested normal reset with BOOT
released, the owner reported that the display returned. No phone, firmware
bytes, or durable device state was intentionally changed; the reset restored
volatile runtime and the display. No reconnect result is claimed.

The owner then confirmed with certainty that this was the same retained test
Heltec used for the phone work. Host audit independently validated the original
prefix-hash method and confirmed esptool does not change application bytes at
`0x10000`.

The owner-directed successor enrolled the isolated unit's factory ESP32-S3 base
MAC as private development identity `OT-DEV-001`, read the exact complete
5,177,344-byte application range, and matched the same base MAC again afterward.
The retained range has SHA-256 `BC2421E0D11AE84FD38C17F49A182E1215DAF5F9BD38BDFA62DD8178092D1EFF`.
Its first 563,824 bytes exactly match the local pre-reproducible successor build
`587A2D55D68EF8C19E9D48308EDD4B7AD6D4495F43A5618BD71335ED66BD0E81`.
That build and the canonical reproducible `91D4CEB4...` candidate differ at only
65 bytes: the embedded ELF digest and the dependent checksum/validation hash;
no other byte differs. This resolves the earlier mismatch as a stale
`B34C95FE...` baseline, not unknown device or flash drift.

After a host power interruption, no write had begun. The owner reaffirmed
`proceed`, and fresh exact artifact, recovery, endpoint, phone, and private
`OT-DEV-001` identity checks passed. The installed prewrite prefix matched the
retained `587A2D55...` predecessor. The canonical 563,824-byte application
`91D4CEB4...` was then written only at `0x10000` and independently read back at
the exact length with the same SHA-256. The required sector erase ended at
`0x99FFF`; its 1,424-byte application tail was all `0xFF` before and after.
Esptool reported no internal retry, and the private factory identity matched
`OT-DEV-001` before and after the operation. No bootloader, partition table,
OTA data, NVS, eFuse, or other region was written.

The dedicated Android 13 Note20 `SM-N986U` retained its app data and saved bond.
Its installed debug APK matched the local artifact at SHA-256
`3DDB3F596A640DD087112864EBB75A326D7E8E6C0C0FEA19BA80974E9A414EEB`,
and the app and foreground service were force-stopped before the firmware gate.
After the owner confirmed the display returned, the existing app was opened,
Bluetooth device mode was selected, and the connected service was started once
at 2026-09-04 18:53:13 EDT. The UI first reported `Checking for authorized
device` and `No PIN or new pairing requested`. After the full scan it reported
`Connected to Authorized device 1`, `Authenticated companion session. Device
state remains authoritative.`, and a mandatory Snapshot of radio unavailable,
GNSS unknown, power unknown, position stopped, and queued 0. No new PIN,
re-pair, app-data clear, or reinstall occurred.

By itself, this passes the one authorized immediate saved-owner reconnect
through Ready. It is not a separate second reconnect, device power-cycle
persistence test, reset-acceptance test, or two-device result.

### App-process and warm-reset persistence

At 19:12:03 EDT, Android `am force-stop` and a following `pidof` check
confirmed complete app-process death. A cold MainActivity launch returned to
the current mode chooser, and the observed Bluetooth mode button was selected.
No assistant service-start tap occurred in this run; that absence alone does
not prove automatic service start. The fresh app process
(`PID 28105`) called GATT connect at 19:13:10.089, reached link status 0 at
19:13:10.382, discovery status 0 at 19:13:10.388, and MTU 151 status 0 at
19:13:11.004. The UI showed stable Ready with the mandatory Snapshot at
19:14:05. This proves retained-owner persistence across app process death using
the current chooser, not the finished zero-tap launch flow.

While that session remained connected, the owner performed one brief physical
RESET with BOOT untouched and USB attached. The display returned, and no phone
input followed. The same app process logged old-link loss/close at
19:14:47.742..19:14:47.751, called reconnect at 19:14:48.761, and reached new
link status 0 at 19:14:49.235, approximately 1.493 seconds after the old-link
callback. Discovery status 0 followed at 19:14:49.239, MTU 151 status 0 at
19:14:49.877, and indication enablement at 19:14:49.879. Ready with a fresh
mandatory Snapshot reporting the same values was visible at 19:15:41; the
session remained connected.
The exact full Ready latency was not instrumented.

The unchanged canonical `91D4CEB4...` firmware, installed `3DDB3F59...` APK,
saved bond, and app data were retained. No flash, ROM read, erase, rebond,
reinstall, data clear, new PIN, radio operation, or second-device work occurred.
This accepts warm physical-reset persistence only, not cold power removal,
factory-reset recovery, a second pairing, or finished zero-tap launch.

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

### Extended absence: physical failure confirmed

The owner-directed ROM absence on 2026-09-04 reproduced the limitation on
OT-DEV-001/Note20 with unchanged firmware/APK. Three reconnect calls failed
with GATT status 133; the app reached terminal exhausted-retry UI roughly
22 seconds after link loss. The Heltec was hard-reset back after approximately
4 minutes 38 seconds from verified ROM entry, and the owner confirmed its
display returned. No phone input occurred. At 78.664 seconds after reset
command completion, the same process remained in terminal failure with no
post-return reconnect call. This fails automatic long-absence recovery, not
the previously accepted immediate/warm-reset paths. Exact timestamps and
limitations are in the [dated evidence](../../tests/hardware/OT-168-2026-09-04.md).

The accepted warm reset recovered during the first fast reconnect attempt.
Current `BleCompanionRuntime` uses `DEFAULT_MAX_RECONNECT_ATTEMPTS = 3`, with
1/2/4-second backoffs plus each attempt's own timeout. Exhaustion clears the
selection and publishes terminal `RECONNECT_EXHAUSTED`; it does not schedule
another scan. Keeping the app open therefore does not yet provide automatic
return after an extended out-of-range or powered-off period. Persistent,
battery-conscious saved-owner rediscovery and long-absence physical acceptance
are separate outstanding work. The owner agrees that the open app should
periodically retry after the fast phase without requiring `Scan again`;
explicit disconnect must stop that activity. This requirement is not yet
implemented. The measured warm-reset link interval is not a
promise of long-absence or full Ready latency.

- Existing clean first-connection acceptance remains valid and must not regress.
- The saved bond and app state are retained for the first corrected run.
- Service discovery completes with a nonempty exact OpenTrail GATT profile and
  no incorrect-opcode/empty-database failure.
- The returning-owner connection completes a fresh claim with the retained
  owner secret and reaches protected ProtocolInfo, mandatory Snapshot, and
  Ready without a new PIN.
- Only after immediate saved-bond reconnect passes should the Heltec
  power-cycle persistence test be attempted.
- A finished production launch with an existing authorized bond begins and
  completes reconnect without a mode-selection or service-start tap.
