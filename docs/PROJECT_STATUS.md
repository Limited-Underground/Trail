# OpenTrail Project Status

As of 2026-09-10. This page summarizes accepted behavior and the next work; dated
history belongs in [PROGRESS_LOG.md](PROGRESS_LOG.md). The complete prior status,
including assumptions and older decision checkpoints, is preserved in the
[2026-09-08 archive](history/PROJECT_STATUS_BEFORE_CLEANUP_2026-09-08.md).
See [archive navigation](history/README.md) for moved headings, [Architecture](ARCHITECTURE.md)
for design and the [backlog](../tasks/BACKLOG.md) for every task identifier.

## Current accepted capabilities

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

Next diagnose absent/rejected receipt capture before any newly admitted physical
attempt. The complete host validation matrix passed. The
[eight-gate plan](testing/OT-163-CRYPTO-INTEGRATION-GATES-2026-09-10.md) retains
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
