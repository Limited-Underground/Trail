# OT-0238c query timing and separate cleanup failure

As of 2026-10-02. Host-only implementation complete; owner review pending.
The full OT-0238c task remains In Progress. No devices were accessed.

## Result and boundaries

The [previous physical startup failure](../../tests/hardware/OT-0238c-STARTUP-A-2026-10-02.md)
remains `hello_A / deadline_expired`. Its underlying cause and the earlier B
cleanup category remain unknown; new logging cannot recover missing old facts.

Five controller modules now retain fixed, bounded diagnostics:

- The controller captures the original shared startup allowance, sampled opening
  cost and clipped HELLO allowance using existing authority-clock samples only.
  Opening cost is a lower bound if opening fails before its final clock check.
- The passive wrapper records remaining allowance after its initial owner check,
  progress/prelude cost and the exact wrapper/controller boundary. An absent
  transport snapshot explicitly means the Endpoint was not entered.
- The USB client records actual guard count/cost, attempted/completed writes and
  reads, received byte count, validated parsing/refusal and accepted return.
  It retains no command values, reply bytes, device identifiers or secrets.
- Performance timing uses an independent observation-only clock, with bounded
  integer nanoseconds. Invalid timing becomes unavailable and never changes
  authority. Observer exchange/close reentry cannot publish a successful reply.
- One fixed summary is persisted after passive closure under the original
  execution authority. No per-I/O durable callback is added. Expired authority
  never permits a late summary; summary failure preserves an existing rejection
  and cannot turn a formerly successful trial into accepted success.
- Custody retains its first cleanup failure independently of the primary failure,
  across pending records and recovery. Runner receipts propagate both and retain
  the first backend restoration exception's method, limit/call/postcheck boundary
  and original clock domain. Unknown exceptions retain only a fixed category.

Fresh guards, authority-clock call sequence, parser, commands, write/read limits,
all deadlines, no-retry rules and original restoration behavior are preserved.
New readers accept historical journals without modifying them during parsing.
Frozen v6 readers cannot read newly extended journals: compatibility is one-way.
The additive v7 capsule does not authorize an operational package or trial.

## Validation

- Final affected matrix: **10 suites / 291 tests passed**, with stable input
  pins and the existing 375 firmware dependencies unchanged. No firmware rebuild.
- Fifteen independent edge checks and five-source frozen-v6 review passed.
  Review caught and corrected final observation-callback reentry through close.
- Focused client/custody/composed regressions cover pre-write expiry, no reply,
  parsed-but-rejected reply, clipped startup budget, invalid timing, reentry,
  summary rejection, primary plus B cleanup failure, and successful recovery.
- Two baseline diagnostic assertions fail against the exact frozen v6 client,
  then pass against the correction. Historical capsules/receipts remain intact.
- Additive v7 fourteen-policy runtime verification and both actual isolated inert
  entrypoints passed without enumeration, device access or grant consumption.
- A preliminary matrix was excluded when review changed its inputs; the stable
  final matrix above is the final host validation run.

Commands use `C:/Python314/python.exe -X utf8 -B`. The maintained private helper
`validate_host.py` runs `matrix`, `package`, then `entrypoints`; each receipt pins
its exact commands, inputs and logs. Evidence directory:
`../../.private/ot0238c-terminal-telemetry-20261002/`.
See [matrix receipt](../../.private/ot0238c-terminal-telemetry-20261002/final-matrix/matrix-receipt.json),
[independent review](../../.private/ot0238c-terminal-telemetry-20261002/independent-review.json),
[v7 packaging](../../.private/ot0238c-terminal-telemetry-20261002/packaging.json) and
[isolated entrypoints](../../.private/ot0238c-terminal-telemetry-20261002/inert-entrypoints.json).
Documentation checks and final worktree preservation are retained in closeout.

## Next gate

Review one v7-bound startup-only procedure and exact private unit/image inputs,
then obtain fresh device readiness/authorization for that one diagnostic trial.
Its acceptance requires a bounded terminal record distinguishing pre-write,
write/read and post-parse boundaries, plus independently verified restoration
and owner screen confirmation. This report grants no device operation or retry.
Enrollment and lifecycle acceptance, physical cause, V1 credit, Git publication
and public website status remain unchanged. Implementation is local/uncommitted.
