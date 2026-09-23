# OT-0247f asynchronous serial timing model

OBSERVED 2026-09-22. Host implementation and validation complete; owner review pending.
The physical seven-delivery expiry is not reproduced. Asynchronous reads explain
most of the missing read/guard counts, but not the physical elapsed-time deficit.

## Scope and lineage

Canonical project C:/lu/OpenTrail; active checkout .private/ot177-publication.
[OT-0247e](OT-0247e-HOST-REPLAY-2026-09-22.md) is owner-accepted. Its synchronous
results and fifteen physical/source inputs are frozen in private
`.private/ot0247f-async/inputs.json`. The actual production Endpoint/EnrolledBridge,
composed C++ session/protocol/crypto/NVS stack and real driver remain in use.
Only host adapters and evidence changed. No production behavior was corrected.

The C++ oracle executes each target command once. Python separately schedules
host time, target completion and ordered response chunks. A write returns before
target completion; bytes cannot be read before their scheduled availability.
Read/guard intervals can overlap target SDK work and do not charge that work again.

| Transition | Required behavior | Forbidden behavior / failure |
|---|---|---|
| Fresh endpoint to write | Existing guards; execute one target command | Duplicate execution or cached identity |
| Write to waiting response | Early return; bounded empty/partial reads | Future response visible early; clock reversal |
| Waiting to parsed response | Ordered bytes; original deadline checks | Late response revives authority |
| Completed TX to receive | Existing driver maintenance and guarded rearm | Queueing mistaken for completion or admission |
| Failure to teardown | Preserve first rejection; close/reap host child | Cleanup replaces original fault or leaks child |

The oracle is nonpreemptive and globally serializes target commands. Button/context
events falling inside a command are applied at its completion boundary. This is
not a physical dual-target scheduler; autonomous idle loops, actual USB scheduling,
crypto and flash-write runtime remain outside its cost model. Input events are
merged in timestamp order. First target rejection layer/now/deadline is captured
separately from final invitation margins and later cleanup.

## Results with unchanged nominal costs

SDK gets cost209us; guards cost14,623us, the prior physical aggregate mean.
Staggered confirmation proxies and radio airtime model remain as in OT-0247e.

| Combined scenario | Full elapsed s | Deliveries | Guards | Reads | Final A / B margin s |
|---|---:|---:|---:|---:|---:|
| Retained/re-run synchronous | 57.612444 | 8 | 781 | 159 | 3.038556 / 3.140556 |
| Asynchronous response at completion | 57.518483 | 8 | 1,545 | 538 | 3.132517 / 3.234517 |
| Asynchronous byte spacing87us | 57.518483 | 8 | 1,545 | 538 | 3.132517 / 3.234517 |
| Physical OT-0247d | failure at61.210639 | 7 | 1,482 | 541 | original rejection A-0.010 / B-0.017 |

Physical failure elapsed and simulated full-cleanup elapsed have different phase
boundaries; they must not be subtracted as a precise missing-runtime estimate.
The immediate asynchronous case has275 empty reads and33.425948s of read time
overlapping target execution, plus11.355789s of guard overlap. SDK modeled work is
44.940016s, not44.940016 plus all serial waits. Full combined command count157
includes SYNC, compared with physical134; totals alone do not establish fidelity.
Synchronous and asynchronous variants also execute all four earlier schedules.
The configured115200 baud suggests87us per10-bit byte as a sensitivity scenario;
it is not a measured USB byte-arrival trace. Host guard intervals absorb this
spacing in the observed model, so the two asynchronous nominal results agree.

## Phase comparison and remaining uncertainty

| Phase | Physical duration s | Async duration s | Physical / async commands | Physical / async guards | Physical / async reads |
|---|---:|---:|---:|---:|---:|
| Confirmation polling | 9.551739 | 9.577977 | 44 / 62 | 328 / 402 | 98 / 108 |
| Activation1 | 3.177558 | 2.870903 | 5 / 5 | 65 / 65 | 25 / 25 |
| Activation4 | 3.824824 | 3.402066 | 7 / 7 | 85 / 83 | 32 / 31 |
| Four A status transfers | 13.267292 | 11.633076 | 20 / 20 | 270 / 276 | 105 / 108 |

Confirmation elapsed already agrees; fewer progress queries are not demonstrated
to shorten that waiting interval. Activation1 still differs by0.306655s despite
matching command/guard/read counts. Four A transfers differ by1.634216s even though
the model performs more guards/reads. Physical per-phase guard costs vary; the
fixed mean and SDK-only execution model do not identify all runtime. No unique
physical cause is established. Complete phase accounting is retained privately.

An adversarial response scenario delays the first chunk100ms and each64-byte chunk
another100ms, using the existing read-timeout cadence as a stress assumption. It
fails during confirmation at62.409843s with zero status deliveries. This is neither
a measured USB latency bound nor the physical seven-delivery reproduction. The
capture lacks byte-arrival timestamps; this case shows sensitivity, not causation.
No delay was fitted to obtain seven deliveries or a particular rejection ordinal.

## Validation and failure behavior

Final validation covers10 scenario runs (four synchronous, four asynchronous,
wire-spacing and timeout-cadence), seven negative controls and eight deterministic
transport/process controls. Nominal cases complete all eight deliveries and verify
protocol cleanup; the adversarial confirmation refusal is retained as a stress result.

- Original identity-loss, late-authority, malformed-RFREADY and failed-rearm controls
  retain their respective refusals. No later unauthorized peer admission.
- Context loss during delayed response rejects the subsequent guarded operation.
- Delayed response past the host read deadline rejects with `late_read`.
- Separate controlled authority-expiry case records original target rejections
  A63912>=60751ms and B64985>=60853ms; host rejects with `late_write`.
- Identity loss closes handles but leaves target secrets uncleared in the oracle
  because further I/O is forbidden and autonomous cleanup is not modeled. Other
  negative cases clear both sessions, but protocol cleanup is verified only where
  the recorded response sequence actually proves it; handle closure is distinct.
- Deterministic transport control proves early write, empty/partial reads, ordered
  response, monotonic clocks and exactly three command executions. It records
  105381us read overlap and1000us guard overlap without duplicate execution.
- Seven child-error controls cover startup EOF/bad reply/timeout, RPC EOF/timeout/
  malformed reply and broken stdin. All children are reaped; original errors survive.

Final host compile/link and40 existing bridge tests pass. Independent review found
no blocking scheduling issue and accepts the bounded model claims. Exact commands,
results, source/artifact hashes and document checks are in private
`.private/ot0247f-async/manifest.json`. No firmware target rebuild was required:
production source was unchanged by this task.

## One measured correction recommendation, not implemented

Investigate combining sender completion observation and guarded receive rearming
into one dispatch. Fourteen separate physical rearm operations occupy9.942914s,
including202 guards and80 reads. This is measured operation occupancy, **not**
9.942914s of removable net cost: durable checks, target execution and RX transition
are required, and work overlaps. The proposal must retain fresh authority/context/
deadline checks, explicit completed-TX state, driver failures and verified RX
readiness before peer advancement. It may remove a round trip and redundant entry
work, never cache authorization or suppress necessary rearming.

Any production change needs its own approved scope, exact before/after operation
accounting, successful and negative async regressions and measured net benefit.
Do not return to a device merely because the host model passes. No physical retry,
production correction, artifact signing, Git mutation/publication or deployment
occurred. Existing originals/restoration remain unchanged. V1 progress and public
website status did not change; local work remains uncommitted.

## Dated erratum - 2026-09-22, OT-0247g

The nominal-success claim above was too broad. The retained asynchronous
`baseline` and `delayed_only` runs failed confirmation with zero statuses; these
were nominal scenarios, not stress inputs. The host adapter advanced 600 ms from
the host clock while target press processing was already ahead, shortening the
session-observed hold. Original report text and evidence are retained as history.

The [OT-0247g correction report](OT-0247g-GUARDED-COMPLETION-2026-09-22.md) records
the adapter correction, exact-source paired replay, separate raw-button and
post-press-processing timing, and full before/after results. With one corrected
adapter shared by both production versions, all nine nominal schedules deliver
eight statuses and verify cleanup. Only the separately configured read-bound
sensitivity remains a stress failure. This erratum does not establish physical
acceptance or reproduce the seven-delivery physical expiry.
