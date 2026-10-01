# OT-0238c isolated target preparation - 2026-09-30

Approved revision 1 is In Progress. OT-0238b revision 2 is now owner-accepted.
This result prepares an additive USB evaluation target and its inert client;
it does not complete the physical two-node task or select production security.

## Implemented boundary

The new [evaluation target](../../firmware/targets/heltec_v4_enrollment_candidate_eval/README.md)
uses the actual candidate session, persistent identity/signers, retained/recovery
owners, leased OLED, BOOT arbiter, fresh time, reset gate/executor and SDK-shaped
NVS/store adapters. The local USB controller selects mode/role/group; received
peer values cannot select that authority. Public records have bounded canonical
encoding; device owners sign, not the host. There is no product BLE admission,
LoRa transmission or production wire/security selection.

Original preparation and invitation/recovery deadlines remain unchanged. Actual
input/time refresh brackets review; queued reset events preempt enrollment and
suppress staged output. USB processes at most 256 bytes per application tick and
yields one actual FreeRTOS tick, avoiding zero-delay conversion. Refusal does not
reopen authority or silently retry. The inert client accepts an already-owned
handle, checks identity/deadline before and after I/O, rejects stale queued input
even at first HELLO and exposes explicit read-only status after known refusal.
It neither opens/flashes devices nor supplies authority, signatures or gestures.

Sparse candidate storage owns 250 disjoint addressed tuples in ot238_eval,
inside the separately named ot238_nvs partition.
Capacity is two spent generations; failed/cancelled allocations can consume one,
with no recycling or broad erase. Absent/erased tuples stay absent. Unknown keys,
known-present disappearance, uncertain readback and over-budget state refuse.
The pinned SDK source gives a conservative ceiling of 51 blobs / 204 entries.
Initial V1 source passed its 65-suite matrix, but offline review then established
that a verified historical original NVS snapshot uses 110 entries, exceeding
V1's 27-entry original-state allowance. Its conservative requirement is 335
entries against 252 usable. Builds were held before compiling that unsuitable
layout. The initial Windows-IDF-path configuration error is separately preserved
and corrected; neither event is a physical device failure.

V2 probes the exact running factory partition and named data/NVS descriptor at
0x500000, size 0x4000, before initialization. Its four pages supply 378 usable
entries, separately charging the unchanged candidate ceiling and reserves.
Original marker/user/bond state stays in the default 12 KiB partition with its
own 252-entry budget, including marker/replacement headroom. A fresh default
budget check precedes physical intent commit. No original-state budget is relaxed
in place; foreign namespace/inventory and wrong mapping refuse.

The new 16 KiB area is original OTA0's prefix, NOT a proven gap or blank area.
The evaluation table is factory-only; original factory/default NVS/otadata/
ot_state spans stay fixed. SDK initialization/recovery can write or erase corrupt
pages even without a caller erase fallback. Before FIRST candidate entry the
entire affected span must therefore be captured and explicitly provisioned under
separate exact authorization, even if it appears blank. Retained restarts reuse
that already prepared nonblank NVS. A mocked init failure does not prove unknown
OTA bytes stay untouched. Actual capacity, fragmentation and latency remain
physical admission gates; simulated counts do not guarantee fit.

Startup checks the real reset marker before candidate writes. READONLY persisted
bond inventory and actual RAM peer occupancy must agree before enabling reset
inspection; this establishes occupancy, not complete bond restoration or trust.
Entropy borrows the initialized controller and retires before deinitialization.
The physical reset gesture commits/readbacks intent, then stops at cleanup_required.
It never runs destructive cleanup, consumes a receipt or boots the original app.
Positive reset preparation currently requires initially occupied original state;
an initially idle_unowned executor safely refuses even if candidate state is later
created. Original default NVS and the repurposed span must be restored before
booting the original app, which
could otherwise automatically act on the prepared reset intent. ot_state is never
written/erased by this target and lies outside the existing recovery capture.

## Validation

- 108 focused groups pass: 33 sparse storage, 40 partition/budget, 17 startup,
  one codec and 17 complete-caller
  groups. First enrollment, restart/rekey and both eligible interrupted-commit
  recovery directions each exchange eight authenticated statuses. Cancellation,
  revoke, old-epoch denial, expiry, queued reset, reentry and uncertain storage
  remain explicit negative controls. Revoke retires membership and traffic;
  public evidence/binding remain retained under the existing owner contract.
- Virtual A/B flow milliseconds: first 4787/4759; rekey 3634/3734; recovery with
  B committed 27725/5991; recovery with A committed 6019/27698. These use modeled
  clock/GPIO 1000 us, OLED draw 3000 us, NVS call 10 us and 256-byte chunks with
  a 10 ms task tick. Peak retained/recovery storage is 50 blobs / 200 entries,
  plus one named namespace entry (201). All flows retain default 111 entries:
  the concrete original 110 plus one marker namespace, with original bytes
  independently checked. Live default occupancy of 232 refuses before intent
  writes. These are independent physical-partition models, not merged counters.
  SDK I/O, occupancy, button/display effects and elapsed time are simulated.
  app_main is source-reviewed and build-bound, not host-executed or observed.
- The inert USB client passes 23 groups; only status IDs 1..8 are accepted.
- The complete final 65-suite current-source matrix passes once on frozen inputs.
  Independent review has zero unresolved source blockers and verifies unchanged
  prior owners. The existing 18-target inventory is reused; only this new target
  changes, bringing the inventory to 19. Earlier failed prototype receipts remain
  preserved and classified, including fixture errors rather than invented device faults.
- Two builds from initially absent directories pass under ESP-IDF 6.0.2,
  Xtensa 15.2.0_20251204, Python 3.14.6, CMake 4.0.3 and Ninja 1.12.1 with -j4.
  All eight raw artifact pairs and actual repository/installed dependency maps
  match. Existing accepted build artifacts remain unchanged. The target directory
  is heltec_v4_enrollment_candidate_eval; embedded project ot238c_enrollment_eval
  avoids the SDK's 31-character truncation. Version is ot0238c-enrollment-candidate-v2.
  Main-task stack is configured at 24 KiB; physical headroom is unmeasured.
- Application is 637,520 bytes, within the existing 733,184-byte application
  recovery span at 0x10000. SHA-256: `93cd4e6d9011d5877cb02e9f0384958239c52a08a28f45700279bc2962cd8e0d`.
  Image fit is a build fact, not authorization or physical operator admission.
  Exact eight artifacts, configuration, closure and installed toolchain pins are
  in the private build-storage-result.json. The retained wide installed-SDK
  snapshot is reused; every actually consumed compiler dependency must match its
  original pin. This does not claim a repeated whole-SDK scan at every build step.
  Owning-document/diff checks are recorded in
  the private closeout.json after reconciliation.

The final diff check caught a CRLF conversion in the existing Python matrix
runner. Its original LF form was restored by removing exactly 377 CR bytes;
the runner's text and complete compiled code are unchanged by this correction.
Its exact before/after/inverse pins are in runner-line-ending-closeout.json.
Prior matrix/build receipts remain immutable; product inputs and compiled
artifacts are unchanged. The final closeout verifies this one explicit runner
normalization rather than pretending its earlier raw source hash still matches.

## Remaining task and next action

The complete source-bound physical controller is still required. The earlier
OTENROLL operator creates host signatures and fixed grants; it cannot simply be
reused as candidate authority. Reuse its unchanged custody/restoration patterns,
then bind the new parser, exact candidate artifacts, fresh readiness after warm
restart, separate partition preparation, original-state occupancy, capacity-two
case isolation and sequential restoration. Run controller-only lifecycle tests
before proposing a device trial. It must capture/readback/restore all six spans:
original bootloader, application, default NVS, partition table, otadata and the
additional original OTA0 prefix. Rollback-enabled bootloaders can change otadata
even before factory fallback; do not assume its contents remain untouched.

Later physical acceptance requires separately authorized exact devices, current
ports/layouts, artifact and restoration hashes, local fingerprint/transcript
gestures, retained restart/rekey/recovery and original firmware/NVS readback.
Cases stay closed and batteries connected; no phones or RF are needed for USB
evaluation. No devices were accessed in this preparation. Actual USB latency,
display/input behavior, flash occupancy and runtime headroom remain unmeasured.
OT-0238c stays In Progress. Existing production selection/binding and security/
reset tasks retain their own gates. No V1 credit or public website status change.
Changes remain local/uncommitted; publication is pending separate authorization.

Task: https://limitedunderground.com/lab/tasks/612b4a3f-c1c0-4e2d-aeb1-0fb37658a249.
Private evidence: .private/ot0238c-target-preparation-20260930/ contains approval,
owner acceptance, baseline, focused-closeout, independent review, final matrix,
build-input-freeze/build-result and final closeout receipts.
