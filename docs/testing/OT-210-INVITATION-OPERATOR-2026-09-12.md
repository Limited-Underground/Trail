# OT-210 invitation candidate operator

Recorded 2026-09-12. Implementation and local host/runtime validation complete;
physical execution remains untested and requires fresh exact-image authority.

The additive operator package binds `heltec_v4_invitation_eval` / `ot208-invitation-v1`
to the accepted 445248-byte application, SHA-256
`4526209643bbfb51ccf95d04a992eed41c877dd72cc0b03f415783a63d9036d2`. Its exact 48-source build report is unchanged. The package,
runtime, requests, execution/recovery grants and observation claims use distinct
OT-210 identities; the frozen OT-203 operator continues to reject this image.

The runtime contains 23 policy/launcher files. Sixteen fixed source pins include
the established receipt boundary and new readback/observation sources. Execute
authority explicitly names invitation boot and role-record writes. Recovery
actions, offsets, span lengths, receipt grammar and serial/restoration ordering
remain unchanged. This task did not rebuild or modify firmware.

## Admission and restoration

Saved original NVS must structurally decode and contain no recognizable namespace
identity from `ot198diag`, `ot208_boot`, `ot208_ia`, `ot208_ib`, `ot187_ta`,
`ot187_tb`, `ot187_ra` or `ot187_rb`. Empty namespaces and retained deleted-key
history also refuse admission. This strengthens the prior host admission, which
checked the diagnostic namespace; TX/RX fresh-only enforcement was in firmware.
Unrelated payloads containing those byte strings do not count as namespace identity.
Absence is not proof of never-used storage, physical freshness or rollback resistance.

The exact image binding follows capture through typed projection and restoration.
A fixed stage record remains separate from strict BEGIN/pass-receipt acceptance.
Missing or unsupported projection does not suppress original restoration. Full
12288-byte NVS restoration covers the added ledgers. The application restore span
remains 589824 bytes at `0x10000`; protected boot/partition/OTA readbacks remain.
A must pass and restore before B starts. Restoration-only recovery requires the
saved originals and provenance, permits a missing candidate file, and never recaptures.

## Validation

[Structured evidence](../../tests/benchmarks/crypto/OT-210-INVITATION-OPERATOR-2026-09-12.json)
records **300 passing tests across 17 suites**, with no skips:
package/source/image rejection, all eight namespace boundaries, actual composed
backup handoff/ROM dispatch/receipt/capture/restore, corruption and interruption
barriers, candidate-independent recovery, and isolated Windows dispatch.
Device/serial behavior is simulated in these tests; the image used by composed
acceptance is the exact candidate BIN. These are not physical board results.

A real isolated Python/PowerShell capsule with 3561 files passed
ordinary and hostile-environment parent/child probes. Post-probe inventory matches;
no hostile startup marker executed. Exact candidate admission with synthetic
originals passed; that retained package is clearly synthetic and confers no custody.
The first matrix stopped because one copied test expected the predecessor error
class. The successor correctly refused before I/O; the assertion now requires its
own typed error. Production code did not change to satisfy that test.

Independent normalized source review found only the intended family bindings,
original-NVS checks and explicit write-action changes. Frozen predecessor sources,
old runtime and firmware artifacts are preserved. The dedicated runner is
`tools/Test-SecurityInvitationOperator.py --candidate <exact candidate path>` and
is registered in the shared host runner. Source hashes and precise private logs
are retained with the structured report and private continuation handoff.

## Next acceptance

The operator-binding prerequisite in the
[OT-208 physical procedure](OT-208-INVITATION-HARDWARE-PROCEDURE-2026-09-12.md)
is now satisfied for this exact package. Before execution, obtain fresh exact-image
authority, re-enumerate and independently verify both devices/routes, capture
verified original application/full NVS/protected regions, then admit one sequential
A-first trial. Previous grants remain consumed; historical snapshots are stale.
No automatic retry, clearing retained state, old-manifest image replacement or
simultaneous board writes are permitted by this package.

No hardware was enumerated, opened, flashed, reset or erased here. No grant or live
custody was created. Physical entropy, interrupted persistence, stack high-water,
production trust/confirmation/rekey and remaining Phase 3 acceptance stay open.
V1 completion and accepted public website status did not change. Work is local and
uncommitted; no publication or remote verification occurred.
