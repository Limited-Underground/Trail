# Repeater host lifecycle composition v0

Owner-approved P2 host contract, implemented under OT-0266b on October 28, 2026.
This is a C++ host API/lifetime contract, **not a wire protocol**, protected
enrollment implementation, target selection or authorization to activate radio.
The [decision packet](../testing/OT-0266b-REPEATER-PROVISIONING-RECOVERY-2026-10-01.md#p2-lifecycle-supervision-contract-candidate)
retains the accepted scope and L1-L12 requirements. D1-D7 are unchanged.

The original P2 sections below describe the nonbridge mode. The additive P3-A
section specifies the separately authorized fixed bridge mode and its claim API.

## Owners and trust boundary

`RepeaterLifecycleController` owns one in-place optional bundle of the existing
duplicate window, context-bound checkpoint store, immutable forwarder and
save-before-release coordinator. Neither controller nor bundle can be copied
or moved: their internal references must remain stable. All provider/storage
objects outlive the controller, and a retired bundle is never reused.

The new interfaces are synthetic host seams. `RepeaterLifecycleProvider`
consumes one-use request and consent handles, revalidates their original
bindings, and binds metadata to exact bytes. A provider returning true promises
coherent instance/lifecycle/role/context/epoch/readiness generations and original
deadlines. This is not cryptographic evidence; no parsed caller Boolean grants
production authentication. Real trusted producers remain P4/P5 work.

`RepeaterLifecycleRoleStorage` models ordinary role read/write/readback and
uncertainty. Its C++ record is not a selected persistent schema or protected
namespace. Replay uses the unchanged existing `DuplicateCheckpointStorage` and
`ODS0` owner; no reset/erase/import path is added. CRC remains accidental-damage
detection. P2 creates no keys, decrypts no protected payload and touches no target.

All calls are externally serialized. Providers and storage are finite,
nonthrowing and return explicit error/unavailability outcomes. This requirement
also applies to the existing replay-storage interface, whose virtual methods
are not declared `noexcept`; throwing from such an adapter violates this host
contract and is not an exception-recovery capability. Reentrant mutators return
`reentrant` before state, clock, provider or owner mutation. Status is a bounded
read; P2 creates no thread, event queue, polling loop or callback executor.

## State and public operations

| Operation / state | Behavior |
| --- | --- |
| Construction | Disabled, no storage IO or default initialization. Invalid configuration or exhausted lifecycle generation is contained. |
| `recover(request, now)` | Allowed only from Disabled/Suspended/Contained with a fresh provider-consumed request. Validate exact enabled role/context, namespace, membership and synthetic readiness. Authorized first use commits and exactly reads back the role before replay initialization; retained disabled-to-enabled transition increments the role generation without clearing replay. |
| Recovery completion | Recover/repair the actual existing checkpoint through a fresh bundle. Revalidate the same consumed request and original snapshot before and after replay boot. Only then enter AwaitingConsent. Missing retained media is never treated as first use. |
| `consent(handle, now)` | Only AwaitingConsent can consume consent. Exact instance/lifecycle/role/readiness bindings and a finite deadline no later than the original authority deadline create an Active lease. Consumed consent cannot extend or replace that lease. |
| `admit(handle, bytes, now)` | Only Active with current original authority/lease. Check bounded shape, copy into fixed private scratch, verify metadata against that copy/length, recheck authority and invoke the actual coordinator. Eligible queue/rate denials still persist replay. Persistence failure or authority loss contains and retires the bundle. |
| `take_candidate(now)` | Only Active with current original bindings. The unchanged queue applies original age/expiry and returns at most one exact frame by value. This is a host observation, not RF submission, endpoint admission or reusable dispatch authority. |
| `notify(generation, event, now)` | Wrong-generation events refuse before clock mutation. Suspension/radio-unavailable events suspend; authority loss, uncertain-submission and failed-rearm events contain. These radio outcomes are injected observations only; no driver/completion/rearm/debit implementation is included. |
| `disable(request, now)` | Consume an authenticated-host request, validate actual retained role and original transaction freshness, then invalidate the lifetime and retire all queued work before role mutation. Acknowledgement requires exact readback and final original-request/current-snapshot verification. Uncertainty or stale authority yields Contained, never a durable-disable acknowledgement. |
| `shutdown()` | Terminal and idempotent. Invalidates and destroys RAM once, preserves retained role/replay bytes, and writes no storage. Only status and repeated shutdown remain possible; a new separately admitted instance is required. |

Retiring increments a nonwrapping lifecycle generation **before** destroying its
bundle and invalidating its lease. Exhaustion contains instead of wrapping;
terminal shutdown still succeeds. Zero and exhausted retained role generations
are refused without rewriting them. Request handles are consumed before any
possible role write, replay initialization or repair. The first-use opportunity
is spent before a role-write attempt; partial provisioning needs explicit
reconciliation, not a default empty-state retry.

Disable uses `request_current` to revalidate the originally consumed transaction
independently of the desired role's not-yet-written value. It checks before
mutation and after verified commit. Its final `current` check observes the
disabled record without substituting a new scope or deadline. If an update
already committed but authority became stale, the role may be disabled while
the operation truthfully refuses acknowledgement. No automatic second write.

Whole-queue retirement applies to suspension, membership/context change, expired
authority, fault and shutdown. Existing owners have no selective-source removal
API. Extra unsent loss is explicit. Saved-but-unsent frames are not reconstructed,
while their retained replay observations remain spent, preserving P1. Resume
requires a new explicit recovery request, exact recovered state and fresh consent;
clearing a fault or advancing time alone never resumes.

## Time and frame binding

The caller supplies one nondecreasing logical clock. Original request,
authority and consent deadlines require `now < deadline`; equality refuses.
Rollback contains. No reread, new snapshot, suspend or reconstruct operation
extends an old grant. Snapshot resampling verifies the original binding, never
silently adopts newer role/readiness generations or a later deadline.

The admitted input is copied into at most 255 private bytes before provider
verification. A frame handle for other bytes or length is refused before replay
mutation. Callback mutation of the caller's original buffer cannot alter the
verified copy. The existing forwarder copies those same opaque bytes into its
queue. Scratch is cleared on return/retirement; this is ordinary RAM clearing,
not a secure-erasure guarantee. No payload/identity is exposed in status.

Queue age is still the existing owner's enqueue time and drops at age >= T.
Expiry is applied when taking a candidate; P2 does not introduce a background
scheduler or P3's radio-service/airtime policy. P3 must separately review and
atomically claim authority and original age before any real submission; it cannot
treat a copied P2 observation as a transmission grant.

## Bounded work and acceptance

Configuration requires nonzero identity/context/epoch/retention/rate/window/age,
exactly one locally authorized repeater, active queue depth 1..8 and frame limit
1..255. These are host/storage ceilings, not chosen production resource values.
The fixed optional bundle and scratch allocate no heap storage. No per-source
map, backlog, retry, transport or dynamic route selection exists here.

Healthy replay boot uses two slot reads/no write; authorized initialization or
known single-slot repair uses five reads/one write. An eligible observation uses
three reads/one write; duplicate/refusal and queue retirement/shutdown add no
replay IO. Recovery makes at most three original-request checks and two snapshot
checks; admission makes at most three snapshot checks and one frame verification.
Role mutation performs one write and exact readback after its initial role read.
Tests instrument these bounds rather than inventing a target timing claim.

L1-L12 in the decision packet cover disabled startup, coherent recovery, one-use
consent, exact-byte admission, queue retirement, fresh resume, role uncertainty,
separate grant/queue expiry boundaries, injected radio faults, shutdown, bounded
lifetime/reentrancy and regression/privacy. Existing P1 and 106-scenario evidence
remain separate from new lifecycle results. Host success establishes no physical
power cut, real protected storage, source authentication, airtime durability,
supported hardware, RF interoperability or production V1 relay capability.


## P3-A bounded dispatch bridge

Owner authorization on October 28, 2026 accepts the nine-file P3-A prerequisite
in the decision packet. This extends the host API only. P3-B dispatch, airtime
ledger, terminal/rearm implementation and real radio adapters remain absent.
The original P2 nonbridge observation path remains supported unchanged.

The forwarder remains the sole fixed eight-slot FIFO. Each admitted slot retains
verified source/context/epoch/message metadata, its original enqueue time and
original maximum age alongside the same opaque bytes. Metadata comes from the
trusted verification seam, never payload parsing. Queue-to-pending transfer does
not renew age. `prune_expired(now)` visits at most eight queue heads and never pops a
live head. Reconstruction restores replay observations, not volatile frames;
there is no saved-payload replay or age renewal after restart.

An optional bounded admission gate runs only for a new eligible replay key after
the existing shape, authentication, authorization, context, permission and time
checks. Denial consumes the replay observation and is saved/readback-verified by
the unchanged coordinator. A successful debit followed by ordinary queue/rate
refusal is conservatively lost, never refunded by this layer. Uncertain gate
state also reports the new replay observation and prevents older queued release;
P2 contains after the persistence boundary. No source quota/airtime ledger is
implemented here: host fake gates model the admitted accounting seam.

Bridge mode is fixed at construction. Legacy extraction is refused in that mode.
P2 owns at most one pending frame; consumers call `prepare_dispatch(now)` and receive a metadata ticket without a
payload copy or second FIFO. While pending, eligible new admission is refused
through the replay-persisting gate. Queue plus pending stays within configured
N (1..8). The ticket binds the original instance, lifecycle, role, readiness,
context, epoch, consent deadline, source/message and enqueue/age evidence.
Candidate identities are nonzero and nonwrapping; exhaustion contains.

`claim_dispatch(ticket, now, callback)` revalidates original authority, lease and
queue age, consumes the
one-use ticket before one synchronous finite nonthrowing callback and lends P2's
owned bytes only for that callback. A future adapter must copy bytes into its
fixed declared storage before returning; retaining the borrowed pointer violates
this contract. Reentrant mutation refuses before owner changes, preserving buffer
lifetime. Stale, altered, duplicate or retired tickets cannot invoke a callback.
Before-claim revocation means zero callbacks; authority loss during/after the
callback may follow one claimed emission and prevents later claims. A callback
is not proof of RF completion, receive rearm or endpoint acknowledgement.

Expiry-only service checks original local grant/lease deadlines without provider
polling and retires all volatile work at equality. External authority changes
are detected by guarded admission/prepare/claim or explicit notification; there
is no background observer.

Suspension, restart, authority loss, expiry, uncertainty and shutdown retire
pending and queued bytes without resetting retained replay protection. Uncertain
submission never restores a consumed ticket. Shutdown remains terminal. Recovery
requires explicit authority and fresh consent. P3-B must preserve uncertain
radio ownership across reconstruction and verify explicit terminal/rearm proof;
P3-A introduces no default-ready port, automatic recovery or transmission loop.

All storage/providers/gates/callbacks must outlive their reference-bearing owners
and remain externally serialized, bounded and nonthrowing. Fixed queue, scratch
and pending buffers allocate no steady-state heap. Raw forwarder admission also
uses a bounded 255-byte local frame snapshot plus verified-metadata scratch;
this stack cost is additional to `sizeof(RepeaterLifecycleController)`. No new key access, Packet-v1
field, protected storage schema, route, priority policy or production target is
introduced. These host seams do not qualify real authentication, storage,
regulatory airtime, physical power loss or radio behavior.
