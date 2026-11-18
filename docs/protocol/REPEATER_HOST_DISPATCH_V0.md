# Repeater host dispatch composition v0

Owner-approved OT-0266b P3-B host contract, October 28, 2026. This C++ composition
is a deterministic host seam, not a radio wire format or production adapter.
The [decision packet](../testing/OT-0266b-REPEATER-PROVISIONING-RECOVERY-2026-10-01.md#p3-b-implementation-decision-candidate)
retains the accepted profile, seven-file scope and F1-F12 fault criteria.
P1/P2/P3-A and Packet-v1 remain unchanged. D1-D7 are unchanged.

## Owners and trust

The nonmovable dispatcher privately owns the real P2 lifecycle controller and its
post-authenticated/new-key admission gate. The existing forwarder is the sole
fixed FIFO. P2 owns pending bytes, original verified metadata/enqueue age and the
one-use final claim. B owns source/global and airtime accounting, one bounded
attempt, terminal/rearm state and coarse diagnostics. There is no raw mutable P2
escape, second payload queue, route, retry scheduler or endpoint ACK generator.

All external authority, role/replay, ledger and radio ports outlive the dispatcher.
Every operation/callback is serialized, finite and nonthrowing. Reentrant mutators
refuse before changes. Synthetic trusted providers/ports promise coherent evidence;
no host Boolean, ordinary record, CRC or fake driver establishes production
source authentication, protected storage, clock rollback defense or RF behavior.

## Finite fixture profile

Core fixture N=2, M=255 and logical payload occupancy cap N*M=510; boundary N=1/8
and M=1/255. Queued + pending + in-flight ownership never exceeds N. Status logical payload
bytes are the conservative occupancy*M charge, not a sum of actual frame lengths. Eligible new
admission during pending/in-flight/rearm is denied through actual replay persistence.
During synchronous submit, pending custody and a copied port buffer represent
one candidate: logical occupancy counts it once; memory accounting charges both.
The full eight-slot allocation still exists at smaller N. Fixed ledger has K=2
verified synthetic sources, one admission/source and two global admissions/epoch.
No new-source eviction, dynamic roster, borrowing or automatic replenishment.

The existing reference PHY is 125000-Hz bandwidth, preamble8, SF7, CR denominator5,
explicit header/CRC enabled, LDRO disabled. Always charge maximum-frame C=399616us,
including shorter frames. Test A=C-1/C/C+1/2C. Queue age T=100ms and grant deadline
10000ms are host fixtures. Terminal/rearm H=1000ms uses H-1/H/H+1 and checked clock
arithmetic. Nominal terminal completion is no earlier than ceil(C/1000)=400ms;
second-source success uses a fresh frame after terminal and explicit rearm rather
than renewing an old queued frame. None of these values select production limits.

## Host interfaces

`RepeaterDispatchController` exposes guarded recover/consent/admit/service,
terminal/rearm handle consumption, notification, disable/shutdown and diagnostic
clear/status. It privately owns P2; no caller takes a payload ticket or bypasses
the final claim. `RepeaterDispatchLedgerPort` provides read and full-record
`compare_exchange`; B follows successful commit with exact readback.
`RepeaterDispatchRadioPort` provides proof observation, atomic expected-tuple
submit and one-use terminal/rearm evidence consumption. Its bounded
`invalidate_quiescence()` clears retained readiness on possible/uncertain
submission or fault; it cannot create a new ready proof. The retained port must
remain blocked across B destruction until explicit verified reconciliation. These are synthetic
nonthrowing ports, not implementations of protected media or real drivers.
The versioned C++ ledger record is an ordinary host value, not a persistent
serialization/schema or Packet-v1 extension.

## Ledger ordering and irreversibility

Trusted fixtures preseed explicit retained records; B never initializes, erases,
refills or rolls over a budget epoch. Domain/epoch/sequence and source/context
bindings are explicit and nonzero/nonwrapping. Global credit/airtime identity
survives P2 instances, lifecycle, consent and context changes. Missing, malformed,
legacy, mismatched or uncertain records stay NOT_READY. Zero remaining credit is
valid exhausted state, never an excuse to recreate a full allowance.

Admission debits source/global credit only after A authenticates shape, source,
context, permission, time and a new replay key. Compare expected prior sequence
and full record, commit once and exactly read back resulting state. Actual replay
save/readback follows. Denial still spends replay; later queue/rate/save failure
refunds no admission credit. Uncertain accounting contains after replay ordering.

Airtime is a separate irreversible full-C transaction before P2 claim, bound to
one attempt and original candidate. No submit without exact verified readback.
Possible commit/readback mismatch contains; no retry resolves uncertainty. No
refund on final-claim failure, definite busy/rejection, timeout or possible emission.
New recovery uses actual retained ledger bytes, never volatile counters as credit.
The ledger is not a durable payload outbox or attempt-recovery journal; the
retained synthetic radio port owns unresolved submission/terminal/rearm evidence.

## Dispatch, completion and cleanup

| Phase | Required evidence and effect | Refusal / ownership |
| --- | --- | --- |
| Recovery | Explicit P2 recovery/fresh consent, exact retained ledger and original driver/quiescent proof. | No default-ready port or fresh-credit fallback; no automatic activation. |
| Admission | Delegate actual P2/A with B's private accounting gate. | Busy/exhausted eligible keys persist replay. Invalid/duplicate input does not allocate or debit source state. |
| Candidate / debit | P2 metadata ticket retains original bytes/age; B commits/readbacks full C once. | Exhausted airtime or failed final setup retires the whole volatile P2 lifetime through existing notification; no unapproved single-slot discard API. |
| Final claim | Recheck original driver/readiness proof after callbacks; P2 checks original authority/age and consumes claim. | Before claim authority loss gives zero callbacks. Spent debit stays spent. |
| Atomic submit | Callback borrows P2 bytes only until return; port atomically enforces the same expected driver/readiness tuple before copying into fixed storage. | Callback invocation, port call and accepted/possible emission are distinct. Changed proof can produce a callback but zero accepted copy. No borrowed pointer retention. |
| Await terminal | Exactly matching attempt/driver/P2 instance/lifecycle/candidate and budget domain/epoch/receipt proof. | Accepted queueing is not completion or ACK. Partial/unknown submission contains with possible emission; no resubmit/refund. |
| Await rearm | Known terminal then separately bound explicit successful receive readiness. | An explicit busy/rejected result with zero accepted bytes is its no-emission terminal boundary and goes directly to the explicit rearm phase; it does not create readiness. Failed/unknown/stale/duplicate/out-of-order events grant no readiness; original H deadline expires at equality. |
| Suspend / revoke / restart | Retire pending/FIFO; retain spent or uncertain ledger/replay/radio state. | Fresh explicit recovery and consent, no lost-frame resurrection or automatic retry. |
| Shutdown | Terminal/idempotent; reject late operations without state/counter/ledger mutation. | Already emitted bytes cannot be recalled; a new separately admitted lifecycle recovers retained uncertainty. |

Each terminal/rearm phase retains its own original start/deadline; wrong,
duplicate, stale or reread events never refresh H. Terminal-to-rearm transition
starts the separate bounded rearm phase once, not on every observation.

IDs never wrap or reuse. Driver readiness is not inferred from transport status,
cooperative service or a fresh fake. Reconstruction retains the radio port's
uncertainty or requires exact quiescence / terminal+rearm reconciliation. A B
uncertainty notification also invalidates the retained port when no submit had
yet occurred; a newly constructed B cannot erase that observation. An independent reconciliation-required latch also covers pre-submit radio
unavailability and failed rearm, where no possible emission or live attempt
exists. Reconciliation
requires a newer driver proof or advanced readiness and quiescence receipts,
never the same previously observed ready proof.

## Bounded work, diagnostics and acceptance

At most one candidate/claim/submit, one corresponding ledger transaction per phase,
one bounded terminal/rearm event and one expiry pass of at most eight heads per
service call. No wait-until-ready loop, dynamic backlog or steady-state heap.
Measure full B/P2/ledger/port storage and bounded local copies under the host ABI;
logical N*M is not an allocated-RAM budget or a target stack qualification.

Diagnostics are fixed coarse state, occupancy and saturated uint32 counters plus
overflow/coarse-fault indication. Event record capacity is zero. No source history,
raw exceptions, protected bytes, key/location/identity export or logger integration.
Clearing diagnostics cannot change replay, ledger, attempts, authority or readiness.
Any possible-emission status refers to the current outstanding attempt; clearing
it after verified reconciliation cannot prove that no historical RF emission
occurred. No physical emission claim is inferred from host status alone.

Fault tests compose actual A/P2/replay owners and retained fake ledger/radio ports.
They cover both transaction phases, caller mutation/reentry, exhaustion, exact
terminal/rearm bindings, timeout/clock/identity boundaries, reconstruction and
terminal cleanup. Existing128 reported cases and existing four-group airtime
suite remain distinct regressions. Host success does not qualify P4/P5/P6,
production encryption/storage, regulatory airtime, physical power cuts or radio.
