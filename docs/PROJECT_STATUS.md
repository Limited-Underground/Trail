# OpenTrail Project Status

As of 2026-09-14. This page summarizes accepted behavior and the next work; dated
history belongs in [PROGRESS_LOG.md](PROGRESS_LOG.md). The complete prior status,
including assumptions and older decision checkpoints, is preserved in the
[2026-09-08 archive](history/PROJECT_STATUS_BEFORE_CLEANUP_2026-09-08.md).
See [archive navigation](history/README.md) for moved headings, [Architecture](ARCHITECTURE.md)
for design and the [backlog](../tasks/BACKLOG.md) for every task identifier.

## Current accepted capabilities

The [version-2 invitation contract](testing/OT-228-INDEPENDENT-INVITATIONS-2026-09-14.md) validates separate boot contexts and local clock windows while retaining one common signed Noise prologue and durable one-use admission. 93 new host groups pass. This removes shared boot/time requirements from the contract; the existing endpoint still requires a successor binding before real-device provisioning and radio/phone integration.


The [independent evaluation endpoints](testing/OT-227-INDEPENDENT-HANDSHAKE-ENDPOINTS-2026-09-14.md) now complete actual cryptographic handshake and separate local confirmation over bounded host transport. They own independent session/storage/Ready state and expose no membership or traffic authority.38 endpoint groups and the full affected security matrix pass. The frozen invitation still requires compatible local boot contexts and clock windows; different retained boot generations refuse. Real provisioning, radio/phone integration and complete lifecycle acceptance remain open.


- Two Heltec/Android pairs have protected BLE authorization, Snapshot/Ready,
  saved-owner reconnect, name and region readbacks, and automatic clock sync
  evidence. These are bench-development results, not field or release acceptance.
- The second Heltec/S24 pair has accepted fresh pairing, a recognizable setup label
  retained after timeout, an editable first-use name suggestion, durable Trail
  Bench 2 name and US915 restoration, and warm/app-only recovery. US915 selection
  does not enable radio transmission.
- The latest S24-only app update preserves data/base app and passed exact installed
  readback. It prioritizes authorization controls, removes a misleading mode switch,
  and persists fresh-setup navigation after exact reset verification. Pending reset
  verification wins; authenticated Ready clears the marker. It does not delete
  Android bonds or reconstruct resets performed by an older app.
- The bounded Noise XK radio trial and corrected five-operation mbedTLS comparison
  passed on both Heltecs, with independent restoration of both original images and
  protected regions. These are crypto benchmark results, not product messaging.
- Corrected mbedTLS matched resources, independent deterministic Noise proof and
  an additive entropy lifecycle guard are now host-validated. The Noise proof also
  produced a narrowly corrected adapter for a real-library nonnull contract issue.
  See the [consolidated admission evidence](security/CRYPTO_ADMISSION_BATCH_2026-09-10.md).

## Last verified bench configuration

| Pair | Firmware | Android app | Evidence boundary |
| --- | --- | --- | --- |
| Original Heltec / Note20 | `ot178-phone-v1`, 586,736 bytes, SHA prefix `43AC6DBC` | V1-Test SHA prefix `CCC4F1EB` | Retained paired control; still needs the newer clock correction. |
| Second Heltec / S24 Ultra | `ot171-label-v1`, 587,968 bytes, SHA prefix `984E241D` | V1-Test 12,505,942 bytes, SHA prefix `5FD10EF0` | Trail Bench 2 / US915; latest app-only Ready observed in 31.015 seconds with protected name/region readback and clock-sync acknowledgement. |

These are recorded observations, not a fresh live-device inventory. Full hashes,
setup, and results are in [pairing-flow evidence](testing/OT-171-PAIRING-FLOW-2026-09-08.md)
and [setup continuation](testing/OT-171-SETUP-CONTINUATION-2026-09-07.md).
The latest Android matrix passed 1,104 tests with no failures/errors/skips, all
variant lint/build tasks and unsigned-release audit. The release APK remains
unsigned. Physical fresh post-reset behavior of the latest navigation marker and
normal/large-font candidate visibility remain untested by that update.

## Next meaningful capability

The durable invitation authority is now integrated with isolated target NVS and
trusted startup. The [actual target host proof](testing/OT-208-INVITATION-TARGET-HOST-2026-09-12.md)
passes 130 groups; two [final firmware builds](../tests/benchmarks/crypto/OT-208-INVITATION-TARGET-BUILD-2026-09-12.json)
match all 7 artifact pairs after the documented stack configuration rebuild.
The [SDK inventory](security/OT-209-SDK-LICENSE-INVENTORY-2026-09-12.md) binds allocated
composition and supplied notices to the exact candidate, with supplier/prebuilt
provenance and license limits retained. It does not provide legal clearance.

The [exact-image operator binding](testing/OT-210-INVITATION-OPERATOR-2026-09-12.md)
passes 300 affected tests across 17 suites, two isolated runtime probes
and synthetic package admission. It preserves one-use capture and independent
restoration, and rejects retained state in all eight physical namespaces.
The [audited OT-211 trial](testing/OT-211-INVITATION-TRIAL-2026-09-12.md)
has status `evaluation_failed`. A: evaluation `capture_failed`; strict receipt accepted `false`; BEGIN observed `true`; matching receipt observed `true`; host read bytes `65`; capture at deadline (reported elapsed ms `30000`, clamped); inner endpoint error `endpoint_read_late`; durable stage/error `send_return/none`. B remained untouched by the candidate.
Candidate-touched originals and protected regions were independently restored/verified
and restarted; untouched roles were guarded-released. Custody is closed and grants consumed.
This attempt does not confirm the invitation candidate on both boards. A durable stage or successful send alone cannot substitute for strict receipt acceptance.
The [corrected OT-212 operator](testing/OT-212-DEADLINE-OPERATOR-2026-09-13.md) passes 335 tests in 18 suites, two isolated runtime probes and synthetic package admission. Both deadline crossings now finish observation without another raw read; empty, malformed, late and trailing output remain refused. The [audited OT-213 trial](testing/OT-213-DEADLINE-TRIAL-2026-09-13.md)
has status `pass`. Both roles accepted matching BEGIN markers and strict pass receipts for the invitation candidate through the corrected OT-212 host operator.
Candidate-touched originals and protected regions were independently restored/verified
and restarted. Per-role restoration/release is audited; custody is closed and grants consumed.
This confirms the bounded normal-path invitation evaluation independently on both boards. Its accepted receipt depends on the target's durable boot/role consume, reconstruction refusal, authenticated exchange, duplicate refusal, cancellation and retirement checks.
The [phone confirmation plumbing](testing/OT-214-GROUP-CONFIRMATION-2026-09-13.md) now connects the existing Group screen to exact one-use service-owned offers. Its production adapter remains unsupported; no product join or device acceptance is added.
The [device confirmation owner](testing/OT-215-DEVICE-CONFIRMATION-2026-09-13.md) now replaces direct automatic confirmation in an additive evaluation target with exact pending offers and durable confirm/cancel effects. Owner/actual-target host tests and twin firmware builds pass; transport and human inputs remain synthetic.
The [protected BLE evaluation integration](testing/OT-216-BLE-CONFIRMATION-2026-09-13.md) now connects that owner to the Android coordinator through an opt-in profile. 1,213 Android tests, 120 new C++ groups, affected regressions, two matching evaluation builds and a default-control build pass. Known host-reset/exit faults revoke admission immediately. The counterpart remains locally synthesized; no physical BLE or two-node product acceptance is added.
The [physical trial preparation](testing/OT-217-BLE-TRIAL-PREPARATION-2026-09-13.md) verifies exact inputs and the required 733184-byte original-app recovery span. Both connected phones have different, signature-compatible V1-Test builds; SM-N986U/Android13 is selected for the first case. No installation or board operation occurred.
The [physical BLE attempt](testing/OT-219-BLE-PHYSICAL-TRIAL-2026-09-13.md) installed the exact OT-216 APK in place and exercised one candidate write/readback/boot command. Candidate Ready was not reached; confirmation remained unrequested. Originals were independently restored and the same updated app/bond reached protected Ready again. A pre-custody ADB stdin collision was corrected. The [OT-221 diagnostic operator](testing/OT-221-STARTUP-DIAGNOSTICS-2026-09-13.md) captured successful self-check/runtime startup and heartbeats followed by a restart sequence. Full originals were restored and custody closed; the same phone/app/bond reached protected Ready after restarting the app process and its Bluetooth service. [OT-222](testing/OT-222-RESET-REASON-2026-09-14.md) captured software CPU reset0x0C, but its caller remains unknown. Originals and phone Ready were recovered. [OT-223](testing/OT-223-CORRECTED-CAPTURE-2026-09-14.md) returned zero capture bytes with the proposed no-reset release. That release change was reverted and the redundant-reset claim withdrawn. Original-only verified hard reset recovered same-session phone Ready. [OT-224](testing/OT-224-CRASH-EVIDENCE-2026-09-14.md) now positively identifies an `ot_ble_host` stack overflow followed by software CPU reset. Matched-ELF decoding confirms the FreeRTOS overflow hook; the corrupted backtrace does not identify the full overflowing call chain. Originals were restored and the same phone/data/bond reached protected Ready. [OT-225](testing/OT-225-BLE-STACK-CORRECTION-2026-09-14.md) increases the evaluation host stack to 8192 bytes and adds owner-safe runtime headroom measurement. Affected software checks and two matching evaluation builds plus the ordinary control pass. The approved corrected-image trial reached protected Ready and displayed successful local confirmation. Startup samples retained 5916 bytes of BLE-task headroom. OT-225 retained a host reporting timeout; the fixed host reporting window was then tested without changing device offer expiry. [OT-226](testing/OT-226-POST-CONFIRMATION-STACK-2026-09-14.md) closes the missing measurement using the unchanged firmware: protected Ready, local confirmation and the complete combined recorder pass succeeded. The post-confirmation window retained 3980 bytes of minimum-free BLE-task stack. Full originals and same-session protected Ready were independently recovered; custody is closed. This accepts the bounded local evaluation correction, not remote membership or the two-node product lifecycle.
No V1 completion or public website status changed.

The additive security policy evaluation binds signed invitation and transcript
confirmation to real Noise, durable transmit counters and authenticated receive
replay admission. Actual-source host tests and two matching ESP32 builds pass;
retained or inconsistent state refuses fresh-only admission. This is a same-chip
evaluation, with synthetic trust and confirmation inputs, not product provisioning,
same-key resume, full rekey or phone-to-phone radio acceptance. See the
[consolidated evidence](testing/OT-187-SECURITY-POLICY-EVALUATION-2026-09-10.md).

The executable one-use capture/recovery composition now passes host tests for
source and payload binding, strict serial capture, durable interruption barriers
and exact original application/full-NVS restoration. See the
[capture and recovery evidence](testing/OT-188-SECURITY-CAPTURE-RECOVERY-2026-09-10.md).
That checkpoint established software preparation; the later bounded trial is recorded below.

An isolated private operator runtime now passes external startup-file checks,
parent/child import verification and poisoned-environment probes. Its ROM child
independently checks journal phase, storage spans and exact write payloads. See the
[runtime and physical scope](testing/OT-189-OPERATOR-RUNTIME-PREFLIGHT-2026-09-10.md).
The reviewed PowerShell verifier and host OS remain a trusted boundary.

The initial-backup controller now has host-tested capture, typed reset-only
release and one-use execution handoff. Private application/NVS custody binds the
expected originals and runtime; original reset invalidates executable NVS freshness.
Interrupted no-write trials can be reconciled without fabricated execution events.
See [backup custody evidence](testing/OT-190-BACKUP-CUSTODY-2026-09-10.md).

Live backups subsequently passed on both roles. In the bounded nonradio trial,
role A's candidate image verified and its reset completed, but no solicited
receipt was accepted.
Its original application/full NVS restored and readback-verified before reset;
role B was never flashed and passed guarded original readback/reset. No active
locks remain. The old snapshots are stale after reset; no security pass is claimed.
See [the trial outcome](testing/OT-191-BACKUP-LAUNCH-2026-09-10.md).

An additive receipt observer now has 32 passing differential tests and eight
passing execution/operator composition tests. It records bounded, allowlisted
stages and counts while preserving the frozen endpoint and restoration behavior.
A matching receipt observed is distinct from an accepted receipt; this software
work does not establish the historical failed capture's physical cause.
See [receipt diagnostics](testing/OT-192-RECEIPT-DIAGNOSTICS-2026-09-10.md).

The complete host matrix and successor isolated-runtime probes pass. A fresh
controlled diagnostic trial then received zero bytes across 94 reads/30 seconds
after the transport accepted 63 command bytes. This does not prove firmware
received or executed the command. A's original application/full NVS restored and
verified before reset; B was never flashed and passed guarded readback/reset.
Both active locks are absent and snapshots are stale after reset. The initial
prelaunch inventory refusal was reconciled before the sole actual trial.
See [diagnostic trial evidence](testing/OT-193-DIAGNOSTIC-TRIAL-2026-09-11.md).

Host lifecycle tests now compose the unchanged application, command control,
persistence and policy session with real scalar crypto and simulated device
services. Three cases also link the actual console. Controlled faults distinguish
silent input/startup exits, cleanup and receipt output failure, while strict
capture retains its rejection rules. The retained build/source audit found no
selected startup/configuration contradiction. See the
[lifecycle evidence](testing/OT-194-POLICY-LIFECYCLE-2026-09-11.md).

The additive durable-stage target, bounded NVS decoder and restoration observation
seam now pass composed tests and matching fresh builds. The stage record is
independent of the console and remains separate from a strict policy receipt.
See [stage diagnostic preparation](testing/OT-195-DURABLE-STAGE-DIAGNOSTICS-2026-09-11.md).
The executable successor now binds that image to fresh-backup custody, distinct
runtime-bound authority, one-use observation, ROM-child admission and verified
restoration. Composed tests, isolated runtime probes and the complete host matrix
pass. See [operator package evidence](testing/OT-196-STAGE-OPERATOR-2026-09-11.md).
The controlled trial now has a captured `input_result / input_refused` marker
from A, while its receipt capture received zero bytes. A restored exactly and B
remained unflashed and passed guarded original readback/reset. Both originals
returned; snapshots are stale and grants consumed. See
[stage trial evidence](testing/OT-197-STAGE-TRIAL-2026-09-11.md).
The additive input diagnostic target now distinguishes the actual refusal branch
in one U64 record and reports bounded host reset-return/open/RUN-intent timing.
Focused suites, two matching warning-free builds, composed operator/isolated
checks and ordinary/hostile runtime probes pass. The typed package is sealed;
actual-image composition used synthetic devices and retained originals were
checked offline. Required GitHub checks and publication passed. See
[input diagnostics](testing/OT-198-INPUT-DIAGNOSTICS-2026-09-11.md).
After explicit exact-image approval, the fresh input trial recorded
`input_result / invalid_length` on A. Its host accepted all 63 command bytes but
received no reply. Exact originals and protected regions restored/readback-verified;
A restarted. B was not flashed and passed guarded original readback/reset.
Host and firmware specify the same 63-byte frame. The wrong accumulated length's
cause remains unknown.
Host reset-return-to-RUN timing is not device boot timing. See
[trial and restoration evidence](testing/OT-199-INPUT-TRIAL-2026-09-11.md).
An additive input synchronization candidate now handles bounded discarded input
and retains exact first-frame and terminal reasons before stage advancement.
The image-selected decoder rejects inconsistent records and distinguishes an
interrupted durable prefix from a completed stage. The complete affected host
matrix and two identical firmware build tuples pass. This is a validated software
candidate. Its image-bound executable observation/restoration package now passes
the complete affected host matrix and actual isolated runtime probes. The approved
fresh physical trial on A accepted the command after discarding 26 startup bytes
and reached send-return without a recorded send error. Host capture instead refused
an oversized read (129 bytes across two reads); no strict receipt was accepted.
A restored exactly and restarted; unflashed B passed guarded release. The retained
USB-output mechanism now reproduces that rejection through the actual writer and
endpoint. A successor image adds a challenge-bound BEGIN marker; bounded host
preamble scanning preserves strict post-marker receipt and silence checks.
Two identical builds, the affected host matrix and the exact isolated package
pass. The approved OT-204 trial is now audited: pass.
Both nodes accepted matching BEGIN markers and strict pass receipts.
Original application/full NVS and protected regions were verified and originals
restarted; custody is closed and all grants consumed. Earlier raw byte contents
remain unknown. See the [physical confirmation](testing/OT-204-RECEIPT-TRIAL-2026-09-12.md),
[receipt-boundary correction](testing/OT-203-RECEIPT-BOUNDARY-2026-09-12.md),
[physical outcome](testing/OT-202-SYNC-TRIAL-2026-09-12.md),
[synchronization evidence](testing/OT-200-INPUT-SYNC-2026-09-12.md) and
[validated operator package](testing/OT-201-SYNC-OPERATOR-2026-09-12.md).
The historical byte contents remain unknown. A bounded policy receipt does not close all security gates. The [eight-gate plan](testing/OT-163-CRYPTO-INTEGRATION-GATES-2026-09-10.md) retains
physical entropy, interrupted persistence and remaining admission boundaries.

Historical raw-capture custody remains unestablished for the older libsodium and
Monocypher measurements. Preserve their receipts and seek exact original captures
or a separately authorized successor capture. Do not reconstruct raw transcripts
from aggregate results. Current mbedTLS canonical captures are retained.

Libsodium remains the eight-operation recommendation. Complete applicable Phase 3
admission and explicitly select the library/suite/handshake/KDF/wire before
connecting protected phone messages to authenticated direct LoRa. Current phone
Messages screens do not yet provide that product radio path. The original pair's
pending clock correction can follow its use as the retained control.

## Remaining acceptance and decisions

Post-release concepts remain separately scoped in the
[future-concepts register](FUTURE_CONCEPTS.md).
[Decision 0036](decisions/0036-post-v2-public-lane-and-assistance-direction.md)
records the post-V2 public-lane and assistance direction; it adds no current
release commitment or completion credit.

- Real protected message commands/events, peer/key provisioning, secure packet
  framing, persistent counters/replay protection, LoRa TX/RX and delivery ACKs.
- Complete reset-domain cleanup, cross-pair denial/isolation, physical rotation
  and large-font flows, automatic production launch and release packaging/signing.
- Target-specific radio/regulatory, antenna, power/battery and GNSS evidence;
  calibrated battery behavior, sustained clock accuracy and field measurements.
- Protected storage/rollback and selected cryptography remain distinct from the
  already working local BLE ownership/name/region storage.
- Multi-region radio operation is not accepted merely because all twelve region
  choices are represented in settings. Emergency features remain safety aids,
  not guaranteed rescue. Map licensing/offline-provider and later platform work
  retain their architectural gates.

The [canonical V1 record](V1_PROGRESS.json) owns completion. This batch changes
accepted evidence and remaining gates, with no score credit or public website
capability change. Bulk website/IIS synchronization was completed earlier; further
editorial work follows the accepted cadence. Numeric-keypad work and cold-power
disassembly remain owner-deferred.
