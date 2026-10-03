# OT-0266b Repeater provisioning, diagnostics and durable recovery

Prepared 2026-10-01 under approved revision 1. Requirements and lifecycle
planning only; no implementation, device access, target selection or release.

## Owner summary and completion boundary

Use deliberate opt-in to authorize the accepted dedicated-first evaluation:
one repeater between two endpoints. Start forwarding disabled, verify the
current role, membership, protected context and recovered replay state, and
release an eligible frame only after its replay observation is durably saved
and read back. Disabling or removing the relay must preserve independent direct
endpoint operation. Troubleshooting should expose bounded state and counts,
with private packet contents and identities excluded.

The owner still needs to select the deployment/target profile and the role
control and restart-consent behavior. Engineering must then freeze the
relay-capable security construction, durable-state binding and measured limits.
Those choices determine the exact implementation-through-release successor
chain. The proposals below are **unregistered and unapproved**; they allocate no
task IDs. **OT-0266b is not complete and remains In Progress** because its
[acceptance](../../tasks/OPTIONAL_PRODUCTS_PLAN.md#ot-0266b-specify-provisioning-diagnostics-and-durable-recovery)
requires that chain to be registered and frozen after the prerequisite choices.
This report supplies the reviewable requirements portion of that task.

## Accepted boundary and reuse

The [deployment profile](OT-0266a-REPEATER-DEPLOYMENT-PROFILE-2026-09-29.md)
accepts dedicated-first planning and the prior
[role](OT-0264b-REPEATER-ROLE-PROPOSAL-2026-09-29.md),
[security](OT-0265a-REPEATER-SECURITY-REVIEW-2026-09-29.md) and
[scheduling](OT-0265b-REPEATER-SCHEDULING-2026-09-29.md) recommendations. This
report maps provisioning and recovery onto those boundaries; it does not repeat
their deployment inventory or choose their numerical budgets.

[Future Concepts](../FUTURE_CONCEPTS.md#optional-clientrepeater-mode-on-user-devices)
preserves the later combined client/repeater direction. That requires separate
client resources, fair scheduling and concurrent-traffic evidence. Dedicated
evidence cannot establish it. [Decision 0033](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md)
keeps base V1 independent of a repeater, superseding the initial-release scope
wording in [Decision 0004](../decisions/0004-immutable-first-release-forwarding.md).
[Decision 0035](../decisions/0035-host-tested-secure-lora-key-transport-contract.md)
and OTSL0/v0 remain direct-only; no reserved bit, parser Boolean or provisioning
option can confer relay authority on that construction.

The existing [forwarding contract](../protocol/SINGLE_REPEATER_FORWARDING_V0.md)
and [replay coordinator contract](../protocol/SINGLE_REPEATER_REPLAY_COORDINATOR_V0.md)
are algorithm-neutral host evidence. They supply useful ordering and refusal
rules, not an authenticated packet producer, live role-control path, protected
target store, radio scheduler or supported firmware target. CRC detects
accidental record damage; it proves neither record authentication nor resistance
to rollback. A finite duplicate cache is not a substitute for endpoint replay
protection. This report claims no new host test, build or physical evidence.

## Provisioning and revocation requirements

The following target-composition requirements preserve the accepted policies.
Their concrete control protocol, schemas and storage mapping remain undecided.

1. **Explicit authority.** Enabling relay operation requires a deliberate,
   authenticated provisioning action bound to the intended device, current group
   context, epoch, membership and exactly one authorized repeater. Names, aliases,
   common group-key possession and untrusted radio metadata do not authenticate
   an individual sender. A canceled, stale, unauthorized or ambiguous action
   leaves forwarding disabled. Provisioning never grants forwarding permission
   to a client role or an unsupported protected frame class.
2. **Commit before readiness.** Validate the complete intended role/context and
   its storage binding, persist the required state, verify exact readback and
   recover the replay checkpoint before showing Ready. Initial empty replay
   state is allowed only for explicitly authorized first provisioning. Missing
   or damaged previously provisioned state is a service fault, not permission to
   create a fresh identity, empty replay history or default-enabled role.
3. **Restart consent.** Every boot begins with forwarding disabled. Only verified
   current state and the selected resume policy may authorize readiness. Whether
   valid retained opt-in resumes automatically or requires renewed local consent
   is an owner choice; this document selects neither. Neither option may resume
   a disabled, removed or retired role, reset spent airtime, or transmit a lost
   volatile queue after reboot.
4. **Disable and membership removal.** Serialize disable/revocation with dispatch.
   Stop new admission, retire affected queued work and invalidate outstanding
   authority/callbacks before acknowledging the transition. Removing a source
   prevents its later admission and retires its pending frames; removing the
   local relay role retires all forwarding work. Epoch/context changes cannot
   retain work authorized under the prior state. Bytes already emitted cannot
   be recalled; report that boundary separately from successful disablement.
5. **Uncertain removal is contained.** Do not acknowledge durable removal until
   its persistence is verified. If its save or readback is uncertain, keep
   forwarding disabled and require recovery to establish which state is current.
   Temporary disable, diagnostic clearing and membership removal must not erase
   replay history or endpoint replay protection merely to make recovery easier.
   Reprovisioning into a new context needs separately authenticated authority
   and a reviewed state transition, not an automatic overwrite of mismatched
   media. The authority and distribution of membership/epoch changes remain a
   security-construction gate.

## Resource ownership

| Resource | Existing owner or contract | Required target binding and release rule |
| --- | --- | --- |
| Role, membership and protected metadata | Future authenticated adapter supplies `VerifiedForwardingMetadata`; the host policy cannot generate cryptographic evidence. | One authority owner verifies the complete protected object and current role/context/epoch. Dispatch rechecks that authority; disable/revocation cannot race an unclaimed transmission. No live binding exists here. |
| Exact queued bytes and original age | [SingleRepeaterForwarder](../../firmware/components/delivery/include/opentrail/single_repeater_forwarder.hpp) owns a finite volatile FIFO and copies exact bytes. | Bound frame buffers, authentication work, per-source/global admission and aggregate airtime. Dropped/expired/retired work releases only its own resources; no deadline renewal, mutation of protected bytes or priority bypass. Existing host queue/rate values are not selected target limits. |
| Replay checkpoint | [SingleRepeaterReplayCoordinator](../../firmware/components/delivery/include/opentrail/single_repeater_replay_coordinator.hpp) gates `next_transmit`; [DuplicateCheckpointStore](../../firmware/components/delivery/include/opentrail/duplicate_checkpoint_store.hpp) alternates two slots and checks readback. | Bind the protected target namespace and selected durable role/epoch/replay/airtime recovery coherently. The host record contains context/epoch/replay data, not a complete role, roster or airtime transaction. Its reset API is not routine recovery authority. |
| Radio and completion | Scheduling contract requires submission, possible emission, terminal outcome and receive rearm to remain distinct. | One radio owner claims an attempt/reservation and releases that exact ownership at a verified terminal boundary. Unknown emission/completion or failed rearm prevents another submission; late callbacks cannot reactivate retired work. |
| Diagnostics and endpoint acceptance | Host status has queue/drop counts and coordinator state; endpoint authentication remains independent. | Diagnostic storage/work/timers have explicit finite bounds and no authority over role or retry. Only the receiving endpoint can establish its protected acceptance/ACK; relay queueing, persistence or RF completion cannot manufacture it. |

## Lifecycle, refusal and recovery matrix

Disabled means no forwarding admission or submission. Contained means a fault
prevents forwarding until explicit recovery. These are specification categories,
not a selected UI or wire-state encoding. Resource ownership above applies to
every row; an absent proof keeps the transition refused.

| Before / trigger | Required evidence and permitted transition | Refusal, containment and recovery |
| --- | --- | --- |
| Unprovisioned / enable request | Fresh authenticated opt-in, exact intended context/epoch, current membership, supported construction, one relay and reviewed limits. Begin bounded provisioning while forwarding stays disabled. | Cancel, wrong device/context, stale request, zero/multiple relays or unsupported frame class: remain disabled, without installing defaults. |
| Provisioning / save and verification | Exact durable readback of selected role state and explicitly authorized initial replay checkpoint; all readiness gates pass before Ready. | Interrupted or uncertain commit: no Ready or forwarding. Reconcile actual durable state on recovery; do not blindly repeat an enable mutation. |
| Cold boot / retained state | Validate resume authority, role/membership, namespace/context/epoch, replay retention and selected airtime recovery before admission. | Stale/removed role, mismatch, missing previously provisioned state or unknown resume authority: disabled/service required. No fresh empty checkpoint as repair. |
| Boot / two-slot replay restore | Readable slots, unambiguous newest valid record, exact context/epoch and valid replay entries. Known recoverable single-slot degradation needs verified repair before operation. | Unreadable/invalid-only media, conflicting generations, legacy unbound record, mismatch or failed repair: service required; do not overwrite mismatched state. Exhausted generation cannot authorize a new replay-state release. The host coordinator boot is one-shot; resolve the condition before constructing a fresh recovery instance. |
| Ready / protected frame | Adapter verifies source authentication, current authorization/context/epoch, immutable forwarding permission and exact protected bytes before policy replay observation. | Bad/malformed/unauthorized/wrong-context input must not poison another source's replay state. Self-source, destination reached, duplicate or denied forwarding permission produces no queue release. |
| Eligible new observation / admission | Observe replay key and save/read back the changed replay window before any frame release. The same persistence order applies when queue or rate pressure denies the new frame. | Uncertain save contains the coordinator and blocks all queued release. Congestion may consume this opportunity permanently; later copies do not turn it into a retry. Charge these denied-traffic writes to wear/latency/energy budgets. |
| Verified queued frame / dispatch | FIFO frame still within original age, current authority/epoch and available airtime; atomically claim one bounded radio attempt. | At or after expiry, clock regression/uncertain age, disable, revocation or unavailable budget: retire/refuse work. Waiting, persistence, backoff or reboot cannot refresh its age or credit. |
| Submission / radio outcome | Record definitely rejected, possible emission, verified RF completion and receive rearm separately; close the exact attempt ownership. | No automatic second submission, synthetic completion or auto-rearm. Possible emission with missing terminal evidence is uncertain; failed rearm contains radio use. Disable may stop future work but cannot recall emitted bytes. |
| RF completion / endpoint outcome | Endpoint independently verifies protected content, membership/epoch/replay and the expected protected ACK where required. | Relay status is not delivery. Missing endpoint evidence stays pending/failed/unknown under the endpoint's finite contract; loss of relay does not create an ACK or endless route search. |
| Any operational state / local disable or source/role removal | Stop admission, serialize with dispatch, retire affected buffers/authority, verify required durable transition and show truthful disabled/removal status. | Persistence uncertainty stays contained; preserve replay protection. Stale queued work and completion callbacks cannot regain authority after the transition. |
| Any state / power loss or restart | Recover verified durable security state; volatile unsent frames are lost honestly. Saved replay keys continue suppressing repeats; spent airtime is not silently reset. | Before save: no released frame. During uncertain save: contain/reconcile. After verified save before RF: possible lost opportunity. After possible emission: no replay-assisted second forward. No durable outbox is selected. |
| Contained / service request | Explicit authorized recovery identifies the fault, reconciles exact bound state and proves all readiness gates through a fresh bounded instance. | No reset-to-empty, legacy import, arbitrary slot deletion, silent downgrade or retry loop. Replacement/reprovisioning/erasure requires its own reviewed authority and recovery procedure. |
| Disabled / removal from deployment | Verify forwarding stopped, preserve endpoint independence and perform the separately authorized retirement/restoration procedure on the exact assembly. | Do not claim physical key erasure or original-firmware restoration from a role flag. Recovery access and private original-state custody follow the deployment profile, with actual readback/owner observation when later authorized. |

## Private, bounded diagnostics

Expose only what can be measured locally: disabled/recovering/ready/busy/contained
state, a fixed refusal/fault category, queue depth/high-water mark and aggregate
admission, authentication/authorization/context rejection, duplicate, congestion,
expiry and persistence-failure counts. Submission/completion/uncertain counts
need the real radio owner; receiver-admitted counts need separate endpoint
evidence. Preserve those distinctions in operator wording.

The eventual diagnostic contract must set storage size, event count, collection
duration, work/rate budget and retention/clearing behavior. Use bounded counters
with an explicit saturation/overflow indication, fixed categories and a bounded
first-fault record; never print raw exceptions or unlimited event streams.
Existing host status counters do not by themselves implement this diagnostic
contract. Floods and repeated storage faults must not cause unbounded logging or
consume the forwarding/control budget.

Ordinary diagnostics must exclude keys, raw identity/aliases, group material,
packet contents, precise locations and per-member histories. Local access and
any optional export need the selected authorization/privacy policy; no remote
export, payload capture or public logging is assumed. Clearing diagnostics must
not clear role/epoch/replay state. Diagnostic failure or an expired collection
session cannot grant relay authority, extend a frame deadline or trigger retry.
These requirements do not establish a selected screen, button, BLE/USB control
path, retention duration or universal memory-erasure guarantee.

## Required future discriminating checks

These are successor acceptance cases, **not executed tests**. Reuse historical
host evidence only at its exact boundary; exercise the eventual real authority,
storage and radio owners with fault injection rather than a double that delivers
or rearms automatically.

| Case | Observable assertion |
| --- | --- |
| Opt-in and revoke authority | Cancel/stale/wrong-device request, ambiguous relay configuration and unauthenticated control never enable. Valid removal excludes the source; local role removal excludes all forwarding. |
| Role save interruption | Cut before write, during an uncertain write, and after durable write before acknowledgement. Ready is never claimed without verification; recovery reconciles actual state without replaying a possibly applied action. |
| Replay save interruption | Cut before/during save, after save before RF and after possible emission. No pre-save release or duplicate forwarding after recovery; saved-but-unsent loss is explicit. |
| Stale/damaged media | Empty authorized first boot differs from missing retained state. Wrong context/epoch, legacy record, unreadable/invalid slots, conflicting/exhausted generations and failed repair stay disabled without overwrite. |
| Disable dispatch race | Disable/remove/rekey before claim, during radio ownership and before late completion. No new old-authority submission; emitted-byte uncertainty and exact reservation release remain truthful. |
| Overload and time | Queue/source/global/airtime/diagnostic limits at and beyond boundary; original expiry exactly at boundary, rollback and uncertain restart age. Bounded work/memory and no renewed deadline, credit or retry opportunity. |
| Authentication preservation | Unsupported OTSL0 relay use, altered protected bytes, forged sender metadata, replay beyond duplicate-cache lifetime and synthetic relay ACK are refused. Receiver remains an independent authenticator. |
| Radio/relay failure | Busy/definite rejection, possible emission without completion, failed rearm, disable/power loss. No hidden retransmit; direct endpoints remain independently usable. |
| Privacy and service | Fault/flood/diagnostic export contains only approved categories/counts, not secrets or packet data. Clear diagnostics leaves security state intact; recovery cannot erase replay protection to pass. |

## Decisions needed before successor registration

| Decision gate | Exact unresolved choice / evidence owner |
| --- | --- |
| Deployment and target | Owner: intended region, accessible installation/service conditions, power/duration needs and candidate assembly, as listed in OT-0266a. Engineering: received assembly/pin/radio/storage/recovery evidence. No supported target is selected. |
| Role control and consent | Owner reviews deliberate enable/disable/removal UX, access authority, diagnostics access and retained-opt-in resume policy. Engineering freezes the authenticated control binding, freshness, acknowledgment and uncertain-outcome recovery. |
| Protected forwarding construction | Engineering review under existing security gates: eligible frame class/version, source authentication distinct from group access, immutable permission, topology, membership/epoch change authority and any envelope. Direct-only OTSL0 is insufficient; no algorithm or new wire ID is chosen here. |
| Durable-state binding | Engineering: protected namespace, coherent role/context/epoch/replay/airtime transition and recovery rules, retention/time basis, legacy handling, physical-access threat boundary and any required rollback resistance. Host CRC/two slots do not settle these choices. |
| Bounds and diagnostic contract | Reviewed exact security/PHY/region/target budget: finite queue/source/global/airtime/age/control/recovery/diagnostic limits, saturation and privacy policy, plus measured RAM/latency/wear/energy. No numerical defaults are promoted from host fixtures. |
| Validation and release scope | Freeze exact candidate artifacts/assembly, negative controls, original-state custody, recovery and support claims. Combined client/repeater acceptance requires separately selected resources/fairness and simultaneous traffic; dedicated-first does not cover it. |

## Unregistered successor scopes

These describe the missing work concretely without allocating references,
freezing a scope, changing dependencies or granting approval. Split further at
registration if the selected target/contract would exceed a bounded increment.

| Proposed scope | Prerequisites | Acceptance to freeze after decisions |
| --- | --- | --- |
| Implement provisioning and durable role/replay recovery | Accepted versioned security/control/state contract and target storage profile. | Real authenticated producer/control and protected store composition; fail-closed opt-in/removal/startup, exact readback, coherent recovery and no reset of replay protection; all matrix fault boundaries have deterministic host assertions. |
| Bind radio ownership and private diagnostics | Accepted protected frame, target radio/PHY, budgets and diagnostic contract; preceding authority/recovery implementation. | Immutable bytes, finite source/global/airtime/age limits, serialized revocation/dispatch, one attempt, explicit terminal/rearm ownership, bounded privacy-safe diagnostics; no synthetic endpoint acceptance. |
| Reproducible affected builds and host integration | Approved implementations, exact target/configuration and source closure. | Every affected target builds reproducibly with pinned artifacts/toolchain; actual composition passes complete lifecycle/fault/overload/privacy cases and independent review. Builds establish no RF or power durability claim. |
| Physical interoperability, interruption and restoration | Host/build gates; OT-0267a procedure; exact three-radio setup and separate fresh physical authorization. | Enabled path and disabled negative control, independent endpoint authentication/replay, packet/loss/duplicate/latency/airtime/power accounting, staged persistence/power cuts, restart/removal and verified original restoration. Combined profile adds concurrent client traffic. |
| Operator/support and release acceptance | Physical acceptance for each claimed profile; OT-0267b guidance; accepted support scope. | Claims trace to measured artifacts/targets/configurations; usable opt-in/disable/service/privacy procedure and failure recovery. Signing, publication and deployment each require separate explicit approval. |

OT-0267a and OT-0267b retain their registered acceptance dependencies; this
partial OT-0266b report does not authorize starting them. The full OT-0266b task
remains open until prerequisite decisions permit exact successor registration
and freezing. No implementation, hardware, keys, purchasing, task IDs, V1
completion credit, publication or public website capability status changed.
