# OpenTrail Project Status

As of 2026-09-09. This page summarizes accepted behavior and the next work; dated
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
- The host-only benchmark successor composes buffered receipts, startup checks,
  fresh-handle restart and separate per-role recovery images. Byte-level tests
  also correct receipt type identity and the firmware's SHA-256 field parsing.
  The concrete role-checking backend and exact source/image snapshot are now
  host-tested. Live two-board identity, layout and recovery-span preflight now
  passes. A separate solicited-readiness target and runtime address Windows
  serial-open buffer loss; build/validation details are in the
  [readiness evidence](testing/OT-163-SOLICITED-READINESS-2026-09-08.md).
  The solicited path now composes the concrete backend with separate recovery
  images and exact build/source admission; see
  [execution integration](testing/OT-163-SOLICITED-EXECUTION-2026-09-08.md).
  A fresh physical attempt passed readiness/restart and the first baseline
  validator, then stopped in the forced-retry scenario. Both complete application
  spans and bootloader/partition/OTA regions were verified after restoration. No complete benchmark
  result is admitted. This does not prove the old
  OT-163 hardware root cause or product messaging.

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

A bounded endpoint observer now distinguishes retry receipt boundaries in host
tests while preserving the existing runner result and exceptions. See
[retry diagnostics](testing/OT-163-RETRY-DIAGNOSTICS-2026-09-08.md). The observer
is now composed at the concrete backend endpoint-open seam using exact role-object
identity. Its 26-source/three-image binding retains the existing coordinator and
separate restoration images, and each admitted attempt ordinal owns distinct
journal, execution and recovery names. Deterministic tests prove successful and
failed cleanup, no-ROM-while-radio-leased behavior, restore-only recovery and
non-reuse of a consumed namespace. See the
[diagnostic execution binding](testing/OT-163-DIAGNOSTIC-EXECUTION-BINDING-2026-09-08.md).

Two bounded diagnostic attempts are now recorded. Attempt 1 stopped at Node B's
post-restart readiness receipt. The 27-source startup-tolerance successor then
allowed only recognized bounded startup noise and healthy stale READY receipts;
attempt 2 issued `m3` and received `TX_START`, then timed out awaiting `TX_DONE`.
Both one-use grants are consumed. After each attempt, both distinct role images
were restored, read back and reset; independent application-span and
bootloader/partition/OTA postchecks passed
for both attempts. See the
[instrumented run](testing/OT-163-DIAGNOSTIC-RUN-2026-09-08.md). The timeout
location is known, but its root cause is not.

The bounded radio candidate now passes actual-driver fault tests, generated
firmware concurrency/containment tests and real-byte host composition. It reports
transmit and receive-rearm return checkpoints, retains bounded diagnostics through
cleanup and preserves the host deadline. Two clean firmware builds are identical.
See [containment evidence](testing/OT-163-RADIO-CONTAINMENT-2026-09-09.md).
The 40-source execution binding and complete host matrix now pass. Physical
attempt 3 again stopped awaiting m3 TX_DONE, but its new transmit and receive-rearm
checkpoints both returned success. Both original applications and protected flash
regions passed independent restoration/readback/reset checks. See the
[contained execution outcome](testing/OT-163-CONTAINED-EXECUTION-2026-09-09.md).
Host fault injection now distinguishes missing bytes from unterminated receipts
without changing the parser or deadline. The separate observer and guarded
42-source session retain safe byte/read counts across failure and cleanup; all
40 historical inputs remain unchanged. Twelve endpoint and eight composed groups
pass; the required full host matrix gates publication. See
[receipt observation evidence](testing/OT-163-RECEIPT-OBSERVATION-2026-09-09.md).
The exact 42-source/three-image observation binding and isolated attempt-4
caller now pass 30 private offline groups. Both roles passed fresh original-image,
erased-tail and protected-region reads with guarded resets; prior region
descriptors match exactly. See [preflight evidence](testing/OT-163-OBSERVATION-PREFLIGHT-2026-09-09.md).
Physical attempt 4 then completed baseline m1/m2/m3 TX_DONE and accepted peer
RX, with END complete on both nodes. After both accepted forced-retry preparation,
Node B's initial RX_START timed out before retry transmission. Its observer
recorded 20 empty reads, zero returned bytes and no pending data. Both original
applications and protected regions passed independent restoration/readback/reset
checks. The grant is consumed. See [the outcome](testing/OT-163-OBSERVATION-RUN-2026-09-09.md).
Next is a bounded host/source probe of console delivery at the immediate PREPARED-to-RX_START
log boundary; no further hardware attempt or grant is part of that probe.
Accepted full host/CI evidence is reused because all 42 sources are unchanged.
The physical cause remains unknown; no complete radio-cost result, cryptographic
selection, phone-to-phone delivery, V1 credit or website change is admitted.

After the remaining crypto measurement/admission and explicit suite/wire selection,
implement one authenticated two-node message exchange, then its protected BLE
phone send/receive path. Current firmware reports radio unavailable, and the real
Messages screen prepares local templates only. Existing BLE success is not
Phone A to Phone B delivery. [Decision 0003](decisions/0003-crypto-benchmark-gate.md)
and [secure-LoRa contract](security/SECURE_LORA_KEY_TRANSPORT_V0.md) retain the
selection and security gates.

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

The [canonical V1 record](V1_PROGRESS.json) remains unchanged at exact 45.50%
(displayed 46%). No score was earned by this documentation cleanup. Website bulk
updates, numeric-keypad work and cold-power disassembly remain owner-deferred.
