# OT-0238c current-attempt settings check

## Problem and measured evidence

The [completed recovery inspection](../../tests/hardware/OT-0238c-RECOVERY-INSPECTION-2026-10-11.md)
proved both original firmware regions still matched, but whole NVS bytes differed
from the historical backup. The retained capture mode linked all six old spans
and then required the live settings storage to equal that old snapshot.

Offline CRC/layout-validated comparison of the recorded images found six
namespaces and 27 live records on each node, with no added/removed live records.
Only the SDK's public `phy/cal_data` calibration payload differs: 104 bytes on A
and 120 on B. Every other live payload is identical. Raw flash differs more
because storage layout/history also changed. The local SDK calibration producer
is pinned as corroborating source; its relationship to the restored binaries and
the exact historical trigger/timing remain unestablished.

Reproduction and sanitized results are private under
`C:\lu\OpenTrail\.private\ot177-publication\.private\ot0238c-settings-check-20261011`:
`compare_settings_offline.py`, `semantic-comparison.json` and
`semantic-comparison-pins.json`. Normalization is diagnostic only; it never
authorizes candidate admission or replaces exact raw-byte freshness.

## Correction boundary

The explicit `OT-RETAINED-FIRMWARE-CURRENT-NVS-REQUEST-1` mode reuses the five
immutable original backups and freshly captures current NVS for that attempt.
The historical retained mode remains unchanged and still rejects divergence.

The new mode requires fresh future capture authority, exact identity/profile and
retained provenance, continuous ROM custody, two equal guarded current-NVS reads,
reset-marker/structural safety and durable write/readback before the snapshot is
usable. Final freshness still compares all six live spans with the attempt's
exact saved bytes. Adoption, original recovery and restoration consume those
same pins. Neither arbitrary mismatches nor old cleanup-only inspection bytes
become accepted originals. Incomplete, unsafe, unstable or corrupted snapshots
leave accurately recorded unresolved custody rather than booting against an
invented or historical settings snapshot.

The full capture/verify/adopt/restore path is covered with the maintained classes
and simulated SDK/transport. The two actual recorded NVS pairs also reproduce
old-mode rejection and new-mode capture plus exact restoration offline, using
synthetic device identities and immutable regions. These are host simulations,
not another physical acceptance case.

## Caller and package gate

Caller integration and the exact inert successor package are independently
reviewed. The familiar launcher is repointed to the approved v2 command bytes
and has not been executed.
The first actual metadata review rejected an overbroad recursive history walk:
it hit oversized historical manifests, mutable external files and archived
markers. That package and its source-only review remain preserved. The revised
gate follows the maintained settled-inspection receipt contract, hashing deeper
historical records as leaves rather than replaying their ancestry. Current
runtime admission remains a separate bounded check.
The historical held journal/marker remains preserved. A separate new-mode gate
must validate the settled diagnostic and independent owner/evidence references,
exact reviewed source/runtime/request bindings and a new session root. Its
permission reaches fresh capture only; it supplies no future physical approval,
candidate grant or retroactive closure of old custody. Legacy paths, removed
metadata and schema downgrades must remain refusing.

Runtime, template, binding and familiar-launcher checks exercised the actual
capsule inventory and new request mode without SDK/device/native actions or live
grants. Deadlines, invitation windows and write-budget reserves are unchanged.
No firmware target or wire protocol changes, firmware build, device operation,
Git publication, V1 credit or public website capability change are in this
host-only increment. Full OT-0238c remains In Progress.

## Current validation

- New current-settings integration suite: 10 cases passed.
- Actual recorded A/B NVS regression: old refusal/new exact restore passed.
- Legacy ROM adapter: 40 cases passed.
- Legacy original capture: 32 cases passed.
- Legacy budget/adoption: 44 cases passed.
- Complete capture runner: 24 cases passed, including three new-mode cases.
- Predevice resume, owner workflow and interactive launch: 45/33/40 passed.
- Revised settings-successor admission: 13 cases passed, including the actual
  finite inspection closure and selected-record corruption refusals.
- Total affected matrix: 281 cases passed. The revised four external-gate
  suites reran 131 cases; 150 unchanged core cases reuse their exact source,
  test and log pins. Records and failed logs from the prior version remain.
- Actual isolated v19 runtime: 3,574 files, 36 policy inventory entries and
  14 core source copies verified; only three core capsule files changed.
- Five existing firmware references and 377 compiled source pins verified;
  no firmware rebuild. The initial 120-second host verifier timeout was retained;
  a 600-second host-only continuation verified the same capsule without copying
  again or changing any device deadline.
- Earlier synthetic fixture failures remain recorded, separately from final
  passing logs. Historical-source fixtures now model old fingerprints explicitly;
  a separate test rejects current-source/old-metadata combinations. The second
  simulated trial now correctly resets its synthetic application volatile state.
- The first independent source review approved the prior 19-source closure and
  277 results; actual metadata review then withheld approval as recorded above.
  Two separate adverse probes confirmed refusal after
  post-verification NVS drift and rejection of a forced old-mode downgrade.
- The revised finite diagnostic gate independently passes the accepted evidence:
  65 unique required leaves excluding linkage, 1,838,754 bytes. It preserves the
  128 KiB control-record cap and separately bounds current runtime manifests to
  1 MiB. Seven baseline provenance refs remain checked as disposition leaves.
- Revised source review binds the exact 19-source closure and final 281 cases.
  Independent complete admission of the actual v2 stage passes; eight altered
  or stale metadata cases refuse. All external closure bytes are rehashed,
  live destinations remain empty and SDK modules are absent.
- These are local prepared-workspace results. The actual-inspection test requires
  retained private evidence and is excluded from the current explicit CI suite
  list. No clean-checkout, CI or publication acceptance is claimed; a later
  publication must explicitly select portable tests and retain this separate
  actual-evidence gate.
- Static, manifest, binding, owner-plan and exact literal/bootstrap reviews
  approved the v2 package. The default host bootstrap used only a NONMAIN
  sentinel, with SDK/native/device actions and workflow execution absent.
- The familiar CMD now exactly matches approved SHA-256
  `76569b91156a72c2ea670320b9b3118f3caa422e20b28164cebd92456caaef67`.
  Original launcher bytes remain preserved; all other baseline files and the
  39 historical held-session files remain unchanged. New readiness/session
  directories remain empty; no grant or physical approval was created.

Final private records: `host-validation-v2.json`,
`independent-source-review-v2.json`, `exact-runtime-package-review-v2.json`,
`bootstrap-gate-v2.json` and `launcher-repoint-v2.json` under the evidence root
above. Next is one separately authorized device attempt using this reviewed
package. Native launch, timing fit, restart and rekey still require actual
acceptance; host preparation does not guarantee a physical pass.
