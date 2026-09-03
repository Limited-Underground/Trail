# Heltec V4 Snapshot Authority Mapping

Status: implemented and non-hardware validated, 2026-09-03. Physical
snapshot-to-Ready acceptance remains pending and no hardware was flashed.

## Observed failure and exact boundary

Before the implementation, a clean selected-Heltec and Note20 run reached encrypted passkey bonding, protected
ProtocolInfo, MTU 151, Stream indication subscription, an accepted
authorization claim, confirmation of both claim indications, and authorization
phase promoted. The immediately following normal Command passed the dynamic
GATT authorization gate but returned authorization error 34,
normal_command_rejected.

This was the mandatory Android SNAPSHOT_REQUEST, not another pairing or claim
operation. At diagnosis time, the live Heltec runtime constructed
DeniedSnapshotAuthority, whose only result is CompanionAuthorityError::not_ready,
and passed that object to the sole live CompanionRequestCoordinator. The
coordinator therefore rejected the request as snapshot_authority_failed; the
normal GATT lifecycle mapped that to coordinator_rejected; and the authorization
lifecycle exposed it as normal_command_rejected.

The live binding now uses HeltecV4SnapshotAuthority. The denied class remains
available but is no longer injected into the production Heltec coordinator.

## Complete call-site map

1. companion_nimble_runtime.cpp defines both the retained
   DeniedSnapshotAuthority and the fixed HeltecV4SnapshotAuthority.
2. The global g_snapshot_authority is now HeltecV4SnapshotAuthority, and
   start_companion_nimble_runtime() passes it to the sole live Heltec
   CompanionRequestCoordinator.
3. The coordinator is injected into
   CompanionGattAuthorizationCallbackAdapter.
4. After the accepted terminal claim indication is confirmed,
   CompanionGattAuthorizationLifecycle::promote_normal_if_ready() opens the
   normal GATT session.
5. Android sends its first normal Command as SNAPSHOT_REQUEST.
6. CompanionGattSessionLifecycle::service_command() delegates to
   CompanionRequestCoordinator::service().
7. The coordinator calls snapshot_authority_.read_snapshot().
8. not_ready produces snapshot_authority_failed, then coordinator_rejected,
   then authorization error 34.

The target boot self-check uses a separate FixedSnapshotAuthority; common host
tests use injected fake authorities. Those are test seams, not the live Heltec
binding. No other production target was found receiving this file-local denied
instance.

Repository-wide `not_ready` accounting does not identify another production
snapshot stub:

- `companion_request_coordinator.hpp` uses `not_ready` only as the fail-closed
  default of result objects;
- `companion_request_coordinator_tests.cpp` deliberately injects `not_ready`
  to preserve the negative `snapshot_authority_failed` contract;
- `companion_factory_reset_authority_tests.cpp` has an always-`not_ready`
  action-authority fake, not a snapshot authority; and
- `companion_factory_reset_authority.cpp` may return `not_ready` for the
  separate reset action. Neither factory-reset occurrence participates in the
  mandatory post-claim snapshot.

## Minimal truthful Heltec contract

The Heltec-only authority must return one complete, internally coherent
CompanionSnapshotAuthorityResult:

- error: CompanionAuthorityError::none;
- revision: nonzero, initially 1;
- radio: unavailable until a real device-owned radio state is wired;
- gnss: unknown until a coherent application-task snapshot is published;
- power: unknown until a coherent application-task snapshot is published;
- position_sharing: stopped;
- queued_action_count: 0;
- pending_critical_alert_id: 0.

These values report only what the current target can truthfully claim. They do
not convert unimplemented radio, GNSS, power, queue, or alert work into false
ready, current, or normal states. Revision 1 may remain stable while the
snapshot is immutable; a future mutable publisher must increment revision for
every observable state change and must publish one atomic copy from the
application task. The NimBLE callback must never read task-owned GNSS or battery
objects directly.

not_ready remains valid for intentionally denied test or non-Heltec
configurations. It is not valid as the permanent live Heltec authority after
authorization because Android requires the first snapshot to establish Ready.

## Required ordering

The existing claim order remains unchanged:

1. accept the claim-start Command;
2. indicate Pending and receive its transport confirmation;
3. resolve the device-owned authority decision;
4. indicate Accepted and receive its transport confirmation;
5. set application-authorized state and promote/open the normal session;
6. accept the first SNAPSHOT_REQUEST;
7. indicate the matching Snapshot and receive its confirmation;
8. keep the promoted session open.

The snapshot authority does not participate before step 6. No Android delay,
weaker characteristic permission, altered ATT error, or pairing change belongs
in this fix.

## Test-first coverage

- companion_gatt_authorization_adapter_tests.cpp explicitly proves that an
  accepted and confirmed claim promotes the session, the first snapshot request
  returns normal_indication_pending with no authorization error, the payload is
  a valid nonzero-revision Snapshot, and confirming it keeps the session
  promoted and active.
- BleAuthorizationRuntimeTest.kt already proves the Android half of the same
  contract: Accepted leaves the GATT connection open, causes a same-session
  SNAPSHOT_REQUEST, and the resulting Snapshot moves the runtime to Ready. Its
  later QUICK_STATUS assertion proves only that Android accepts and emits the
  request; the live Heltec action authority currently returns
  unsupported_action for non-factory-reset actions.
- companion_request_coordinator_tests.cpp retains the negative contract: an
  intentionally not_ready authority returns snapshot_authority_failed without
  partial output.
- heltec_v4_bench_target_tests.py requires the denied authority to remain
  available, but requires the live Heltec g_snapshot_authority to use
  HeltecV4SnapshotAuthority with the conservative values above.

Before the runtime swap, the common behavior regression passed and the new
production-composition test failed specifically because g_snapshot_authority
was still DeniedSnapshotAuthority. That expected RED result was the
implementation gate.

The 2026-09-03 mapping run produced exactly that split:

- `PASS: 12 companion GATT authorization adapter groups` under C++17 with
  `-Wall -Wextra -Wpedantic -Werror -O2`;
- the focused Android
  `exactAuthorizeFlowPromotesThenRequestsSnapshotOnSameSession` test passed;
  and
- before the implementation, the Heltec target test stopped at
  `live Heltec runtime must not bind the always-not_ready snapshot authority`;
  after the class and binding swap, all 16 target-admission groups passed.

The complete host matrix subsequently exited 0. ESP-IDF 6.0.2 built the real
esp32s3 target in 26,332 ms with no GCC or linker warnings. The application is
551,472 bytes with SHA-256
`ECCEA30BCB0FCA5A85C870A2EAB5C79FBC1D766BE08455C186EE6A81CE980669`.
Three upstream ESP-IDF Kconfig boolean-default notes remain; they are not
compiler or linker warnings.

## Checklist boundaries

The following belong to deterministic host or Android regression coverage for
this change: claim-indication ordering; promotion before snapshot admission;
same-session Snapshot-to-Ready behavior; pre-promotion rejection without state
corruption; retained ProtocolInfo, Command, Stream CCCD, indication completion,
timeout, and factory-reset gates; exact internal coordinator errors; response
busy/reservation behavior; and retention of intentional negative authorities.

The following require later physical acceptance and cannot be claimed from host
tests: clean system bond through Snapshot and Ready, saved-bond reconnect after a
Heltec power cycle, one-sided bond clearing and recovery, second-phone policy,
real NimBLE resource/MTU behavior, rapid live claim-to-snapshot timing, Android
remaining connected, and absence of live ATT status 8 or firmware error 34 on
the happy path.

Three terms must stay precise:

- V1 has no ordinary `unclaim` command. Removal and re-enrollment use the
  destructive physical factory-reset flow plus manual Android stale-bond
  cleanup.
- The first post-claim normal Command is the mandatory SNAPSHOT_REQUEST. The
  first later action, such as QUICK_STATUS, is a separate acceptance gate. The
  current live Heltec action authority rejects non-factory-reset actions as
  unsupported_action; changing that is outside this snapshot fix.
- `not_ready` may remain a transient, fail-closed internal result. This change
  removes permanent `not_ready` only from the live Heltec composition; it does
  not invent a new public Android error taxonomy.

## Later validation sequence

Completed after the Heltec-only swap:

1. run the focused coordinator, GATT authorization, callback-adapter, and target
   admission tests;
2. run the focused Android authorization-to-Ready test and the complete host
   matrix;
3. build the ESP32-S3 target.

Remaining physical acceptance:

4. perform one clean physical bond, claim, mandatory snapshot, and
   Ready run while capturing privacy-safe firmware and Android evidence side by
   side; observe any later action result separately without treating Android
   submission as device-side support;
5. separately power-cycle and prove saved-bond reconnect without a new PIN;
6. test the documented one-sided stale-bond recovery and single-owner/second-
   phone policy; and
7. stress the actual rapid claim-to-snapshot sequence and confirm no happy-path
   ATT status 8, firmware error 34, indication failure, or session close.

Physical flashing and another connection experiment were intentionally excluded
from this implementation and non-hardware validation step.
