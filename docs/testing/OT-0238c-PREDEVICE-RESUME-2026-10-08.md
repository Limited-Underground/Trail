# OT-0238c pre-device coordination correction, 2026-10-08

Canonical project: `C:/lu/OpenTrail`. Selected worktree: `C:/lu/OpenTrail/.private/ot177-publication`. This increment concerns the computer launcher. Full OT-0238c remains In Progress; it does not establish physical enrollment acceptance.

## Observed setup boundary

The owner completed the word/position check and explicitly confirmed both screens were readable. The four computer-only practice prompts also completed in 66.5177 seconds. This is a human-only timing lower bound, including modeled gestures; it does not prove the complete device exchange fits its deadline.

The old session contains only `owner-workflow-start.json`. The 22:08:48Z read-only process snapshot found no matching Python workflow or worker. No capture grant, launch intent, candidate grant or device journal was created. The device test did not start.

The workflow waited 300 seconds for root to record authorization after practice. Practice finished at 21:55:45.319Z; the actual new human approval arrived at 22:00:53.733Z. Those saved times and the stopped pre-device state support authorization-wait expiration. The old workflow did not persist its terminal category, so that category remains **UNKNOWN**, rather than a fabricated `wait_expired` receipt. The coordination delay is a host execution failure, not a firmware failure.

Exact immutable observations are in [coordination-observation.json](../../.private/ot0238c-predevice-resume-20261008/coordination-observation.json) and its referenced baseline and predecessor files.

## Correction boundary

The maintained launcher gains a narrow pre-device resume path with an independently reviewed predecessor-to-successor relation and a fresh nonce. Previous word, readability and practice evidence stays historical and retains its original timestamps. The resumed launch checks the current visible desktop and asks once whether the window is readable and the owner is ready. It does not repeat the puzzle or modeled button practice.

Fresh actual chat approval is recorded before that final visible checkpoint. The resulting authorization binds its original reply time and the new readiness receipt. No second short root-transcription wait follows the click. A predecessor with an active process, capture grant, launch intent, device journal, changed source/evidence or unexpected session state is refused. Duplicate successor starts and uncertain window clearing also refuse.

The v16 runtime, all 14 core policy sources, candidate firmware and physical instructions remain unchanged. The 900-second initial freshness checks, shared 120-second enrollment window, 2,100-second execution ceiling, 3,600-second restoration ceiling, independent live handoff review and custody/restoration requirements remain separate and unchanged. Held continuation uses the new readiness anchor with the original issued device ceilings; it does not renew them.

## Validation and remaining gate

The accepted affected host matrix passes 76 tests: 12 resume, 35 interactive-launch, 24 owner-workflow and 5 visible-session cases. Source and receipt review verifies all eight source/test pins and the four accepted logs. The first focused run retained one synthetic binding-pin mismatch; the first owner-suite run retained one synthetic result-envelope mismatch. Fixture corrections preserve the production guards. Unchanged passing suites are reused rather than rerun after the owner fixture correction.

A separate real Windows process-API check passes three cases: no matching fixture process, rejection of one harmless owned hidden Python child, and acceptance after that child closes. It executes the exact prehashed helper bytes, verifies the source before and after, and retains no command lines or private identifiers. It uses no native UI, device admission, ports, real session or grant. [Host matrix](../../.private/ot0238c-predevice-resume-20261008/host-gates.json); [Windows process check](../../.private/ot0238c-predevice-resume-20261008/native-process-check.json).

Independent source, static-input, succession-manifest, binding, owner-plan and final literal-launcher reviews pass. The actual compiled template's non-operational `inputs()` and static-review checks also pass. Review first found an omitted local candidate-template dependency; its correction and original observation are retained. A separate exact-bootstrap regression demonstrates the original CMD's missing import path and the corrected helper resolution from the launcher's folder, without running a Workflow or native path.

The familiar `.private/ot0238c-neutral-time-20261006/Start Trail check with more time.cmd` now contains the reviewed 1,158-byte resume launcher, SHA-256 `ab27de5319cd34f2aabdcca2998f3835b7e15e012ea7a5b8ff2498d482690ccc`. Its previous 1,508 bytes remain in the existing `.private/ot0238c-launch-binding-20261008/Start Trail check with more time.cmd` copy. No new backup is made. [Final literal review](../../.private/ot0238c-predevice-resume-20261008/prestage/independent-final-launcher-review.json); [exact repoint receipt](../../.private/ot0238c-predevice-resume-20261008/familiar-launcher-repoint.json). Earlier generation receipts describe the preserved original CMD; supplemental correction receipts own the final bootstrap.

CI input freezing now includes the four affected external modules. Only the two self-contained suites (35 interactive and 12 resume cases) are newly registered. The other 29 locally passing owner/visible cases still need portable fixtures before clean-checkout CI registration; their historical ignored-template dependency is not hidden or skipped. Independent leaf review verifies exact six additions and preservation of the already-dirty runner and all prior C++/Python entries. These are two suites covering 47 cases, correcting the v2 registration receipt's prose typo. No full or clean-checkout CI execution is claimed. [Independent CI leaf review](../../.private/ot0238c-predevice-resume-20261008/prestage/independent-ci-leaf-review.json).

Existing v16/core and firmware validation is reused; no rebuild or whole protocol matrix is repeated. Fresh successor nonce `6c322863ed8021b92ca0122ab1d23af2` has empty readiness/session directories. Neither CMD nor the current readiness popup has been run. The correction remains local and uncommitted.

The owner has deferred the device session until later. No new real approval intent, grant, native readiness window or device action is authorized by these source checks. A later fresh readiness/authorization step is required before one physical attempt. No V1 completion credit, Git publication or public website capability change follows.
