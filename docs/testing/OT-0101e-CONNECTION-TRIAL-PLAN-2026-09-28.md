# OT-0101e bounded protected-connection diagnostic plan

This is preparation for a new, separately authorized physical attempt. It is
not a completed trial or a demonstrated correction. Canonical project:
`C:\lu\OpenTrail`; selected worktree: `.private/ot0101e-gnss`.

## What the owner needs to know

- Find out why the phone stops connecting when the test firmware is installed.
- Use Trail Bench 2 and the S24 only. Leave Bench 1 alone.
- Keep both connected by USB. Leave the case closed and battery connected.
- We inspect the phone directly. You only read the Heltec screen when asked.
- We restore and check the Heltec's original software and saved settings.

## Exact experiment and limits

First verify the diagnostic V1-Test app with the original firmware: the saved
device must reach protected Ready and synchronize the display clock. Installing
the app is an update preserving data and the bond, after checking the selected
phone's installed package and signer. A signature conflict stops before update;
do not uninstall, clear data, forget the bond or enroll again.

The prepared APK is `.private/ot0101e-android-diagnostics/outputs/app/outputs/apk/v1Test/app-v1Test.apk`:
11,974,618 bytes, SHA-256
`79603e1a49b299f06908d9792658af61c85cf77bc498240693b67bb5684adec0`.
Package `io.github.nbjelanovic.otclient.v1test`, version code 1,
version `1.0.0-v1test`; certificate SHA-256
`bc60cc64be586444a0ce181e426e586ec74d55e764268089e3aa4e8f1dbfac06`.
The existing certificate matches the retained V1-Test recovery APK. Check it
against the selected phone's actual installation before updating. No signing
identity was created or changed. Offline assembly and final APK audit passed;
the 151 Android source inputs remained unchanged.

The diagnostic recorder is a Kotlin observer, not a ContentProvider endpoint.
Its bounded ASCII `files/v1-connection-log` stores only typed OTCL events.
The selected debug package can provide this file through `adb exec-out run-as`
for private analysis. Existing log UI also offers Save text file or sharing
through the app's unexported FileProvider with an explicit read grant. Do not
collect unrestricted Bluetooth logs or unrelated phone data.

Profile A is the first candidate attempt. It retains the candidate's battery/GPS
OLED display. Profile B masks only those copied display fields; sensor acquisition,
clock handling, security and the ordinary 4096-byte BLE stack remain identical.
Both builds include the same diagnostic instrumentation. Exact artifacts and
host evidence are in the [diagnostic preparation](OT-0101e-CONNECTION-DIAGNOSTICS-2026-09-28.md).

| Profile | Application bytes | SHA-256 |
| --- | ---: | --- |
| A | 593200 | `4351080e407ae8bf33370737e9313ecfb8ec6865e5bb1ed076d2fd094bc85c72` |
| B | 593232 | `6db05a3453870dbcf6c4c9583cd66ef5c64f25bc5b06a7b4eb9b5ae74d161388` |

Do not start B automatically. A must reproduce the protected connection failure
before B can distinguish display involvement, and B needs its own current
authorization and one-use attempt. If A succeeds, stop: the previous failure is
unexplained, rather than proven fixed. A different B result is comparative
evidence, not a complete cause or release claim.

One capture/phone observation window is limited to 600 seconds, starting before
the capture child is launched. There is no fresh GPS-reading window afterward.
Use one deliberately initiated saved-device connection after capture readiness;
retain automatic app behavior as observed behavior, without starting another
attempt to replace a failure. Never extend a timeout or weaken authorization.

## State, ownership and recovery review

| Transition and owner | Required observation | Forbidden effect | Failure/recovery |
| --- | --- | --- | --- |
| Original phone/device control; operator | Same signed package, saved relationship, protected Ready and clock synchronization | Candidate write before this control passes | Retain phone diagnostics and stop before candidate installation |
| Exact custody admission; maintained engine | Fresh electronic identity, current port, exact input hashes and one-use grant; no other hardware lease | Selecting by port order or replaying a consumed grant | Refuse before write; inspect custody if ROM preparation is interrupted |
| Original captures; ROM backend | Complete current application/NVS and protected bootloader/partition/OTA; accepted partition and original-prefix pins | Substituting old NVS backups for current state | Hold recoverable custody on incomplete prewrite capture; new restore-only authorization if required |
| Candidate install/readback; engine | Exact approved padded application, grant rechecked after backup and before boot | Protected-region writes or silent retries | Use the established restore/recovery path on uncertainty |
| ROM child releases port; backend | Candidate reset completes and child exits | ROM reverify/hold while passive capture owns the port | Do not start the phone connection until capture is armed |
| Passive capture acquires port; capture child | Re-enumerated inventory identity; DTR/RTS inactive; explicit `CONNECTION_CAPTURE_ARMED` | Serial writes, resets, two handles or assumed readiness | Open/read/readiness errors end observation and lead to restoration |
| Saved-device phone connection; operator/app | Correlated request initiation, raw bounded callback status/length, firmware security/access/GAP markers | Treating read admission, GATT connection or a UI phase as Ready | Preserve first failure and its lower-level observations; no reset/clear-data workaround |
| Observation completion; operator | A small typed terminal outcome, separate sanitized capture with size/hash | Full capture in the custody journal; unbounded raw logs | Cancellation, EOF, timeout and capture errors remain explicit |
| Capture release; parent | Child exits or is terminated and reaped before any ROM operation | Restoring while a capture handle/thread remains alive | Watchdog bounds a stuck read/close; uncertain child termination holds custody |
| Controller interruption; OS lifetime guard and recovery | Collector is bound to a private attempt lease and a kill-on-close Windows job before receiving its capture payload; fresh recovery verifies no live job processes before ROM claim | A new recovery process assuming an old collector has released USB | A surviving job, malformed lease or unverifiable OS query denies ROM access; no automatic candidate retry |
| Original restoration; engine/backend | Independent application/NVS readback and final sweep of all five spans; reset and closed journal | Assuming a successful write proves restoration | Exact restore-only recovery on failure; never rerun the candidate blindly |
| Physical closure; owner | Bench 2 usual Trail screen | Claiming screen operation from a reset command alone | Keep visual confirmation distinct from verified bytes |

## How evidence narrows the failure

The process guard uses the documented Windows job-object lifetime and accounting
interfaces. Its handle is non-inheritable; closing the controller's last handle
terminates the assigned process tree. Recovery still checks the durable lease,
Windows session and live-process count before permitting ROM access. See
[Microsoft's job-object lifetime documentation](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects),
[extended-limit structure](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_extended_limit_information)
and [accounting structure](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_accounting_information).

The phone log and firmware timestamps use different clocks. Align event order
and request boundaries; do not subtract their timestamps without an established
correlation. Keep capture gaps, queue drops, missing startup markers and resets
visible. The firmware drains events in its main task: absent events alone cannot
prove a request never arrived, especially if that task stalled or reset.

| Observed boundary | What it supports | What remains unproven |
| --- | --- | --- |
| Phone platform declines initiation | Failure before an admitted GATT read | GNSS/display caused it |
| Firmware protected access records a lock/security rejection | That application's rejecting check and bounded code | The underlying physical reason without surrounding markers |
| Firmware enters access but lacks exit, with live surrounding capture | A stalled or interrupted access boundary worth tracing | Stack overflow or display starvation without stronger evidence |
| Firmware exits successfully but phone reports failure | Failure beyond that application's encoding/return boundary | Which host/controller/link/platform layer lost or rejected the reply |
| Panic/reset category or host reset recorded | A runtime reset occurred near the failed sequence | Causal direction from timing alone |
| A fails and B succeeds with equivalent setup | Display involvement in this comparison | Acquisition independence, a product correction, or repeatable field behavior |

The original control, candidate boot, protected Ready, visible clock, diagnostic
capture and restoration are separate results. Sustained clock and GNSS
freshness/loss/recovery acceptance remain separate unfinished OT-0101e gates.
No V1 completion credit or public website status change follows from preparation.

## Host preparation evidence

The new connection runner uses the existing GNSS custody/restoration engine and
ROM transport. Legacy GNSS request/grant behavior remains available; diagnostic
requests/grants have separate schemas and exact A/B artifact pins. An explicit
original-control flag is an operator attestation, not independent proof.
The full sanitized capture is stored separately; the journal retains only its
bounded descriptor and typed outcome.

All 104 affected host checks passed: 20 connection operator/custody cases,
31 collector cases, 11 legacy GNSS cases, 29 existing BLE transport cases and
13 capture-lifetime cases. Three of the latter used real Windows processes,
without serial imports or devices: a live job denied release, forcibly killing
the controller terminated its assigned child, and closing the job terminated
an assigned child plus descendant. This is host recovery evidence, not physical
device restoration or Bluetooth acceptance.

The private exact-hash binding recipe is `.private/Prepare-ConnectionBindings.ps1`.
It freezes the runner, engine, collector, lifetime guard, transitive helpers,
tests, this plan, inventory registry, APK and existing isolated runtime manifest.
It issues no grant and accesses no device. A/B input checks use the maintained
isolated Python capsule, verify its complete inventory and import origins, and
probe the collector imports without serial enumeration/opening. A new physical
grant still requires the current device/phone setup and original control.

Private evidence: `.private/ot0101e-connection-operator-host-validation-2026-09-28.json`,
`.private/ot0101e-connection-custody-tests.log`, the Android package manifest/audit,
and the A/B image-info records. Existing firmware reproducibility and Android
tests/lint are reused because those product inputs did not change in this increment.
