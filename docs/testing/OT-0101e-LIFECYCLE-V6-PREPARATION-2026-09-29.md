# OT-0101e corrected test package preparation

## Prepared boundary

This computer-only preparation follows the [closed v5 trial and deadline fix](OT-0101e-LIFECYCLE-DEADLINE-2026-09-29.md).
The candidate firmware, app, original recovery path and revision2 acceptance
are unchanged. The next package binds checkpoint/state schema2, which retains
the controller's absolute monotonic deadline. No device was enumerated, opened,
reset or written during preparation; no request, grant or reservation was issued.

Selected pair remains Trail Bench OT-DEV-001 and Note20 SM-N986U. The previous
physical trial is closed, restored, normal-screen confirmed and released.
Current original connection/readback and exact owner readiness/one-trial
permission are required before another execution. Cases remain closed and
batteries connected; Bench2/S24 are excluded.

## Exact package

Private package: `.private/gnss-lifecycle-plan-v6` in the selected GNSS worktree.
Binding SHA-256:
`84321076c66b3ddd87ff16dcb5022cf2e0dce8902ba4528eb096dcbf43d5a88d`.
Issuer SHA-256:
`5649590451558b4b2b8226d8c39b74020882b6544d9af7cabf2499723570f031`.

All34 pins verify. Compared with v5, six pins changed: the observer, handoff
guard/CLI, their two tests, and the operator procedure. One private operator
sheet was added. It lists each stage and provides a same-host monotonic
remaining-time command, immediate prepare-to-stdin forwarding and the30-second
restart_guard action without an owner prompt. Observation fields are created
from actual readings, never pre-attested all-true templates.

The issuer changes only its two package-directory references and exact binding
digest. Historical original-preflight provenance remains tied to its original
v4 digest. Those originals establish integrity only; execution recaptures current
originals/settings before any write. Current one-trial, original-control and
revision2 attestations are required. Reservation precedes USB discovery and
cannot be retried after uncertainty. Historical v4/v5 packages remain untouched.

## Validation

- Seven issuer checks passed using mocked discovery/writes and synthetic
  in-memory authority; no real grant is produced by the suite.
- Real issuer `check` validated current binding/runtime and all retained
  original descriptors without serial enumeration, opening or issuance.
- Maintained runner `check` passed its isolated runtime probe with no hardware
  access or serial enumeration.
- Exact hashes match the prior70 focused host checks and actual58.074-second
  PTY handoff evidence. These unchanged results were reused, not rerun or
  relabeled as device acceptance.

Commands use `.private/ot0101e-runtime/python.exe -I -S -B` followed by
`.private/gnss-lifecycle-plan-v6/test_issue.py`,
`.private/gnss-lifecycle-plan-v6/issue.py check`, and
`tools/run_gnss_lifecycle_trial.py check --binding`
`.private/gnss-lifecycle-plan-v6/binding.json --binding-sha256` with the digest above.
Final independent review and documentation validation are recorded in the
private package validation manifest.

## Next acceptance

One separately authorized physical trial must still observe disconnected-clock
retention, GPS loss/recovery, one warm restart clearing time, fresh protected
reconnect/clock sync and retained region, followed by verified original restoration.
No physical case, V1 percentage or public website status changes through this
preparation. No firmware/app rebuild, Git mutation or publication occurred.
