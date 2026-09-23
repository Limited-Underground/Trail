# OT-0247e production bridge host replay

OBSERVED 2026-09-22. Host validation complete; owner review pending.
Physical expiry is **not reproduced**. This result satisfies the approved alternative
of quantifying residual unmodeled behavior; it does not establish a physical fix.

## Inputs and implementation

The [physical trial](OT-0247d-PHYSICAL-2026-09-22.md) is the input, not an accepted
completion dependency. Private `.private/ot0247e-replay/inputs.json` freezes nine
physical/source inputs; `reconcile.py` verifies their hashes and reconciles commands.
The new `tests/host/enrolled_bridge_replay.cpp` includes the existing composed
fixture and real enrolled radio driver, session, protocol, crypto and NVS adapter.
`tests/host/enrolled_bridge_replay.py` drives it through the actual production
Endpoint and EnrolledBridge. No production source changed for this task.

Model inputs: 209 microseconds per SDK get; 14,623 microseconds per fresh identity
guard (physical aggregate mean); observed confirmation offsets 4.935931/10.620977
seconds as upper-bound button proxies, not calibrated physical button times.
Finite airtime is calculated from actual frame length at SF7/BW125/CR4:5,
explicit header and CRC. Hardware radio delivery is synthetic. Target B's fixture
clock is rebased to the shared simulated origin, not fitted to a physical outcome.
SDK execution is charged once. Physical serial-read duration is not added to it.

## Executed scenarios

| Schedule | Commands incl. SYNC | STATUS polls | Delivered | Full elapsed s | A / B final invitation margin s |
|---|---:|---:|---:|---:|---:|
| Baseline, immediate confirmation | 97 | 2 | 8 | 48.889109 | 11.761891 / 11.863891 |
| Staggered confirmation polling | 155 | 60 | 8 | 57.612444 | 3.038556 / 3.140556 |
| Finite TX completion only | 97 | 2 | 8 | 48.889109 | 11.761891 / 11.863891 |
| Both | 155 | 60 | 8 | 57.612444 | 3.038556 / 3.140556 |

All four complete verified protocol cleanup. Margins use actual observer-issued
invitation deadlines in the synthetic clock, after cleanup; they are not physical
clock measurements. The actual driver takes the completion-only/guarded-rearm path
in all four schedules: finite airtime does not change which subsequent command
observes completion here. This is not evidence that physical airtime is free.

The old 76-command handwritten model and this actual-bridge baseline are distinct.
Physical reconciliation: 76 +40 confirmation polls +14 rearms +8 startup -4
unfinished final-transfer/cleanup commands =134. Combined replay has155: physical134
+16 STATUS polls +1 final sender rearm +4 final-transfer/cleanup commands. No failure
ordinal or delivery count was injected. The successful count is asserted afterward.

## Quantified residual and limits

| Observation | Physical trial | Combined replay |
|---|---:|---:|
| Identity guards | 1,482 | 781 |
| Guard duration s | 21.671815 | 11.420563 |
| Serial reads | 541 | 159 |
| STATUS commands | 44 | 60 |
| Status deliveries | 7 | 8 |

The synchronous fake write executes the target command completely before returning
and queues its response. Physical writes return before target execution finishes;
empty/partial serial reads can therefore invoke more fresh guards. The replay has
zero empty-read wait. Its 701 fewer guards and382 fewer reads, despite21 more
commands, demonstrate a specific transport-model deficit. They do not prove a
10.251252-second additive correction: origins, command paths and overlapping target
execution differ. Physical STATUS transport takes9.551739s for44 commands; its
328 guards/98 reads contrast with whole-response replay behavior. Physical RFPOLL
has661 guards/266 reads across43 commands. Private transport tables retain details.

Combined SDK reads214,494 cost44.829246 modeled seconds. Concurrent target idle
work, crypto, flash writes and UART timing are not modeled. Radio completion and
button events are observed at service boundaries; maximum combined observation
lateness is248.119ms and84.310ms respectively. Receiver readiness is sampled at IRQ
observation, not exact airtime completion. This is a bounded discrete model, not
a continuous physical schedule. Two physical target clock origins cannot be
subtracted; physical expiry remains the originally captured A10ms/B17ms overrun.
No specific unmodeled cause is declared unique or proven.

## Negative controls and validation

- Identity loss: `passive_lease_invalid`; no later target command, host handles
  closed, protocol cleanup unverified. Target secrets remain uncleared in this
  oracle because autonomous target idle work is absent and further I/O is denied.
- Late authority: `late_write`; rejection and both target sessions cleared.
- Malformed RFREADY: `radio_statistics_invalid`; both sessions cleared.
- Failed rearm: `target_refused`; both sessions cleared.

All controls assert injection was reached and the expected rejection occurred;
independent review found no subsequent peer admission. Final host adapter compile
and link pass with the retained scalar crypto object set and actual driver object.
Four scenarios and four controls pass; unchanged production bridge tests40/40 pass.
Private `replay-commands.json`, build logs, profiles, controls, `summary.json` and
`manifest.json` bind commands/results/source hashes. Early adapter compile/link
failures are retained; they were harness integration errors, not firmware failures.
Independent review accepts the bounded residual claims, not physical reproduction.
Repository documentation checks are recorded in the manifest after this report.

## Bounded correction proposal, not implemented

Investigate avoiding redundant progress STATUS queries for a role already observed
locally confirmed, with fresh queries of **both** roles immediately before activation.
Every actual I/O retains its current identity guards; every target authorization,
context and deadline check remains. No cached result may authorize activation.
Before production implementation, validate asynchronous serial-response availability
and empty/partial guarded reads without double-counting target time; test expiry,
identity loss and context loss between confirmation observations and final queries.
Measure whether this saves work in that corrected transport model. If it does not,
reject the proposal rather than expand scope or promise a physical fix.

Owner acceptance of this evidence precedes any separately scoped correction or
physical retry. Both devices remain restored from OT-0247d; no hardware, RF, signing,
Git mutation/publication or deployment occurred. V1 progress and public website
status did not change. Changes are local and uncommitted.
