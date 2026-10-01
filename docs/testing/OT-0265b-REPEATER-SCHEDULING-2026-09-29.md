# OT-0265b Repeater scheduling and congestion proposal

Prepared 2026-09-29 under approved revision 1. This is a planning deliverable
for review, not relay implementation or measured radio acceptance.

## Proposed behavior in plain English

Use the accepted dedicated-first baseline: two endpoints and one explicitly
authorized repeater. The repeater keeps a small, fixed-capacity waiting list,
forwards an eligible protected frame at most once within its authorized replay
lifetime, and drops work that is too old or cannot fit the radio budget. It
does not keep retrying a busy or missing destination. Dropping a frame can lose
delivery; it must never produce a success message on the destination's behalf.

Recommend preserving already admitted work when the queue is full: refuse the
new arrival rather than evicting another sender's queued frame. Use FIFO for
the dedicated relay, with per-source admission limits as well as a total limit.
Do not introduce an emergency priority bypass. All traffic, including any future
authenticated priority class, pays the same overall finite radio budget.

Combined client/repeater mode remains the accepted future direction. It needs
separate local-client and relay capacity plus a fair radio scheduler; it is not
enabled by this dedicated-first plan. Disabling or losing the relay must leave
the base direct endpoint path usable. No capacity, battery duration, range or
delivery guarantee is proposed.

The owner can review these behavior choices now. Exact queue sizes, timing and
airtime values require the budget evidence below; no arbitrary numeric defaults
are supplied. Accepting this report does not authorize implementation, select a
board or select a new protected radio format.

## Sources and scope precedence

The accepted [role proposal](OT-0264b-REPEATER-ROLE-PROPOSAL-2026-09-29.md)
and [security review](OT-0265a-REPEATER-SECURITY-REVIEW-2026-09-29.md) supply
the planning baseline. Their earlier review-pending wording is historical;
their accepted recommendations are the starting point, not a request to approve
them again. The dedicated first topology does not select exact devices or a
multi-repeater network.

[Decision 0004](../decisions/0004-immutable-first-release-forwarding.md)
supplies immutable-object and bounded-forwarding constraints, while
[Decision 0033](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md)
supersedes its initial-release relay promise.
[Decision 0035](../decisions/0035-host-tested-secure-lora-key-transport-contract.md)
keeps OTSL0/v0 pairwise and direct-only. This proposal neither changes those
normatives nor makes OTSL0 relay-eligible. A future relay-capable construction,
its source authentication, forwarding permission and any outer envelope still
require separate review. A Boolean, mutable TTL or reserved field cannot grant
that authority.

The [host forwarder](../protocol/SINGLE_REPEATER_FORWARDING_V0.md) and
[replay coordinator](../protocol/SINGLE_REPEATER_REPLAY_COORDINATOR_V0.md)
provide useful bounded-policy and save-before-release evidence. Their finite
duplicate window is not complete cryptographic replay protection. Production
needs the accepted secure adapter and protected replay lifetime/counter rules;
forgetting an old cache entry must never make an otherwise replayed object fresh.

## Technical policy proposed for a future implementation

| Resource or decision | Proposed rule | Evidence needed before implementation values are frozen |
| --- | --- | --- |
| Input work | Bound frame size, parser work and authentication concurrency before allocation. Reject unsupported versions/classes. A saturated verifier refuses work rather than creating an unbounded verification backlog. | Exact protected format, maximum bytes and measured verification cost on the candidate target. |
| Queue | Fixed slot and byte caps, one owned immutable copy per admitted frame; FIFO among retained dedicated-relay entries. Refuse newest on full. No hidden overflow or durable outbox. | RAM budget covering full protected objects, any envelope, metadata, crypto scratch, radio buffers and reserved system headroom. Existing eight-slot host storage is a reuse ceiling, not a selected product queue depth. |
| Fair admission | Total and authenticated per-source admission budgets. Source tracking has a finite membership bound; unknown sources cannot allocate permanent entries. Per-source denial does not bypass the aggregate limit. | Accepted maximum membership and offered-load/burst profile; demonstrate a noisy admitted source cannot occupy every opportunity indefinitely. |
| Duplicate/replay | Authentication, membership, epoch and immutable permission precede replay mutation. Same identity/context/epoch/object key cannot create another forwarding opportunity; conflicting bytes are refused. No replay identity derived from unauthenticated metadata. | Reviewed identity/message/counter binding and cryptographic replay retention. Cache saturation must refuse new work or use a proven secure replay floor, never evict protection just to improve throughput. |
| Congestion and durability | Preserve the existing consumed-opportunity tradeoff: eligible new keys denied by queue/rate pressure are still durably observed before further release. Invalid/unauthorized input cannot poison legitimate replay state. Save failure or uncertainty contains forwarding. | Bounded storage latency/write demand and wear budget, including authenticated denied traffic. If that load is infeasible, a separately reviewed admission/persistence design is required. |
| Age | Use one nondecreasing local clock from original admission. Drop at age greater than or equal to the chosen maximum; recheck immediately before radio submission. Queue movement or backoff never refreshes age. Clock regression/uncertain age drops or contains affected work. | Reviewed useful-message age and worst-case verification, persistence, queue, backoff and radio-start delay. Local queue age alone is not proof of source freshness; the protected construction must supply any required end-to-end freshness. |
| Rate and airtime | Enforce both a finite packet-admission rate and a transmit-airtime budget. Admission rate alone is insufficient because queued traffic can drain in a burst. Reserve the full conservative transmission cost before dispatch; no balance below zero and no fresh budget merely on restart. | Exact region/PHY/frame costs, burst cap and sustained limit; persisted accounting or a conservative restart quiet period whose safety is established by the reviewed regional policy. |
| Retry and radio ownership | Recommend one radio submission per admitted relay object. A definitely rejected submission ends that opportunity; possible RF emission is uncertain and is never retried automatically. Receive rearm is explicit after completion/error; no next transmission while the driver is unresolved. | Driver's admission/completion/timeout contract, worst-case command duration, rearm timing and bounds on any internal hardware retries. |
| Expiry while waiting | Expire queued work even when radio or airtime admission stays unavailable. Bound each scheduler turn and retained entries; never spin until the medium becomes free. | Finite wake/timeout behavior, fair CPU service and a measured upper bound on cleanup delay. |

The one-submission recommendation is deliberately conservative. A later request
for repeat attempts, coalescing, priority preemption or saved-frame recovery
changes the loss and amplification analysis and needs an explicit reviewed
successor. It cannot be added as an implementation convenience.

## Start-to-finish ownership and recovery

| Before state / owner | Trigger and required transition | Forbidden effect | Failure or release |
| --- | --- | --- | --- |
| Disabled / role and storage owners | Verify current role, context, epoch, replay recovery and airtime recovery before Ready. | Empty or corrupt storage treated as fresh authorization or a full airtime allowance. | Remain disabled pending explicit recovery; no automatic reset of protected state. |
| Ready / secure input adapter | Bounded parse and authenticate eligible protected bytes; bind verified metadata to that exact object. | Direct-only frame forwarded because of a caller flag. | Reject before changing another object's replay state. |
| Eligible / replay and queue owners | Check replay, finite source/global admission and queue capacity; persist every eligible new observation according to the consumed-opportunity policy. | RAM insertion treated as durable, or a congestion replay admitted later as fresh. | Definite denial is dropped; uncertain save contains all queue release. |
| Waiting / scheduler | Retain immutable bytes until their original deadline; choose an eligible FIFO head only while authority and budget remain current. | Head-of-line busy wait or extending expiry on a later scheduler tick. | Drop expired/revoked work, release its buffer, preserve replay protection. |
| Dispatch / radio owner | Recheck role/epoch/age, reserve airtime and atomically claim this attempt before submitting exact bytes. | Disable/rekey racing between the last check and dispatch; late callbacks authorizing replacement work. | Before-submission stop emits nothing. After submission, bytes cannot be recalled; debit conservatively and report uncertain emission if completion is missing. |
| Submitted / radio owner | Receive a bound terminal callback or reach the finite timeout, then explicitly restore receive readiness. | Queue acceptance reported as RF completion or receiver delivery. | Release frame ownership; unresolved radio state disables further submissions until reconciled. |
| Receiver / endpoint owner | Authenticate and durably admit independently; only an exact protected endpoint ACK can prove endpoint admission. | Relay-generated success, or phone-display/human-read claims. | Sender's finite endpoint deadline governs its result, independently of relay status. |
| Disable, epoch replacement or role change / authority owner | Stop new admission and retire queued old-context work; serialize with dispatch and invalidate stale callbacks. | Draining old epoch after activation or automatically restoring a retired role. | Preserve safety state; new authority follows the separately reviewed provisioning/epoch procedure. |
| Restart / recovery owner | Recover verified replay and airtime constraints before new work. | Resending saved-but-unsent RAM work, resetting budgets or lowering replay state. | Lost queued work remains lost; uncertain durable state requires service. |

No dynamic route discovery, alternate-relay election or second hop is included.
Relay disappearance is a loss of optional forwarding. Endpoints retain their
direct behavior; they must not create an unlimited search/retry flood to replace
the absent repeater.

## Combined-mode scheduling constraint

For the future combined profile, recommend separate finite local-client and relay
queues with reserved admission capacity, and weighted deficit round-robin charged
in airtime rather than packet count. Both classes receive positive service share
within one overall radio budget, and neither may borrow the other's reserved
queue capacity. Cap accumulated scheduling credit so a previously idle class
cannot create an unlimited burst. Stop/disable and recovery controls do not wait
behind data queues; they grant no RF-budget exemption.

Weights, class capacity, maximum service gap and whether bounded unused airtime
may be borrowed remain a separate combined-mode policy decision. A finite useful
latency claim is conditional on bounded offered load, available radio service
and a feasible airtime budget; overload is handled by finite refusal/expiry.
Relay disablement retires only relay work while valid local-client work keeps
its own ownership and security gates. This preserves
[Future Concepts](../FUTURE_CONCEPTS.md) without claiming combined operation now.

## Budget evidence required before numeric commitments

Use the [protected packet budget](../protocol/PROTECTED_PACKET_BUDGET_V0.md)
and [group load model](GROUP_LOAD_MODEL_V0.md) as accounting methods. Their
historical header/signature sizes, four/eight-client profiles, bench PHY,
percentage demand and fragmentation cap are examples, not selected relay limits.

For each proposed profile, record the exact byte count and computed airtime of
every permitted source frame, relay copy, protected ACK, required control frame
and permitted source retry. Charge every fragment separately if fragmentation
is later approved, and charge any future outer envelope. Let total demand be
the sum of those bounded counts multiplied by each frame's full airtime. Show
both normal demand and maximum permitted simultaneous burst; do not infer
collision-free capacity from average demand. Separate each transmitter's allowed
budget from aggregate channel occupancy and leave reviewed headroom for receive,
channel access, turnaround, interference and recovery.

The review packet must include: finite slot/byte/source limits; actual PHY and
region-specific constraints; maximum frame cost; burst and sustained airtime
accounting; original queue age; sender attempt caps; measured authentication,
storage and radio/rearm latency; memory high-water mark; storage wear estimates;
and idle/loaded power measurements on the exact candidate. Calculations are
host planning evidence; target measurements and authorized RF evidence remain
separate gates. No regional compliance claim or regulatory choice is made here.

## Discriminating test plan

These are required future cases and assertions, not tests executed by this task.
Exercise the real policy/coordinator and eventual adapter/scheduler owners;
transport doubles must expose submission, possible emission, completion and
receive rearm separately. All retained object bytes must compare exactly.

| Case | Required observable outcome |
| --- | --- |
| Nominal sender-relay-receiver | One eligible object, one persisted observation, at most one relay submission; receiver admission/ACK remains separately evidenced. |
| Loop/reflection | Reinject the relay output, self-source and endpoint repeats; no second forward. A client never forwards and a second/ambiguous authorized relay configuration fails closed. |
| Duplicate, conflicting bytes and replay | Exact duplicate suppressed; same identity with changed protected bytes refused; malformed/unauthenticated input cannot poison a valid sender key. Evict/expire a duplicate-cache entry and prove secure replay still rejects a replay outside that cache. |
| Queue/source/rate overload | Fill exactly each configured bound, then exceed it; bounded memory/work, refusal of new work, retained FIFO order and consumed-opportunity replay after congestion. Flood one authorized source while another stays within its share; no infinite starvation. |
| Airtime and variable sizes | Mix shortest and longest eligible frames; enforce aggregate cost, finite burst and no underflow. Drain a previously full queue and prove packet-rate admission cannot create an unchecked RF burst. |
| Age/clock | Just before, exactly at and after original expiry; long persistence/backoff delay; clock regression and restart with uncertain age. No deadline refresh or expired release. |
| Stale epoch/revocation | Rekey/remove role while queued and at the dispatch boundary; no old-context post-transition submission. Late completion cannot reactivate work or free another attempt's reservation. |
| Power/storage cuts | Interrupt before save, during uncertain save, after verified save before radio, and after possible emission. Reboot cannot amplify replay or restore spent airtime; saved-but-unsent loss is explicit. Denied-traffic writes remain within the reviewed storage budget. |
| Relay loss | Remove/disable relay and interrupt its power/network-independent radio path; sender remains finitely pending/failed/unknown as appropriate, direct endpoints remain usable, no synthetic ACK or infinite route search. |
| Radio busy/error | Definitely rejected submission, missing completion, possible RF emission and failed receive rearm all terminate or contain within bounds. No silent retry and no test-double auto-delivery. |
| Combined profile only | Simultaneous local and relay bursts with different frame sizes, idle-to-busy credit, both queues saturated and relay disablement. Demonstrate the accepted airtime shares/service-gap bound under admitted load and independent local operation. |

For later physical acceptance, use three independently identified radios with
relay-enabled and relay-disabled controls. Record aggregate generated/admitted/
dropped/expired/submitted/completed/uncertain/receiver-admitted counts, duplicate
and replay refusals, queue high-water marks, airtime, latency and power. Keep
private identities, payloads, keys, packet contents and precise locations out of
ordinary evidence. A host power-cut model does not establish physical durability.

## Review choices and successor boundaries

Review now: FIFO with newest refusal, finite per-source plus aggregate admission,
one relay submission, no priority bypass, and the explicit possibility of losing
a congested or saved-but-unsent frame. Recommend these for the dedicated-first
profile. The later combined profile should use separately reserved queues and
airtime fairness; its exact weights/borrowing policy still need review.

Still unresolved for implementation: exact eligible protected frame class and
relay-capable topology/format binding, source authentication and any outer
envelope, protected epoch/retention/replay recovery, region/PHY, received target,
and every numeric memory/time/radio/power limit. These are named conditions,
not missing inputs preventing completion of this planning report.

The [registered product plan](../../tasks/OPTIONAL_PRODUCTS_PLAN.md) and
[backlog](../../tasks/BACKLOG.md) retain sequencing: OT-0266a supplies the
candidate hardware/deployment profile; OT-0266b specifies provisioning,
diagnostics and durable recovery and registers bounded implementation children
only after scope/target decisions; OT-0267a prepares interoperability/fault
acceptance; OT-0267b prepares deployment/release guidance. None is a claim that
firmware, physical tests or product release are already complete.

This task changes only the planning report. Local link/document checks and
independent review are recorded in the batch evidence. No code, protocol,
hardware, keys, new task IDs, V1 completion credit, publication or public website
capability status changed.
