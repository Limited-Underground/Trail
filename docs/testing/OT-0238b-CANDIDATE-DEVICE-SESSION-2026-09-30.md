# OT-0238b full session through actual display/input owners - 2026-09-30

Approved revision 2, evaluation-only computer integration. This extends the
[supported recovery flow](OT-0238b-CANDIDATE-RECOVERY-2026-09-30.md) by replacing
the session fixture's direct screen/button double with the existing real
review port, Heltec input arbiter, startup display owner and OLED renderer.
OT-0238b remains In Progress; production routing and physical acceptance remain.

## Complete flow and correction

The trusted session supplies its exact reserved boot/generation/request context.
The one-shot adapter binds it to the actual input owner, acquires one exclusive
display lease and checks the observed context and initial revision. The same
real review-port lifetime spans complete fingerprint pages, debounced local
gesture, fresh possession/invitation/handshake, a newly rendered transcript
comparison, durable activation and eight statuses, four in each direction.
Retained rekey and both supported recovery directions pass through those owners.
Low-level SDK I/O, storage drivers, entropy and peer delivery are simulated.

A refused display acquisition previously had no exact context cleanup API.
The arbiter now releases only its matching admitted context after the lease is
gone, including this pre-acquisition refusal. It cannot clear a newer context,
redraw over PIN/reset, cancel a committed reset or manufacture confirmation.
The generic adapter records actual release/concealment outcomes; failed cleanup
does not become a successful close. Reentry poisons the attempt and defers cleanup
to the outer operation. A new session reconstructs a new one-shot adapter.

The real arbiter retains sole GPIO sampling and cached observation. Render
completion, revision acknowledgement, fresh release, debounce and reset priority
remain required. Held/bouncing buttons and identity-review gestures cannot
approve a later transcript page. Original request/invitation deadlines remain.

## Validation

- New complete actual-owner regression: **PASS 20 actual candidate device session groups**.
- Final current-source host matrix: **63 suites passed**, run once after
  source freeze. Focused compilation reuses unchanged, pinned crypto objects.
- Independent source/lifetime review, conservative closure over all 18 target
  directories, documentation tests and `git diff --check` pass.
- Bench standard and confirmation profiles each pass two fresh builds; their
  authoritative artifacts and repository dependencies match within each pair.
  Exact compiler/configuration, source/object/symbol evidence and hashes are
  recorded in `build-result.json`. The new adapter has no firmware caller;
  unused method elimination is recorded rather than called product integration.

Tests cover missing service ticks, clock rollover, held entry/bounce, stale
page/context cleanup, PIN acquisition refusal, reset preemption and fresh-release
handoff, render/restore faults, cancellation/reentry and expiry during drawing.
Cryptography, request/session/state/store/display/input owners are real code.
SDK time/button/panel behavior and physical visibility remain simulations.

## Remaining gate

The candidate handoff brackets a device sample with fresh authority samples.
A cached application tick can precede that bracket after a millisecond rollover
and safely refuse. The passing nominal host flow keeps SDK time fixed inside
each service operation; it does not prove production scheduling, render/NVS
latency or resource headroom. The rollover refusal is retained explicitly.

Next: establish source-backed clock/scheduling binding through the full candidate
session under elapsed-time tests, preserving exact contexts, reset events and
original deadlines. Remaining physical storage/transport/phone adapters,
production selection/binding and separately authorized device acceptance remain.

Exact receipts: `.private/ot0238b-session-device-20260930/closeout.json`,
`security-matrix-final/result.json`, `focused-device-session/result.json`,
`review-manifest.json`, `include-impact.json`, `target-preflight.md` and
`build-result.json`. Failed prototype checks and tool-environment failures are
retained in their command records, not substituted for passes.

No devices, cases or batteries were touched. No V1 completion credit or public
website capability changed. Changes are local/uncommitted; publication pending.
Existing OT-0332 work, earlier candidate evidence, separate GNSS checkout and
shared HomeAssistant focus are preserved.
