# OT-0266b Repeater provisioning, diagnostics and durable recovery

Prepared 2026-10-01 under approved revision 1. Requirements and lifecycle
planning only; no implementation, device access, target selection or release.

Decision preparation updated 2026-10-27 under the owner's three-file,
documentation-only instruction. Recommendations and successor scopes were
submitted for owner review at that checkpoint.
The live [OT-0266b item](https://github.com/orgs/Limited-Underground/projects/1/views/4?pane=issue&itemId=271124158)
was checked October 27: **Todo / Needs Decision**, scope revision 2 Pending,
no approved revision in that migrated snapshot. Subsequent explicit owner
authorization conditionally approves D1-D7 and permits **P1 host tests only**
within its existing allowlist, without GitHub registration or status changes.
It grants no P2-P6 execution, deployable target, mesh, physical or publication
authority. The Project's unchanged fields are not a denial of that local grant.
OT-0266a is Done for its planning deliverable; no supported target was selected.

## Owner summary and completion boundary

Use deliberate opt-in to authorize the accepted dedicated-first evaluation:
one repeater between two endpoints. Start forwarding disabled, verify the
current role, membership, protected context and recovered replay state, and
release an eligible frame only after its replay observation is durably saved
and read back. Disabling or removing the relay must preserve independent direct
endpoint operation. Troubleshooting should expose bounded state and counts,
with private packet contents and identities excluded.

The owner conditionally accepted the recommended D1-D7 direction, including
fresh authenticated restart consent. Exact deployment/target inputs and actual
control/security/storage bindings remain unresolved. Engineering must freeze the
relay-capable security construction, durable-state binding and measured limits.
Those choices determine the exact implementation-through-release successor
chain. The proposals below allocate no task IDs. P1 has explicit local host-test
authority; P2-P6 remain **unregistered and unapproved for execution**.
**OT-0266b is not complete** because its
[acceptance](../../tasks/OPTIONAL_PRODUCTS_PLAN.md#ot-0266b-specify-provisioning-diagnostics-and-durable-recovery)
requires that chain to be registered and frozen after the prerequisite choices.
This report supplies the reviewable requirements and decision packet. The
October 1 In Progress wording is historical; the live Project owns status.
The bounded planning delivery does not satisfy the later registration gate.

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
protection. Decision preparation reused the verified host baseline below. The
separately authorized P1 closeout records new host-test evidence only.

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

## Reused host baseline and preservation

The existing isolated Repeater worktree remains detached at Trail revision
`c96d93ea459a7c41cf453e27edea082985650fa6`. It was tracked-clean at this
planning task's start. Its existing ignored `build/repeater-baseline` contains
the runner, source manifest, compiler identity, executables and receipts. No
baseline copy or new worktree was made. Local evidence is not shipped by Git.

The October 27 baseline passed 14 executables / 104 reported scenarios or
groups, including nine immutable-forwarder and nine reboot-safe replay groups.
Before reuse, all 252 manifest input hashes and all 22 verification-receipt
artifact hashes matched. No runtime suite was rerun for this documentation task.

| Existing receipt, relative to worktree root | SHA-256 |
| --- | --- |
| `build/repeater-baseline/source-hashes.json` | `f145d2f3074302628f3ccb4797798f7a6facbc45acc8d76bf586bb9080146b63` |
| `build/repeater-baseline/results.json` | `59cd0e669456caa3fa387537647c8faa44f7e56c9a904879fd8f012030848f2d` |
| `build/repeater-baseline/verification.json` | `247c00284ac8b783919d764bfe6106b48af80b51542d9185f85573093f180741` |

This is a focused host subset using synthetic authority and storage, not
production cryptography, target persistence or a protected three-radio path.
The generic emergency-priority suite does not add priority to the immutable
repeater FIFO. Historical TTL/multi-node tests remain simulation evidence.

## Owner decision requests

The D labels identify choices within this document, not registered tasks.
**D1-D7 recommendations are conditionally approved** by the subsequent owner P1
authorization. Alternatives below retain the original review choices; none is
silently selected instead. No repeat approval of that direction is requested.
Exact region, installation/duration/assembly and engineering/physical gates in
the last column remain unresolved. Conditional acceptance is not target,
production-security, mesh or physical acceptance.

| Choice | Recommended choice for initial evaluation | Alternative / exact owner input | Gate remaining after a planning decision |
| --- | --- | --- | --- |
| D1 Power and installation | Accessible, supervised, dedicated, externally powered evaluation first. Treat regulated vehicle power as a candidate external supply; stop on ignition/supply loss, with fresh consent on restart. Portable battery is a separately measured later profile. No unattended, moving-vehicle, solar or duration claim. | Choose external/vehicle first, portable first or comparison only; supply intended region, stationary installation/service access and desired operating duration. Vehicle use also needs an explicit ignition-off policy. Recommend no ignition-off forwarding initially. | Exact assembly, vehicle input protection/regulator, antenna/region, thermal limits, supply transients/brownout and power-duration measurements. No direct vehicle electrical connection is authorized. |
| D2 Provisioning and authorized enrollment | Default disabled; deliberate authenticated local-service action from the authorized operator, bound to exact device, group/context/epoch, current membership and exactly one repeater. Reuse Trail's existing ownership/security owners where applicable; no parallel enrollment protocol or implicit group-key enrollment. Commit/read back before Ready. | Accept this boundary or defer; name who may enable, remove members and access diagnostics. The exact UI/control transport stays a shared-security design gate, not an assumed BLE/USB command. | Freshness, replay and access binding through the actual control producer; enrollment/removal distribution and recovery acknowledgement. Physical possession alone does not confer membership authority. |
| D3 Restart and recovery consent | Every boot starts disabled, verifies retained role/replay/airtime state and then waits for fresh authenticated local consent. Service recovery never resets replay history. This is the conservative initial profile, including ignition cycling. | Accept fresh consent, or choose automatic resume only after valid retained opt-in and all readiness checks verify; disabled/removed/stale roles never resume under either option. | Exact consent producer and readiness binding. Unknown age/airtime state cannot be repaired by consent; remain contained. Automated retained resume needs its own reviewed cases if chosen. |
| D4 Relay-capable authentication | Keep protected payloads opaque and immutable; require independently verified source, current membership/epoch and immutable permission for the exact bytes. Trail's shared protocol/security owner retains sole authority. Reject direct-only OTSL0/v0 as relay input. | Accept this compatibility constraint or defer relay work. The owner is not being asked to choose a cipher, signature, wrapper or new wire ID here. | Shared security review must identify an actually relay-eligible construction and endpoint checks. No Packet-v1 definitions, reserved bits, framing, endpoint keys or pairwise secrets change under this plan. Until that review passes, production relay admission stays disabled. |
| D5 Protected keys and durable state | Keep endpoint private keys on their existing owning endpoints; no copy to the relay to decrypt payloads. Bind relay authority, context/epoch, replay and airtime recovery coherently to the intended device/storage domain, with authenticated integrity and the reviewed rollback policy. Missing retained media is a service fault. | Accept protected-state requirements or defer; explicitly choose the physical-access threat/support boundary through the shared security review. Do not select ordinary CRC/NVS as sufficient protection by default. | Exact target key handling, protected namespace, integrity/rollback mechanism, retention clock, generation exhaustion, migration/retirement and power-cut/wear evidence. CRC/two slots or a host Boolean do not prove protection. |
| D6 Queue, memory and privacy budgets | Dedicated FIFO, refuse newest on full, no emergency priority bypass, no durable frame outbox. Require finite slot/byte/source/global/airtime/age/work/diagnostic limits. Unknown production budgets keep forwarding disabled. Host-only tests use explicit bounded fixture parameters. Local authorized category/count diagnostics only; export off. | Accept bounded-policy direction or defer; choose external/portable profile through D1 before requesting production numbers. Any priority, retry, outbox or combined-role change is a separate decision. | Exact protected-frame/PHY/region/target costs and measured RAM, crypto/store latency, wear and energy. Existing eight slots and 255-byte storage ceiling are implementation ceilings, not chosen operating limits. |
| D7 Radio interruption and recovery | One submission per admitted object; no automatic resubmission after busy/rejection/timeout/possible emission. Distinguish submission, possible RF emission, terminal completion, receive rearm and endpoint admission. Unknown completion or failed rearm contains further submissions. | Accept one-attempt policy or defer; any retry changes amplification/loss analysis and requires separate review. Recovery must be deliberate and cannot requeue the retired object or erase spent airtime. | Actual driver's internal retry behavior, bounded completion/rearm, IRQ/reset and power interruption evidence. A successful transport send is only queue admission. |

Accepting D1-D7 permits preparation/registration of the corresponding bounded
successors only when separately authorized; it does not authorize executing
them. Region, duration, candidate assembly and specific security/target
bindings may remain explicitly unresolved while host-only work proceeds.
No recommendation expands direct-only, two-endpoint production V1. One relay
between two endpoints is a separate future three-radio evaluation; multi-hop,
route discovery/election and combined client/repeater deployment are excluded.

## Measurable successor acceptance

These are pass/fail requirements for future tests, not new results. Each future
suite must report injected boundary, observed send/persist/claim counts and
failure category; missing cases are NOT RUN, never inferred from 104 prior cases.
Host evidence may use synthetic capabilities, but cannot promote them into
authentication or protected-state proof.

Use explicit fixture parameters: active queue N within the existing 1..8 ceiling,
frame ceiling M within 1..255 bytes, original age T, admission quota Q/window W,
per-source quota S, airtime balance A and maximum scheduler work K. Test minimum
and maximum supported settings, invalid/zero/overflow settings and each chosen
boundary at minus one/equal/plus one where representable. These are test inputs,
not production defaults; target numeric selection remains gated.

| Case / owner | Pass condition | Fail condition / recovery |
| --- | --- | --- |
| Initial/returning enrollment, authority owner | Before exact consent, role, context/epoch, replay and airtime readiness: zero frame releases/submissions. Cancel, stale/wrong-device/context, untrusted caller and zero/multiple relays leave disabled. Missing retained store never initializes as first use. | Any premature release, implicit enrollment or overwritten mismatched media fails; contain without automatic repair. |
| Consent and retained state, lifecycle owner | Under recommended D3, every reconstructed instance has zero releases until fresh consent after recovery; removal stays removed. Diagnostic clear cannot change security-state hashes. | Automatic restart forwarding or consent used to reset protection fails. Alternative retained-resume policy needs separately frozen tests. |
| Interrupted save/readback, durable owner | Inject before write, partial/uncertain commit, read failure/mismatch and after durable commit before acknowledgement. Zero release before verified save; uncertain failure contains all queued work. Reconstruct from actual retained bytes, not the intended write. | Any Ready/release based on a requested write or guessed latest generation fails. No blind retry, slot deletion or reset-to-empty recovery. |
| Replay and cache exhaustion, existing coordinator | Queued, queue-full and rate-limited eligible observations remain spent after reconstruction within their retained lifetime. Wrong epoch/legacy/unreadable/ambiguous/exhausted media stays refused. Source replay beyond cache lifetime remains the future authenticated adapter's gate. | Double forwarding or treating finite cache eviction as cryptographic freshness fails. Reuse existing covered tests; add only proven gaps. |
| Opaque bytes and compatibility, shared protocol owner | Released bytes exactly equal input length/hash; zero decrypted payload exposure, TTL edits, resealing or relay-generated ACKs. Unsupported/direct-only classes and malformed/forged evidence are rejected before replay mutation in actual adapter tests. | Any changed protected byte or forwarding granted by parsed metadata alone fails; host synthetic tests cannot close the adapter gate. |
| Queue and work, scheduler owner | Queue count <= N, resident allocation <= declared memory budget including fixed eight-slot storage and scratch/stack, retained bytes <= declared cap; no hot-path unbounded allocation or sender-table growth. Each turn does <= K declared operations; control/disable stays serviceable under flood. | Growing memory/work, priority bypass or a hidden backlog fails. Refuse newest and preserve consumed replay opportunities. |
| Age and time, queue owner | At T-1 frame may release if other gates pass; at T and T+1 zero releases. Queue moves, persistence and wait never refresh original age. Clock rollback or uncertain restart age drops/contains. | Renewed deadline or ambiguous-age transmission fails; no invented timestamp recovery. |
| Disable/remove/rekey races, lifecycle and dispatch owners | Revoke before claim yields zero old-authority submits. Revocation after claim prevents any later claim and retires affected queued work. Stale terminal/rearm callbacks change no new generation's state; ownership/reservation released at most once. | Late callback reactivation, old-epoch drain or double release fails. Possible prior emission remains explicitly uncertain. |
| Submission/rearm failure, radio owner | At most one send per admitted object; inject definite reject, busy, timeout before/after possible emission, duplicate/late terminal callback and rearm failure. No next send without a bound terminal/rearm readiness proof. | Hidden retry, unbounded busy loop, false no-emission claim or automatic readiness fails; explicit recovery excludes the spent object. |
| Airtime restart, budget owner | Reserve full conservative cost before claim; balance never negative. Repeated reconstruction cannot restore spent credit. Missing/uncertain debit/time recovery yields zero dispatches until reviewed recovery proves a safe allowance. | Reboot-created credit or uncharged uncertain emission fails. No numeric quiet period is invented without region/clock evidence. |
| Private diagnostics, diagnostic owner | Fixed categories, bounded saturating counters with overflow indication and finite event/work cap. Secret/identity/payload/location sentinel strings absent; export disabled. Diagnostic failure/clear leaves security state, age and retry authority unchanged. | Sentinel leak, unbounded stream or diagnostic-created authority fails; contain diagnostics independently. |
| Direct independence and future physical path, endpoint owners | Existing two-endpoint behavior remains independent. Later three-radio test includes enabled path, relay-disabled negative control and independent endpoint authentication/replay/ACK evidence with counted loss/duplicates/latency/airtime. | Relay status substituted for endpoint admission or disabled relay breaking direct functionality fails. Host status cannot close RF gates. |

Hardware-dependent acceptance cannot be assigned a pass from host fixtures.
Before a physical task is admitted, P5 must supply the exact candidate and
measured-profile contract: region/PHY/antenna, supply operating and interruption
limits, desired duration H and offered load, memory headroom, maximum storage
latency/write demand, and finite completion/rearm limits. Missing values mean
NOT READY. A later authorized run must meet each frozen limit for the claimed
profile, show zero duplicate endpoint admissions, retain replay/airtime safety
through each authorized interruption, and verify original-state restoration.
Vehicle ignition/supply transitions, battery depletion/recovery, thermal/wear
and actual RF behavior require physical evidence; selecting a planning policy
or computing airtime cannot settle them.

## Proposed successor scope freeze for review

The P labels are local proposal labels, **not new engineering IDs or registered
tasks**. The table and exact lists below freeze this review candidate's scope;
owner acceptance, registration and execution authority remain separate. Any
additional file or materially changed prerequisite returns for scope review.
No target build, physical execution or release file allowlist is fabricated for
an unselected target. Their bounded decision/procedure tasks must first produce
those exact allowlists; this unresolved expansion keeps OT-0266b open.

| Proposal | Depends on | Outcome and stop boundary |
| --- | --- | --- |
| P1 Existing-owner host characterization | Explicit owner authorization permits this local host-only test slice without registration; original baseline inputs/receipts verified. No target, cipher or device choice required. | Add the two discriminating coordinator cases specified below using actual forwarder/coordinator/store code. All focused cases and affected regression suites pass with independent review. A policy discrepancy is reported, not fixed outside the test-only allowlist. |
| P2 Host opt-in and recovery supervision | P1; owner chooses D2/D3; accepted host lifecycle contract and explicit synthetic evidence boundary; separately authorized task. | Compose existing forwarder/replay owners with disabled/consent/service/revocation generations and fake role/airtime storage. Pass applicable lifecycle, interruption and stale-callback criteria. No real keys, parser, storage backend, enrollment service or target linkage. |
| P3 Host dispatch, budget and private diagnostics | P2; owner accepts D6/D7; reviewed parameterized host budget and explicit completion/rearm seam; separately authorized task. | Reuse opaque transport/airtime interfaces; implement only host-testable ownership/admission composition and bounded diagnostics. Pass byte, expiry, quota/airtime, interruption/rearm, revoke and privacy cases. Do not alter shared transport semantics or claim a target driver is ready. |
| P4 Shared protected-adapter and state-binding decision packet | D4/D5; shared Trail security-owner review; P1 findings; may be prepared in parallel with P2/P3 only after its own approval. | Identify accepted relay-eligible construction, actual trusted producers, state/key owner and threat/rollback/retention contract. Produce an exact future adapter/store allowlist, vectors and dependencies for separate approval; if incompatible with unchanged Packet-v1, stop and return to its authoritative owner. No normative/wire edits or production adapter implementation in P4. |
| P5 Candidate target, power and build-scope packet | D1 owner inputs; P4 protection requirements; accepted host P2/P3 interfaces. | Review exact received candidate/region/antenna/power/storage/radio/recovery evidence, identify missing physical facts and freeze a separate target-binding/build task's exact files/artifacts/toolchain and measured-budget gates. No target is selected from a product name or host fixture. This is documentation only. |
| P6 Acceptance and support procedure packets | After full OT-0266b decision/registration gate; existing OT-0267a then OT-0267b dependencies remain. | Freeze exact host/build integration, three-radio/power-cut/restoration and support/release procedures with source/target/artifact custody and separate execution/publication authority. No physical test or release is authorized by a procedure. Actual target implementation, full build, physical execution and release successors remain blocked until P4/P5 provide exact allowlists and the owner approves registration. |

Exact candidate allowlists, relative to Trail root. New paths listed here are
**proposed future files**, not created by this planning update. Shared owners
are read-only dependencies unless explicitly listed; no copied codec, crypto,
enrollment, duplicate-store or radio implementation is allowed.

- **P1:** `tests/host/single_repeater_replay_coordinator_tests.cpp`;
  this OT-0266b report; `docs/PROGRESS_LOG.md`. The existing congestion test
  exercises queue-full persistence, and the existing restart test transmits
  before reconstructing. Add (1) a rate-limited distinct key with queue space
  available: exactly one verified save, then duplicate refusal with no new save
  after reconstruction; (2) verified save followed by reconstruction before any
  `next_transmit`: empty volatile output queue, same key still duplicate and
  zero release. Count release/save/generation outcomes explicitly. These extend
  coverage rather than duplicate the existing queue-full/transmitted cases.
  The executable already appears in `tools/Test-Host.ps1`, so no runner/source
  change is allowed.
- **P2:** proposed `firmware/components/delivery/include/opentrail/repeater_lifecycle_controller.hpp`;
  proposed `firmware/components/delivery/src/repeater_lifecycle_controller.cpp`;
  proposed `tests/host/repeater_lifecycle_controller_tests.cpp`;
  proposed `docs/protocol/REPEATER_HOST_LIFECYCLE_V0.md` (host API contract only,
  never a wire protocol); `tools/Test-Host.ps1`; this report;
  `docs/PROGRESS_LOG.md`. Fake storage/evidence providers stay within the test
  file. Existing source owners are composed without modification.
  Current `SingleRepeaterForwarder` stores bytes/time, not per-source retirement
  ownership, and exposes no clear API. P2 therefore stops admission and retires
  the **entire volatile queue** on disable, membership or context/epoch change,
  invalidating its generation before any new claim. It may reconstruct fresh
  owners only after verified bound durable replay/role recovery and the selected
  consent policy; a new context is not permission to overwrite old media or
  initialize an empty checkpoint. Extra unsent loss is reported honestly.
  Selective source retirement or modification of an existing owner requires
  reviewed scope expansion; do not silently add either to P2.
- **P3:** proposed `firmware/components/delivery/include/opentrail/repeater_dispatch_controller.hpp`;
  proposed `firmware/components/delivery/src/repeater_dispatch_controller.cpp`;
  proposed `tests/host/repeater_dispatch_controller_tests.cpp`;
  proposed `docs/protocol/REPEATER_HOST_DISPATCH_V0.md` (host API contract only);
  `tools/Test-Host.ps1`; this report; `docs/PROGRESS_LOG.md`. Completion/rearm
  events are an explicit host seam, not silently inferred from
  `RadioTransport::send()`/status. Existing `RadioTransport`,
  `calculate_lora_airtime`, packet definitions and P2 interface stay read-only;
  an interface mismatch stops P3 for scope review.
- **P4:** this report; `tasks/OPTIONAL_PRODUCTS_PLAN.md`; `tasks/BACKLOG.md`;
  `docs/PROGRESS_LOG.md`. Existing security decisions, Packet-v1 and protected
  store formats are read-only inputs. Output includes exact proposed future
  adapter/storage paths; none is admitted for implementation by P4.
- **P5:** `docs/testing/OT-0266a-REPEATER-DEPLOYMENT-PROFILE-2026-09-29.md`;
  this report; `tasks/OPTIONAL_PRODUCTS_PLAN.md`; `tasks/BACKLOG.md`;
  `docs/PROGRESS_LOG.md`. No `firmware/targets` or device/configuration edit.
- **P6 / OT-0267a:** proposed `docs/testing/REPEATER-INTEROPERABILITY-FAULT-ACCEPTANCE.md`;
  `tasks/OPTIONAL_PRODUCTS_PLAN.md`; `tasks/BACKLOG.md`; `docs/PROGRESS_LOG.md`.
  **Then OT-0267b:** proposed `docs/testing/REPEATER-DEPLOYMENT-SUPPORT-GATES.md`
  and those same three existing task/history files. These are procedure scopes,
  not physical execution or release allowlists.

P1 has now received separate, local host-only authorization. P2/P3 can later
progress without physical hardware under their own authorization using synthetic authority and
parameterized budgets. Their source must remain unlinked from all production
targets. Product integration requires P4/P5, actual authenticated producers and
protected storage, and the separate affected build/physical/release chain. No
host helper can grant radio eligibility to direct-only V1 traffic.

## Validation, unresolved gates and pickup

The earlier decision-preparation update modified only this report,
`tasks/OPTIONAL_PRODUCTS_PLAN.md` and `tasks/BACKLOG.md` in the existing isolated
worktree. The subsequent P1 grant permits only the coordinator test, this report
and `docs/PROGRESS_LOG.md`, plus task-owned generated validation evidence. The
two pre-existing task-plan/backlog edits remain byte-preserved. Shared protocol
definitions, Trail Live, Android, product versions, original baseline receipts
and other sessions' files remain unchanged.

Decision-packet validation on October 27: `python -B tools/check_repository_docs.py` passed;
`python -B tests/host/repository_docs_tests.py` passed all 18 tests. All 440 local
links/anchors and exact changed-file checks passed, with only the three authorized
files changed and no new untracked files. The baseline's 252 input hashes and
22 artifact hashes remain identical. Independent review found no blocking
issue; its queue-retirement clarification is included in P2 above. The 104
runtime scenarios are reused, not new test results and not acceptance of any
P1-P6 proposal. No full runtime matrix was rerun for this document-only update.

Owner pickup after P1: retain conditional D1-D7 approval and the remaining
region/use/duration/assembly inputs. P2's host lifecycle contract and execution
remain separately gated; do not begin P2 automatically. Shared security/target
owners must resolve P4/P5 before proposing product implementation. Approval to
register any successor is still required; P1 explicitly prohibits GitHub writes. OT-0267a/b
retain their existing dependencies, and full OT-0266b remains open until the
required downstream tasks are actually registered and frozen after decisions.
No V1 completion credit or public website capability changes.

P1 results and its Housekeeping closeout follow below; original baseline and
decision-packet results above remain historical evidence at their stated scope.

## P1 host recovery test closeout

The owner explicitly authorized this bounded test slice and conditionally
approved D1-D7. P1 local implementation and host validation are complete;
OT-0266b remains open and the live Project fields are unchanged. No registration,
commit, push, merge, target implementation or P2 execution occurred.

Exactly three tracked files changed during P1:
`tests/host/single_repeater_replay_coordinator_tests.cpp`, this report and
`docs/PROGRESS_LOG.md`. The existing `tasks/BACKLOG.md` and
`tasks/OPTIONAL_PRODUCTS_PLAN.md` working changes retain their exact pre-P1
hashes. Base revision remains `c96d93ea459a7c41cf453e27edea082985650fa6`;
the candidate also retains the earlier uncommitted decision packet.

| New scenario group | Discriminating assertions and result |
| --- | --- |
| Rate-limited replay persistence | With active depth 2 and quota 1, the first key occupies one slot and the distinct second key is rate-limited with spare queue capacity. Its replay observation is saved/readback-verified exactly once: write/generation counts advance 1 -> 2 -> 3. Only the first frame releases byte-for-byte. All volatile owners are destroyed. At logical time 2000, after the old 1000-ms rate window and within the retained duplicate lifetime, fresh owners restore generation 3 without a write. The denied key remains duplicate with no replay mutation, new save, queue entry or output. PASS. |
| Saved-but-unsent recovery | The first instance verifies generation 2/save count 2 with one queued frame, then every volatile owner is destroyed without any `next_transmit` call. Fresh owners refuse release before boot, restore one replay entry but an empty queue, and produce no frame or expired-drop at logical time 21, before the old frame's 100-ms age limit. Re-presenting the same key stays duplicate, without a write/generation advance or release. PASS. |

The focused coordinator executable passed **11 groups**, comprising all nine
unchanged prior groups and the two additions. The affected baseline selection
then rebuilt and passed **14 executables / 106 scenarios or groups**: all 104
original cases plus the two additions. Existing exact-byte forwarding,
one-repeater admission, queue/rate/expiry bounds, duplicate/reflection handling,
wrong binding/epoch/legacy/unreadable state and failed/uncertain persistence
refusal cases remain passing. Generic priority and historical TTL simulations
retain their original limits; no new SOS or mesh capability is claimed.

Compilation reused the existing source lists, C++17 and
`-Wall -Wextra -Wpedantic -Werror -O2` under MSYS2 UCRT64 GCC 16.1.0.
The existing `run_baseline.py` was evaluated with only its output-directory
assignment redirected in memory. The source runner and `tools/Test-Host.ps1`
were not edited. All original 22 artifact hashes remain unchanged; among the
original 252 manifest inputs, only the allowlisted coordinator test changed.
The new matrix source hashes were captured and rechecked after execution.

Separate independent security and test reviews found no blocking issues.
They confirmed spare-queue discrimination, destroyed/reconstructed owners,
write/generation assertions and checks before expiry that prevent a false pass.
The reviews preserve the important limit: these are host object-reconstruction
tests with synthetic metadata and RAM storage, not a physical reboot/power cut,
source-authentication proof, protected rollback/storage acceptance, durable
frame recovery or persisted radio-rate/airtime-budget evidence. Protected
payloads are opaque fixed fixture bytes; no endpoint keys are accessed.

Generated evidence is retained under the existing task directory
`build/repeater-baseline/p1-20261027`: `before.json` and exact three-file
preimages; `focused/results.json`; `matrix/results.json`, source hashes,
toolchain and executables; `matrix-console.txt`; `runner-recipe.json`;
`p1.patch`; and final `verification.json` with SHA-256 of the patch, sources,
receipts and artifacts. This ignored local evidence is not shipped by Git.
Documentation validation and scope/preservation results are captured in that
receipt; source history and preimages remain intact. No product version, V1
completion credit or public website capability status changes.

Remaining gates: P2 requires its accepted host lifecycle contract and separate
execution authorization; P3 requires P2 and reviewed host budget/completion
interfaces. P4 must establish the shared relay-eligible protected construction
and state/key binding; P5 needs exact region, stationary installation, duration,
assembly, power/ignition, antenna, storage and radio profile. P6/OT-0267a/b retain
their registration, integration/build, physical procedure and support/release
dependencies. Conditional D1-D7 approval selects no deployable target or measured
limit. Physical supply/brownout, endurance/wear, IRQ/rearm, RF interoperability
and original-state restoration evidence remain separately authorized work.

**Housekeeping**

- **Temporary files removed:** none; no cleanup was necessary or destructive
  cleanup performed.
- **Temporary files retained and why:** original `build/repeater-baseline`
  artifacts remain hash-bound historical evidence; its `p1-20261027` child
  retains the focused/full subset run, patch, preimages and receipts for review.
- **Worktree status:** existing detached Repeater worktree retained at the base
  revision; three P1 files plus two byte-preserved earlier planning files remain
  dirty and uncommitted. No worktree created, removed or other session modified.
- **Evidence preserved:** original baseline hashes/artifacts, pre-P1 working
  content, test output, source hashes, patch and independent-review findings.
- **Cleanup requiring owner approval:** none necessary for P1; no historical
  output or worktree has been selected for retirement. Stop here; do not begin P2.

## P2 lifecycle supervision contract candidate

Prepared October 28, 2026 under the owner's instruction to implement only if P2
already has sufficient authority, otherwise prepare its exact scope and stop.
**Readiness: contract candidate prepared; implementation NOT AUTHORIZED.**
Conditional D1-D7 approval and completed P1 satisfy the direction/recovery-test
inputs. They do not accept a concrete P2 lifecycle contract or grant its C++ and
runner implementation. The proposed lifecycle header, source, tests and host
contract file are absent. The live parent was read October 28: Todo / Needs
Decision, with the same planning-only scope; no remote change was made. The
specific missing approval is acceptance of this candidate and execution of the
seven-file allowlist below. No repeat D1-D7 or physical-target decision is needed
to authorize this synthetic host slice.

### Verified inputs and preparation boundary

Base revision remains `c96d93ea459a7c41cf453e27edea082985650fa6`, in the existing
detached Repeater worktree. Before preparation, all three P1 source/document
hashes, both preserved task-plan/backlog hashes, all 28 P1 artifacts, all 252 P1
matrix input hashes and the original baseline's 22 artifact hashes matched.
P1 verification receipt SHA-256 is
`5b4669955dcf39ea511f8cd298ba1d9c1aabe7dc43315ed80fdc2af75cb57bf7`.
Its recorded 11 coordinator groups and 14-executable/106-scenario matrix remain
reusable host evidence. No new lifecycle runtime tests or runtime reruns occur
in this preparation-only task.

Only this report and `docs/PROGRESS_LOG.md` are changed for preparation. Earlier
P1 test source, original reports in the P1 preimages, baseline/P1 receipts and
the other dirty planning files are preserved. The complete P2 host contract
will be materialized at its proposed path only after owner acceptance. This
candidate is an API/lifetime contract, never a wire protocol or new task ID.

### Host boundary and ownership

- One serialized supervisor owns at most one reconstructible bundle of the
  existing `DuplicateWindow`, `DuplicateCheckpointStore`,
  `SingleRepeaterForwarder` and `SingleRepeaterReplayCoordinator`. The storage
  provider outlives the bundle; every referenced provider must outlive the
  supervisor. Loss of availability is an explicit provider result, not deletion
  of a referenced object. Existing owners and P1 tests are read-only.
- A provider bound at construction supplies coherent **synthetic** role,
  membership/context/epoch, storage-binding, consent and airtime-readiness
  observations. Public requests do not turn caller-supplied parser flags into
  evidence. Test handles refer back to that provider's exact current snapshot,
  including instance/generation, role generation and original deadline.
  This models a future trusted producer; it implements no authentication,
  endpoint-key handling, protected target store or production provisioning.
- Frame admission first validates bounded length and copies the input into one
  fixed private scratch buffer (at most 255 bytes). Provider verification binds
  metadata to that exact immutable copy and length; the existing forwarder
  queues those same bytes during the serialized call. A metadata-only handle
  cannot authorize different bytes or length. Scratch storage is charged to the
  fixed memory bound and is never status/export data. This is a host binding
  obligation, not cryptographic verification of a protected production packet.
- Each new supervisor has a distinct nonzero host instance identity; each
  authority lifetime has a nonzero nonwrapping generation. Tests inject these
  identities through the trusted host fixture. Real boot freshness/entropy and
  security authority remain P4/P5 obligations. Invalidate the old generation
  before retiring its bundle; exhaustion contains the instance without wrap.
- All calls, provider observations and owner destruction occur in one serialized
  caller domain. Reentrant operations are refused before mutating state; no
  callback may run the supervisor recursively. Pending notifications are handled
  as later generation-bound events in the caller's bounded domain, never as
  implicit permission to resume. P2 creates no internal notification backlog.
- A bounded status snapshot exposes state/fixed fault category and existing
  counts, not payloads, identities or private exceptions. No log/export service,
  per-source fairness, radio driver, actual airtime debit, completion or rearm
  implementation belongs to P2. Those remain P3/P4/P5 boundaries.

### Proposed lifecycle and transition rules

The names below are host concepts, not selected on-device UI or encoded states.
Every unsuccessful readiness/admission/candidate-release check returns no frame.

| Before / event | Required transition and owner effect | Refusal and recovery boundary |
| --- | --- | --- |
| Construction | Disabled, empty volatile bundle, no forwarding or automatic store initialization. Status/shutdown are safe. | Missing/zero instance identity or invalid configured limits cannot enable operation. |
| Disabled / explicit host opt-in | A fresh provider-authorized enable request names the intended exact role/context/epoch. For authorized first use, verify the fake role commit/readback before initializing replay; for a retained disabled role, verify its authorized enable transition without clearing retained replay. Continue through Recovering and AwaitingConsent, never directly Active. | Consume the request on the first attempted mutation; canceled, stale, reused or uncertain requests cannot retry automatically. A retained role with missing replay is not first use. Partial first provisioning requires explicit reconciliation with actual state, not another default initialization. |
| Disabled / explicit recovery request | Enter Recovering only with a current provider-authorized request and coherent exact role/context/epoch/storage binding. Recover the actual existing replay store; validate fake role readback and fake airtime-readiness evidence before AwaitingConsent. | Wrong/missing/mismatched or stale evidence stays refused; retained missing media is not first provisioning. Only separately supplied explicit first-use authority permits the existing coordinator's empty-store initialization. Uncertain save/readback or incoherent generations contains the supervisor. |
| Recovering / readiness complete | Resample current provider authority after recovery and verify that the bound generation/context and original evidence deadline remain current. Enter AwaitingConsent, still with zero releases. | A change during recovery cannot become Ready; retire the volatile bundle and require explicit reconciliation. No automatic retry or checkpoint reset. |
| AwaitingConsent / consent request | Enter Active only for fresh provider-verified consent bound to this exact instance, lifecycle generation, role generation, context/epoch and original finite deadline. Consume the consent once and retain a distinct Active lease with the same bindings and unchanged deadline; later operations revalidate that lease. | Wrong-instance, stale, reused, canceled, expired or superseded handles never enable or extend an Active lease. A new instance never inherits active consent. |
| Active / frame admission | Recheck current role, membership/context/epoch, consent generation/deadline, nondecreasing time and synthetic readiness, then call the existing coordinator with exact bytes and provider-bound verified-metadata fixture. Observe/persist in the existing order. | Unsupported/unauthorized fixture evidence is refused before replay mutation. Failed or uncertain persistence contains and invalidates the bundle; queue/rate denial still consumes the eligible replay opportunity as in P1. |
| Active / take host candidate | Recheck the same gates and call `next_transmit(now)` only while valid. Existing owner applies original queue age, expiry and exact-byte release. At most one candidate is returned per call. | This is a host-test observation, not RF submission or delivery authority. A returned frame cannot be recalled; future P3 must recheck/claim authority and age before actual dispatch under its own reviewed interface. P2 does not implement that handoff or infer it from a Boolean. |
| Active or AwaitingConsent / suspend, consent expiry or injected radio-unavailable/fault event | Stop admission/candidate release, invalidate consent/generation and retire the entire volatile bundle; enter Suspended. Preserve durable opt-in and replay state. | No automatic resume after fault clear or a later clock tick. Require an explicit current recovery request, verified state and fresh consent. Injected radio events prove no driver/rearm behavior. |
| Any nonterminal state / authenticated disable, membership removal or context/epoch change | Invalidate first and retire the entire volatile queue, because existing owners cannot selectively retire by source. Verify the fake durable role transition before acknowledging durable disable/removal. | Unknown save/readback outcome enters Contained and cannot acknowledge durable removal. Preserve replay protection and report lost unsent work. A new context never authorizes overwriting mismatched media or clearing history; remain disabled until an explicitly reviewed bound transition is supplied. |
| Any live state / clock rollback, storage uncertainty, binding conflict or generation exhaustion | Enter Contained, with no admission or candidate release and no revived old-generation notifications. | Explicit service reconciliation may reconstruct fresh owners from actual verified state, followed by fresh consent; it cannot erase/import/repair arbitrarily. Exhausted generation cannot reopen within that instance. |
| Any state / shutdown | Terminal Shutdown: invalidate authority and retire volatile owners once; repeated shutdown is a no-op. Retained opt-in/replay storage is unchanged, so shutdown does not claim durable role removal. | All later mutating/recovery/admission/release events are refused; only bounded status and idempotent shutdown remain. No storage write or automatic restart occurs after closure. |

Suspension is deliberately lossy: saved-but-unsent frames remain lost, and their
persisted replay observations remain spent. Recovery never requeues them to
improve delivery. This preserves both new P1 cases without a second replay owner
or durable frame outbox. Whole-queue retirement can lose other admitted unsent
frames during membership changes; report that rather than implying selective
retirement exists.

### Time, bounds and fault contract

All host operations use one externally supplied nondecreasing logical clock.
Authority/consent/request lifetime is valid only while `now < original_deadline`;
equality refuses. Provider rereads, waiting and reconstruction do not extend an
existing grant. Clock regression contains; no guessed elapsed downtime is
credited. The existing checkpoint's retained-lifetime semantics remain host
duplicate evidence, not a trusted downtime or cryptographic replay guarantee.
Snapshot resampling checks the already-bound generation and deadline; it never
silently substitutes a newer grant, role generation or readiness token. A changed
binding retires authority and requires the explicit recovery/consent path.

Candidate release uses the existing enqueue time and drops at queue age >= T;
P2 never renews a frame's deadline. Expired entries are handled when the existing
candidate-release owner is called; P2 does not add a background scheduler or
promise P3's independent radio/airtime queue-service behavior. Suspension drops
the whole volatile bundle instead of draining it while authority is absent.

Use valid configured active depth 1..8, frame ceiling <=255 bytes and nonzero
existing rate/window/age parameters as host fixture limits, not selected target
values. One fixed bundle bounds resident frame storage by the existing eight-slot
allocation, even if active depth is smaller. No unbounded queues, member maps,
retry loops or steady-state allocations are permitted. Each call makes a finite
declared number of provider/store operations; queue scans remain capped at eight
and checkpoint slot reads at the existing store's bounded behavior. Instrument
those counts in tests. An unavailable provider is a refused/contained operation,
never a polling loop. Runtime concurrency, RF timing and target RAM headroom
remain unverified.

The fake role store must distinguish read, write, commit-uncertain and exact
readback outcomes. Inject interruption before write, after possible commit and
during readback; recover from actual retained bytes instead of the requested
result. Fake airtime readiness is a coherent input only: P2 does not create
credit, persist debit, reset a budget on reconstruction, implement a quiet
period, or validate target power-loss protection. Unknown readiness refuses
candidate release; P3 owns the future accounting contract.

### Exact proposed P2 implementation allowlist

The following **seven files only** retain the decision packet's original scope.
The first four are proposed new files and have not been created in this task:

1. `firmware/components/delivery/include/opentrail/repeater_lifecycle_controller.hpp`
2. `firmware/components/delivery/src/repeater_lifecycle_controller.cpp`
3. `tests/host/repeater_lifecycle_controller_tests.cpp`
4. `docs/protocol/REPEATER_HOST_LIFECYCLE_V0.md`
5. `tools/Test-Host.ps1`
6. `docs/testing/OT-0266b-REPEATER-PROVISIONING-RECOVERY-2026-10-01.md`
7. `docs/PROGRESS_LOG.md`

Runner scope is only the new lifecycle executable's source/include registration
using the existing strict compiler path; no unrelated wrapper behavior changes.
Fake authority, role-storage and readiness providers stay inside the new test
file. Component interfaces for those providers belong in the new header.
No existing delivery owner, P1 test, transport interface, codec/Packet-v1, crypto,
target/CMake, Android, task registry, progress score or other repository file may
change. Required changes outside this list stop implementation for scope review.
Task-owned generated outputs use a fresh child of the existing baseline directory
and retain provenance/patches without overwriting earlier receipts.

### Proposed acceptance and fault-injection matrix

L1-L12 are test-case labels only, not engineering task registrations. Every row
must execute against the new supervisor composed with actual existing replay/
forwarding/store owners; provider authority/storage conditions remain explicitly
synthetic. A skipped row is NOT RUN, not a pass.

| Case | Observable pass/fail criterion |
| --- | --- |
| L1 Disabled startup | Zero admission/candidate release and no default store initialization before a current explicit recovery request; invalid identity/configuration never opens authority. |
| L2 Coherent recovery | Missing retained media without first-use authority, unprotected/wrong context or epoch, legacy/unreadable/ambiguous media and changed provider generation refuse readiness. Exact role/readiness/replay recovery reaches AwaitingConsent, never Active by itself. |
| L3 Consent scope | Wrong instance, generation, role/context, reused/canceled/stale or deadline-equal consent yields zero admissions/releases. Exact fresh consent is consumed once and yields only this generation's Active lease at the original deadline; replaying it cannot renew that lease. Reconstructing a controller requires another consent. |
| L4 Admission ordering | Rejected metadata/authority leaves replay/store unchanged. A handle for different bytes/length is refused; caller-buffer mutation cannot alter the fixed verified copy. Accepted exact bytes follow real persist-before-release behavior; denied rate/queue opportunities remain durably spent. No caller parser flag becomes authority. |
| L5 Whole-queue retirement | Queue a valid frame, then separately inject suspension, membership removal, disable and context change. Invalidate before any later call, destroy the entire bundle, retain replay bytes and reject old-generation notifications. No selective-retirement claim. |
| L6 Resume and saved-unsent loss | Clearing a fault or advancing time cannot resume. Explicit recovery plus fresh consent reconstructs an empty queue; saved queued/denied keys remain duplicate within the retained lifetime, with zero extra save/release. Preserve P1 rate-denial and pre-expiry unsent assertions. |
| L7 Role-store interruption | Inject every fake before-write, possible-commit and readback-failure/mismatch seam. No durable-disable acknowledgement or Ready on uncertainty. Explicit recovery uses actual stored state; clearing diagnostics or retrying cannot clear replay history. |
| L8 Deadline and rollback | Exercise original grant deadline and queue age at minus one/equal/plus one. Keep the Active lease valid beyond all tested queue-age boundaries, and test grant expiry separately with an otherwise fresh frame, so one refusal cannot mask the other. Equality refuses/drops; no reread, suspend or recovery refreshes an old grant/frame. A backwards clock contains and produces zero later candidate releases. |
| L9 Injected radio fault/readiness | Fault/unavailable/missing or superseded synthetic readiness refuses or suspends; late notifications cannot reopen. Source/interface review confirms no transport dependency or submission callback. Host candidates are not counted as RF sends; P2 implements no rearm or real budget-debit path. |
| L10 Shutdown | From each reachable state, shutdown retires once without changing retained opt-in/replay bytes. Repeated shutdown adds no mutation. Every subsequent operation except status/shutdown refuses. |
| L11 Lifetime and resource bounds | Reentrant calls, provider-reported unavailability and generation exhaustion cannot revive authority. Test minimum/maximum and invalid queue limits, finite owner/provider operation counts and no steady-state heap growth. Old owners cannot be used after retirement; referenced providers remain alive by contract. |
| L12 Regression/privacy boundary | All 106 P1/baseline scenarios remain passing unchanged; new lifecycle cases pass under strict warning-as-error compilation and independent security/test review. Fixed secret/identity/payload sentinels are absent from bounded status; no endpoint-key access, Packet-v1 mutation or production target linkage. |

Acceptance requires the new focused lifecycle suite, the affected existing
14-executable/106-scenario selection plus the new executable, documentation
checker/tests, exact allowed-file and source/artifact hash checks, and independent
review. Record actual executed counts, source closure, commands and fault
outcomes; no projected P2 count is credited now. P3 dispatch/airtime ownership
and P4/P5 protected/target acceptance cannot be inferred from these host results.

### Approval request and next executable task

Accept or revise this concrete host lifecycle contract, then explicitly
authorize its seven-file P2 implementation and validation. This is the exact
missing gate identified in the existing packet; the October 28 conditional
instruction does not itself accept a contract that had not yet been prepared.
No GitHub registration is requested or required as a substitute for current
human authorization, and no remote write will occur without separate permission.
P3 remains excluded. Host-only P2 can proceed after that gate without waiting
for physical hardware or repeating the conditionally accepted D1-D7 direction.

Production source authentication, actual protected storage/rollback and boot
freshness remain P4/P5 dependencies. Region, installation/duration, assembly,
power/ignition, antenna and measured resource/radio limits remain future product
inputs; none authorizes a target, wire change or production relay here.

Preparation validation and independent review are recorded in the task-owned
`build/repeater-baseline/p2-preparation-20261028` receipt. P1's 106 runtime
scenarios are reused after hash verification; no P2 runtime tests are claimed.
This preparation creates no product/version/V1/public-website capability change.

**Housekeeping:** no files removed; original baseline/P1 artifacts and prior
working content preserved. The existing detached worktree remains with its five
uncommitted files; only this report and the progress log change in preparation.
The task-owned preparation child retains preimages, patch, hashes, documentation
results and review receipts. No worktree creation/removal, other-session change
or cleanup needing owner approval. Stop for the concrete P2 contract decision.

## P2 host lifecycle implementation closeout

The owner's subsequent explicit October 28 authorization accepted the reviewed
P2 contract and L1-L12 and granted its exact seven-file implementation scope.
That grant supersedes the preparation-only readiness statement above for P2;
D1-D7 remain unchanged. **P2 host implementation and validation are complete.**
OT-0266b remains open, the parent Project remains Todo, and no GitHub task/status,
publication, hardware, deployable target or P3 operation was performed.

### Exact changed files and candidate

In the same detached worktree at base
`c96d93ea459a7c41cf453e27edea082985650fa6`, P2 changes only:

1. `firmware/components/delivery/include/opentrail/repeater_lifecycle_controller.hpp`
2. `firmware/components/delivery/src/repeater_lifecycle_controller.cpp`
3. `tests/host/repeater_lifecycle_controller_tests.cpp`
4. `docs/protocol/REPEATER_HOST_LIFECYCLE_V0.md`
5. `tools/Test-Host.ps1`
6. This existing report.
7. `docs/PROGRESS_LOG.md`

The first four are new. The runner change registers exactly one new executable
using the five existing replay/forwarder sources plus the new component/test.
Earlier P1 coordinator-test and task-plan/backlog changes remain byte-preserved;
the two existing handoff documents retain their earlier content plus this P2
closeout. No existing production owner, Packet-v1, crypto, Android, target or
other worktree source changes.

### Lifecycle and recovery result

The [materialized host contract](../protocol/REPEATER_HOST_LIFECYCLE_V0.md)
defines a nonmovable, allocation-free owner composition. Startup cannot admit
or release a frame. Exact recovered role/context/replay/readiness first reaches
AwaitingConsent; a fresh consumed provider consent creates a distinct Active
lease at its original deadline. Admission verifies a bounded private frame copy
and exact length, then invokes the existing save-before-release coordinator.
Context/readiness changes are never silently adopted as new authority.

Suspension, revoked/expired authority, injected radio faults and storage
uncertainty invalidate the generation before destroying the entire volatile
queue. Saved-but-unsent work stays lost and saved replay stays spent. Explicit
recovery and fresh consent are required after retirement; clearing a fault is
insufficient. Shutdown is terminal/idempotent and leaves retained opt-in/replay
bytes intact. Exhausted generations never wrap or repair themselves.

Uncertain-submission and failed-rearm events enter Contained as injected host
observations only. There is no send, completion, rearm, transport callback or
airtime-debit implementation. A returned candidate is an owned host observation,
not a production transmission grant. P3 must separately review dispatch claiming
and original-age/authority checks. Protected payloads remain opaque, and no
endpoint key is accessed.

### L1-L12 validation

Each named group executes multiple boundary/fault variants against actual
existing forwarding/replay/checkpoint owners with explicitly synthetic providers:

| Group | Executed coverage | Result |
| --- | --- | --- |
| L1 | Disabled/no-IO startup and 16 invalid identity/configuration variants | PASS |
| L2 | Exact role/readback before replay init, healthy IO bounds; retained missing/wrong-context/epoch/legacy/unreadable/ambiguous/corrupt media; zero/MAX role and desired-counter refusal | PASS |
| L3 | Instance/generation/role/readiness/deadline-bound one-use consent and nonrenewable Active lease; fresh consent after reconstructed instance | PASS |
| L4 | Wrong-byte/length rejection, caller-buffer mutation versus private scratch, real persistence ordering and post-replay-commit readiness loss with retained duplicate proof | PASS |
| L5 | Whole-queue retirement on suspension/authority changes/disable; stale generation notifications cannot reopen | PASS |
| L6 | No automatic resume; explicit recovery/consent preserves saved and rate-denied replay with no extra save/release | PASS |
| L7 | Fake role/replay before-write, possible-commit, readback-error/mismatch seams and consumed-request staleness before write/after commit; no false durable acknowledgement | PASS |
| L8 | Separate grant and queue-age minus-one/equal/plus-one fixtures; rollback containment without expiry masking | PASS |
| L9 | Injected unavailable/uncertain-submission/rearm-failed/readiness-generation outcomes; no radio operation | PASS |
| L10 | Terminal/idempotent shutdown from reachable states, including reentrant Recovering observation, with unchanged retained bytes | PASS |
| L11 | Reentrancy refusal, provider unavailability, lifecycle exhaustion, queue depth 1/8 bounds, no steady-state allocation, bounded provider/replay IO | PASS |
| L12 | Bounded typed status sentinel/privacy checks, full 255-byte opaque candidate and invalid shape refusal; regression run below | PASS |

Focused strict compilation and execution passed all **12 lifecycle groups**.
The final affected matrix rebuilt and passed **15 executables**, preserving all
**106 existing scenarios/groups** and adding those 12 groups (118 reported
scenarios/groups total). P1's coordinator remains **11 passing groups**; its
source and original nine groups plus two P1 additions are unchanged. Compilation
uses GCC 16.1.0, C++17, `-Wall -Wextra -Wpedantic -Werror -O2`; final compile/run
receipts report exit 0 and no compiler/runtime stderr.

The existing runner was reused in memory with only a fresh output directory and
the new lifecycle executable added to its selected set. The runner recipe records
both transformations and hashes. The original baseline runner and all original,
P1 and preparation receipts/artifacts remain unchanged. The new matrix source
manifest was checked again at completion. Documentation checker/tests, added
links/anchors, exact seven-file scope and preservation checks are retained in the
final receipt; no full unrelated repository matrix or physical trial is claimed.

### Independent review and evidence

Separate security and test/source reviews found no remaining blocking issue.
Review corrections now have discriminating fixtures: malformed/exhausted retained
role generations are contained instead of rewritten; disable acknowledgements
recheck the consumed transaction across storage callbacks; and post-persistence
authority loss retires queued work while retaining the spent replay observation.
Providers, including the existing replay-storage seam, must be finite and
nonthrowing; arbitrary thrown-adapter exception recovery is not claimed.

The first author run failed an incorrect test fixture that tried to activate an
already exhausted initial generation. Its failed receipt/executable remain
preserved; corrected focused attempts and the final matrix passed. In particular,
the earlier top-level `test-author/author-result.json` is **not** the passing
closeout. Use `test-author/attempt-3/author-result.json` and final
`matrix/results.json` for the accepted focused/matrix observations.

Task-owned evidence lives in
`build/repeater-baseline/p2-implementation-20261028`: `before.json`, exact
preimages of the three existing allowed files, component preflight, all author
attempts, matrix outputs/executables/source hashes/toolchain, console log, runner
recipe, documentation results, independent reviews, `p2.patch` and final
`verification.json`. The final receipt records SHA-256 for every changed file,
patch and artifact and verifies preserved P1/test-plan bytes. These ignored local
artifacts do not constitute a release or travel automatically with Git.

### Remaining dependencies and housekeeping

Stop after P2. P3 still needs separate authority for dispatch claiming, actual
airtime/budget ownership and completion/rearm integration; no candidate implies
that acceptance. P4 owns real relay-capable authentication, key/state protection
and trusted boot freshness; P5 owns exact target/region/antenna/power/duration,
memory/wear/radio measurements and build scope. P6/OT-0267a/b keep their separate
integration, physical interoperability/interruption/restoration and support/
release gates. No product version, V1 score or public website capability changes.

**Housekeeping**

- **Temporary files removed:** none; no cleanup necessary or destructive cleanup.
- **Temporary files retained and why:** original baseline/P1/preparation evidence
  and the P2 implementation child, including failed author attempts, remain for
  deterministic review/provenance and recovery.
- **Worktree status:** existing detached worktree retained at the same base with
  seven P2 files and three byte-preserved prior dirty files, all uncommitted.
  No worktree created/removed and no Trail Live or other-session file modified.
- **Evidence preserved:** exact preimages, source/patch/artifact hashes, strict
  compile/run logs, final matrix and independent review findings.
- **Cleanup requiring owner approval:** none necessary for P2; no historical
  output/worktree retirement proposed. P3 is not started.

## P3 dispatch and resource-budget preparation

Prepared October 28, 2026 under the owner's bounded preparation-only task.
**Recommendation: BLOCKED for implementation under the original seven-file P3
scope; READY for owner review of the prerequisite and exact scopes below.**
No dispatch code, lifecycle interface, queue implementation or runtime test is
changed by this preparation. Local P3-A/P3-B labels are proposal labels, not new
registered engineering tasks. D1-D7 and the accepted P2 behavior are unchanged.
The live parent remains Todo; no GitHub write or execution authority is inferred.

### Verified baseline and actual interface gap

Preparation starts in the existing detached worktree at
`c96d93ea459a7c41cf453e27edea082985650fa6`. P2's seven current source/document
hashes, all 35 P2 artifacts and 255 matrix inputs matched its verification
receipt, SHA-256
`f39a65399070f69bf384ba13dc21b6e01d3a9e86e0ebc7fbeef9e1892f25ce71`.
All 28 P1 and 22 original baseline artifacts also matched. The 15-executable,
118-scenario/group result (106 prior plus 12 P2 groups) is reused; no runtime
suite is rerun and no P3 result is claimed for preparation.

| Existing owner/interface | What exists now | What P3 must not infer |
| --- | --- | --- |
| `SingleRepeaterForwarder` / delivery | Sole fixed FIFO, exact bytes, private enqueue time, global packet-rate/age/queue checks and duplicate/reflection policy. | Output contains no original age or verified source provenance. There is no expiry-only prune or external source-admission gate. |
| `SingleRepeaterReplayCoordinator` / delivery | Actual verified checkpoint save before release, including queue/rate denials. | It is not an airtime/source-quota ledger or durable frame outbox. Do not replace it or change its P1 semantics. |
| `RepeaterLifecycleController` / P2 | Consent/current-authority checks, whole-queue retirement and destructive `take_candidate(now)`. Returned data is only lifecycle generation plus exact frame/result. | A popped observation has no dispatch claim, instance/role/readiness/source binding or original enqueue/expiry metadata. Its generation/status cannot authorize a later submission. |
| `RadioTransport` / radio | Opaque nonblocking `send`, receive, cooperative service and aggregate status. `send` copies/queues bytes. | Queue acceptance is not correlated completion, no-emission proof, receive rearm or endpoint admission. |
| `FakeRadioTransport` / test support | Bounded queues, synthetic drops/faults/latency and delivery on service. | Its readiness/delivery convenience is not an independent terminal/rearm contract. |
| `PairRadioDriver` / security evaluation | Evaluation-only pending-transmit/rearm/readiness methods and statistics. | No accepted Repeater attempt/claim adapter; do not import target or evaluation authority. |
| `calculate_lora_airtime` / radio | Pure deterministic cost calculation from explicit PHY and byte count; existing `LoRa airtime` tests. | No reservation, durable debit, refill authority or regulatory/target acceptance. |
| `PriorityTrafficQueue`, `DeliveryController`, logger/ring | Reusable priority/retry and bounded logging components exist for other contracts. | Priority/preemption/retries conflict with the selected dedicated FIFO/one-attempt policy. Logger use alone does not establish private bounded Repeater diagnostics. |

The original P3 scope keeps these P2/queue owners read-only. A dispatcher cannot
recover lost provenance by parsing protected payloads, starting a new age clock
at take/claim, mirroring a second payload FIFO or trusting a caller Boolean.
A standalone dispatcher with fabricated candidates would not connect actual P2
and is not the recommended next increment.

### Smallest proposed sequence and exact file allowlists

Recommend two independently reviewable host slices. **Neither is approved for
implementation by this preparation.** Approve P3-A first; validate its real P2
bridge before authorizing P3-B. Their union is fourteen files, not an expansion
silently admitted under the original seven-file grant. Any additional file or
materially different contract returns for owner review.

**P3-A: provenance, bounded admission hook and one-use P2 handoff** — nine
existing files only; no new dispatcher, budget implementation or radio adapter:

1. `firmware/components/delivery/include/opentrail/single_repeater_forwarder.hpp`
2. `firmware/components/delivery/src/single_repeater_forwarder.cpp`
3. `tests/host/single_repeater_forwarder_tests.cpp`
4. `firmware/components/delivery/include/opentrail/repeater_lifecycle_controller.hpp`
5. `firmware/components/delivery/src/repeater_lifecycle_controller.cpp`
6. `tests/host/repeater_lifecycle_controller_tests.cpp`
7. `docs/protocol/REPEATER_HOST_LIFECYCLE_V0.md`
8. This existing OT-0266b report.
9. `docs/PROGRESS_LOG.md`

P3-A extends existing owned queue metadata/results, adds a bounded expiry-only
operation and an optional host admission-gate interface, and exposes a fixed-mode
one-use claim bridge through P2. Its tests use fake gates/callbacks only. Existing
coordinator source and P1 coordinator tests remain read-only; the coordinator
already persists every reported new replay observation. Existing test executables
are registered, so no runner change is needed for A. Default nonbridge P2 and
forwarder behavior must retain every existing regression outcome.

**P3-B: host dispatch, fixed resource accounting and private diagnostics** — the
original seven-file candidate, after P3-A and its contract are accepted:

1. Proposed `firmware/components/delivery/include/opentrail/repeater_dispatch_controller.hpp`
2. Proposed `firmware/components/delivery/src/repeater_dispatch_controller.cpp`
3. Proposed `tests/host/repeater_dispatch_controller_tests.cpp`
4. Proposed `docs/protocol/REPEATER_HOST_DISPATCH_V0.md` (host API, not wire protocol).
5. `tools/Test-Host.ps1` — register one new composed executable only.
6. This existing OT-0266b report.
7. `docs/PROGRESS_LOG.md`

The four proposed B files are not created now. B composes the actual extended P2
and unchanged airtime helper through synthetic budget/radio ports. Existing
transport/evaluation/crypto/target/Packet-v1 code and all files outside A/B stay
read-only. B cannot quietly repair A's interface; a mismatch stops for review.

### Proposed queue, admission and claim ownership

The existing forwarder remains the sole payload FIFO and queue-age owner.
P3-A retains the verified opaque source/context/epoch/message binding and original
enqueue time/age limit with each admitted frame; no protected byte changes.
Expiry uses subtraction against that original time, not an overflowing invented
deadline or a new timestamp at dispatch. Expiry-only service removes expired
heads in at most eight visits and never releases a live head.

The optional admission gate runs only after existing shape/authentication/
context/permission/time and new-key checks. It receives already verified host
metadata, never parsed payload identity. Ordinary source/global quota or pending-
capacity denial reports a changed replay observation, so the unchanged coordinator
saves/readback-verifies it before further release. Gate success followed by the
existing queue/rate denial still consumes quota conservatively; no refund.
An unavailable/uncertain gate is distinct from ordinary exhaustion: preserve
that replay ordering and contain P2/B so older queued frames cannot escape under
unknown accounting. Target storage-write demand remains a P5 measurement gate.

P2 bridge mode is fixed at construction. It owns at most one volatile pending
payload taken from its FIFO; B receives only a metadata ticket, not another
payload queue. Legacy `take_candidate` is refused unconditionally in bridge mode.
The ticket binds the exact instance, lifecycle/role/readiness/context/epoch,
verified source/message, original enqueue/age limit, original consent bound and
one-use candidate identity to P2's owned bytes. No new hash/crypto or wire field
is needed to bind an internally owned buffer.

After B verifies/debits resources, P2 checks the same original authority and age
again, consumes the ticket **before** invoking one synchronous finite nonthrowing
claim/submit callback in the shared serialized domain. The callback borrows the
owned exact bytes only until return; the radio port must copy them within its
declared fixed buffer and retain no borrowed pointer. Reentrant mutation cannot
replace bytes, authority, time or the claim. After return or retirement the
pending slot/ticket is retired, never requeued. Revocation before claim means
zero submissions; revocation during/after the callback may follow one claimed
emission and cannot retroactively imply no-send. It prevents every later claim.

B is a nonmovable composition owning its P2 and private gate; caller-owned
authority/store/budget/radio ports outlive that graph. No caller-visible path
bypasses the gate/claim bridge. All methods and port calls are serialized,
bounded and nonthrowing; no target callback/thread executor is introduced.

### Proposed budgets and conservative initial policy

These choices require owner acceptance with A/B; they are host evaluation
parameters, not selected production/regulatory defaults.

| Resource | Proposed finite rule and accounting owner |
| --- | --- |
| Queue/count | FIFO depth N within existing 1..8. Count queued + pending + in-flight against the same N; moving a frame never opens extra capacity. For the smallest first slice, refuse new data admission while any pending/in-flight/rearm ownership exists, through the post-auth/new-key gate so eligible pressure is still durably spent. Control/disable/shutdown are not queued behind data. |
| Memory/bytes | Frame ceiling M in 1..255 and explicit logical byte cap at least N*M. Charge the actual eight-slot fixed queue allocation even for smaller N, plus P2 scratch, one pending buffer, one radio-port copy, provenance, ledger and diagnostics. Record actual `sizeof`/allocation bounds; no steady-state heap, second FIFO or unbounded backlog. Logical occupancy is at most N, even where fixed copy/storage regions overlap in lifetime. |
| Source/global admission | B's fixed private ledger keys quotas by verified context/epoch/source. Initial host fixture roster has two named synthetic endpoint sources; K=2 fixed entries, with K=1/full/K+1 negative fixtures. Unknown/unavailable entries refuse; never evict spent protection to admit another identity. Source allowance S and global admission allowance G are positive finite epoch credits (fixture S=1, G=2), separate from P2's existing packet-rate/window. No implicit borrowing/refill or quota reset on reconstruction. |
| Airtime | Reuse `calculate_lora_airtime` with an explicitly validated host PHY/M; charge full conservative maximum-frame cost C before claim. Test allowance A at C-1/C/C+1, arithmetic overflow and invalid PHY/cost. No fractional/negative allowance or uncharged uncertain emission. Host values prove neither regional compliance nor actual driver/antenna behavior. |
| Durable debit/restart | A synthetic exact-bound ledger port commits/readback-verifies admission/airtime consumption. Debit is irreversible for the initial slice: no refunds, including definite rejection or failed final claim. Unknown or uncertain debit contains before submission. Recover actual retained ledger state; no full-credit defaults, invented quiet period or retry to resolve an uncertain debit. |
| Budget domain | Global airtime/credit identity survives lifecycle generation, consent, group/context changes and restart; do not key fresh global credit solely on a volatile P2 instance or epoch. New/refilled allowance requires separate verified budget-epoch/time authority, not an automatic window rollover. Missing/damaged/legacy/mismatched ledger stays NOT_READY. Actual protected retention/clock/rollback are P4/P5 gates. |
| Work/events | At most one data candidate and one claim/submission per service call; at most one budget transaction per corresponding phase, one bounded terminal/rearm event, and an expiry pass capped by eight queue entries. No wait-until-ready loop, internal event backlog or automatic resubmission. Budget exhaustion retires/refuses that opportunity; radio-unready waiting keeps original age and is bounded by expiry. |
| Diagnostics | Typed state/refusal/queue/pending/in-flight/aggregate counters only; fixed counter set, uint32 saturation plus overflow flag and one coarse fault category. Event-record capacity is zero: no raw exception stream, packet/source history, payload/key/location export or automatic logger integration. Clearing counters cannot change replay, quota/debit, attempt or lifecycle state. |

Admission/source debit can precede replay save and be conservatively lost if
that later save fails. Airtime debit can precede claim and be lost if authority
expires. This deliberately sacrifices opportunities rather than creating an
uncertain credit/refund transaction. Nothing makes volatile frames durable.
Authenticated denied traffic still imposes replay-write demand; these host caps
do not establish target flash endurance or denial-of-service resistance.

### Proposed dispatch and failure transitions

| Owner phase | Permitted operation | Failure/cleanup |
| --- | --- | --- |
| NOT_READY | Verify P2 fresh admission, original candidate provenance, known ledger, valid PHY/cost and current radio generation/readiness. | Zero submits on absent proof. No automatic role/budget initialization. |
| Pending | Hold P2-owned oldest live candidate; prune expired FIFO work even when radio/budget is unavailable. | Pending/queue expiry retires bytes/ticket without renewing age, replay or source allowance. Refuse newest under pressure; no priority bypass. |
| Resource reservation | Commit/readback full conservative airtime debit for one bound attempt before final P2 claim. | Exhaustion refuses; uncertainty contains P2/B. No retry/refund/requeue of this opportunity. |
| Claim / one submit | Final P2 authority/age check and one-use claim immediately precede one host-port submit. Bind attempt, driver, P2 and budget receipt generations. | Busy/definite rejection/partial or uncertain acceptance never triggers another submit. Retire the ticket; spent credits remain spent. Partial or ambiguous acceptance contains with possible emission. |
| Await terminal | Accepted submit means pending only. Accept one matching bounded terminal event for the exact attempt/driver/lifecycle tuple. | Timeout or missing/unknown terminal state conservatively preserves debit and contains. Old, duplicate or wrong-attempt events cannot free new resources or create readiness. RF completion is not endpoint ACK/admission. |
| Await receive rearm | After a known terminal boundary, require an explicit bound receive-ready/rearm result before another attempt. Even definite submission rejection does not manufacture receive readiness. | Failed/uncertain/late/wrong-generation rearm contains; no automatic recovery or hidden retry. |
| Suspend/revoke/restart | Stop admission and claims, invalidate pending/attempt authority and retire volatile P2 work; keep all verified or uncertain debit/source/replay records. | Already emitted bytes cannot be recalled. Explicit ledger/lifecycle recovery and fresh consent are needed; lost saved-but-unsent work is never restored. |
| Shutdown | Terminal/idempotent; no further submit, ledger mutation or callback-created readiness. Release borrowed RAM safely after the bounded callback; radio port owns any already-copied bytes. | Late events are refused, not used to rewrite uncertainty into completion. Possible emission/debit remains truthful; a new instance recovers actual retained state and never inherits fresh credit. |

Candidate, attempt, driver and budget-epoch identities must be nonzero and
nonwrapping; exhaustion contains rather than reusing an old callback identity.
Reconstruction preserves radio uncertainty: require retained/quiescent-port proof
or verified terminal and rearm reconciliation before another attempt. A fresh
default-ready fake radio is not recovery evidence.

The new header may define synthetic submit/terminal/rearm and ledger seams, with
all fake implementations confined to its host test. No real radio adapter or
evaluation-driver reuse is admitted by A/B. A later target adapter must prove
copy/lifetime, internal retry, terminal and receive-rearm semantics separately.
No relay-generated endpoint ACK, alternate route, second hop, source priority or
new protected envelope is included.

### Proposed acceptance and fault-injection matrix

F1-F12 are acceptance labels only; all are **NOT RUN** for P3 preparation.

| Case | Observable pass/fail criteria |
| --- | --- |
| F1 Actual P2 gate | Zero submits for Disabled/AwaitingConsent/Suspended/Contained/Shutdown, stale/canceled/mismatched source/authority or unbound bridge. Legacy take always refuses in bridge mode; host status/Boolean cannot claim a dispatch. |
| F2 Provenance and FIFO age | Exact original bytes/length/source/context survive queue-to-pending handoff. Separate age T-1/T/T+1 from consent expiry; wait/reservation cannot refresh age. Expiry-only prune drops expired heads without popping a live one. |
| F3 Shared capacity | N=1/8 and M bounds; queued+pending+in-flight never exceed N/count/byte caps. Busy-data denials flow through real replay persistence and remain duplicate across reconstruction, not a cheap early busy bypass. No duplicate payload FIFO or steady-state allocation. |
| F4 Admission quotas | K/S/G boundary/full/overflow fixtures, unknown source, duplicate and denied-key reconstruction. Gate runs after authentic fixture/new-key checks; invalid input allocates no source state. Never evict a spent source entry; later queue/rate rejection refunds nothing. Uncertain gate state contains all release. |
| F5 Airtime ledger | Validate actual helper/profile/cost; A=C-1 refuses, A=C permits one debit, no negative/overflow/zero-cost acceptance. Inject before-write, possible-commit, readback-error/mismatch. Zero submits without verified debit; final-claim failure retains spent debit. |
| F6 Callback races | Revoke/change original binding after take, during ledger callback, immediately before claim and inside/after submit. Before claim: zero submits. After the claim point: at most one possible emission, then zero new old-generation claims. Ticket consumed before callback; reentrant mutation refuses. |
| F7 One attempt and terminal | At most one submit per admitted object; exactly one only when a successful final claim invokes the submission callback. Preclaim refusal/expiry/revocation gives zero. Busy/reject/partial/unknown outcome never causes resubmission. Accepted queueing is not completion. Match attempt+driver+lifecycle+budget identities; duplicate/late terminal cannot complete another attempt or refund budget. |
| F8 Rearm | Known terminal then explicit successful rearm is required for next work. Failed/uncertain/wrong-generation/missing rearm keeps zero further submits; fake transport service must not silently rearm. |
| F9 Restart/uncertainty | Reconstruct actual P2 and ledger from retained bytes, preserving radio-port uncertainty at save/debit/claim/possible-emission boundaries. Require quiescent proof or terminal/rearm reconciliation; never substitute a default-ready fake port. Reject zero/wrapped callback identities. Preserve P1 replay and saved-but-unsent loss, spent source/global/airtime allowances and no reset after new consent/context. Missing ledger never defaults to full. |
| F10 Bounded failure cleanup | With radio/budget unavailable or attempt pending, expiry/control service remains finite. Suspend, revoke, driver fault and shutdown retire all volatile work and reject old tokens/events. One service phase cannot loop, queue unbounded events or re-submit an object. |
| F11 Privacy/diagnostics | Saturation/overflow and fixed coarse state projection; secret/source/identity/payload/location sentinels absent. No event records/export; diagnostic clear leaves role/replay/ledger/attempt hashes unchanged. Terminal shutdown adds no callback mutation. |
| F12 Regression/scope | P1 coordinator source and all 118 existing reported cases remain passing. A extends existing forwarder/P2 suites; B adds its new suite and the existing LoRa-airtime suite to the affected matrix. Strict builds, docs/link checks, source/patch/artifact hashes and independent security/test review pass with exact allowed files and no production linkage. Record actual future counts; do not credit proposed tests now. |

### Readiness, approval and preparation closeout

The smallest next executable candidate is **P3-A's nine-file host bridge**, after
owner acceptance of its provenance, gate, pending-capacity and one-use-claim
contract and explicit implementation authority. P3-B then needs separate scope/
budget/port acceptance and implementation authorization. The fourteen-file union
is disclosed for dependency visibility, not requested as implicit blanket work.
The original seven-file P3 implementation stays BLOCKED until A exists; do not
hide the missing integration behind synthetic candidate producers.

No new hardware decision is required for A or synthetic B. Production source
authentication/protected ledger/boot-time authority remains P4; exact region,
PHY/radio/antenna, installation/power/duration, RAM/latency/wear and adapter
evidence remain P5; physical/release/support claims remain P6/OT-0267a/b.
No actual budget refill/time/rollback protection or regulatory claim is selected
by this host plan. Owner acceptance of A/B does not alter Packet-v1 or permit RF.

Only this report and `docs/PROGRESS_LOG.md` change during preparation. Source,
tests, P1/P2 contracts, runner and prior dirty files remain byte-preserved.
Documentation results, independent reviews and exact preservation/patch hashes
are retained under `build/repeater-baseline/p3-preparation-20261028`. Existing
runtime results are reused after hash verification, not rerun or promoted into
P3 evidence. No version, V1 score or public-website capability change.

**Housekeeping:** removed nothing; no cleanup necessary. Baseline/P1/P2 and all
failed/successful prior evidence remain retained. The existing detached worktree
keeps its ten prior dirty files, with only this report/progress edited now. The
preparation child retains preimages, patch, doc/review receipts and hashes; no
new worktree, other-session mutation or cleanup needing owner approval. Stop
for P3-A contract and exact scope approval; do not begin either implementation.


## P3-A host bridge implementation

Owner authorization on October 28, 2026 accepts only the nine existing files in
[P3-A](#smallest-proposed-sequence-and-exact-file-allowlists).
P3-B is not started. D1-D7 and the two-node V1 production boundary are unchanged.
Live Trail V1 item 271124158 remains Todo / Needs Decision with its historical
parent planning scope; the explicit human nine-file grant supplies this bounded
implementation authority. No GitHub record or status was changed.

The existing detached candidate remains `c96d93ea459a7c41cf453e27edea082985650fa6`
in the isolated Repeater worktree. Before mutation, the P3 preparation receipt,
its six artifacts and current P1/P2 source hashes matched. All 28 preserved P1
and 35 P2 artifacts verified against their receipts. Nine exact preimages and a
tracked-file preservation manifest were captured under
`build/repeater-baseline/p3a-implementation-20261028`.

### Implemented ownership and transition boundaries

| Boundary | P3-A host behavior |
| --- | --- |
| Verified admission | Forwarder snapshots verified metadata and exact opaque bytes before an optional bounded gate. Existing authentication/context/permission/time and new-key checks precede gate invocation. Denials keep replay-changed signaling; the unchanged coordinator saves/readback-verifies the opportunity. |
| Uncertain accounting | Unknown/uncertain gate outcome latches raw release refusal. P2 persists the new observation before containing and retiring the whole volatile owner. No credit refund or hidden retry is introduced. |
| FIFO and age | Existing fixed FIFO remains sole queue. Original source/context/message metadata, enqueue time and maximum age transfer with bytes. Expiry-only pruning is bounded and leaves live heads untouched. |
| Pending custody | Fixed bridge mode refuses legacy extraction. P2 owns one pending frame and issues metadata-only tickets. Eligible admission while pending is denied through replay persistence, preserving combined N bounds. |
| Claim | Exact original ticket, current original authority/lease and original queue age are required. Candidate IDs are nonzero/nonwrapping. Ticket is consumed before one synchronous nonthrowing callback; only borrowed exact bytes are exposed for its duration. Stale/altered/reused tickets and reentrant mutators refuse. |
| Retirement | Suspension, authority loss, restart and terminal shutdown retire pending and queued bytes. Replay remains durable; saved-but-unsent frames are never reconstructed. Injected uncertain submission contains without reissuing a consumed claim. |
| Limits | Fixed buffers and finite operations; no second FIFO, target, real radio adapter, airtime ledger, protected storage, endpoint key or Packet-v1 change. Local 255-byte forwarder snapshot stack cost is additional to in-object fixed storage. |

The [host lifecycle contract](../protocol/REPEATER_HOST_LIFECYCLE_V0.md#p3-a-bounded-dispatch-bridge)
describes the additive bridge API. A callback invocation proves neither RF
submission/completion nor receive rearm or endpoint acknowledgement. Postclaim
authority loss truthfully reports that the callback already ran. B must separately
own budget durability and attempt/terminal/rearm accounting, preserve radio
uncertainty across reconstruction, and refuse admission while its external
pending/in-flight ownership remains outstanding.


### P3-A validation coverage and remaining gates

The focused suite extends the existing executables, not the runner or production
composition. A1-A3 are three new forwarder groups; A4-A10 are seven new lifecycle
groups. Original nine forwarder and twelve P2 group bodies remain intact. The
unchanged eleven-group P1 coordinator suite remains a separate regression gate.

| Fixture | P3-A subset of approved F1-F12 |
| --- | --- |
| A1 | F2/F10: FIFO verified metadata and exact bytes, original enqueue/age, bounded expiry-only pruning without popping live heads. |
| A2 | F3/F4: shape/auth/context/permission/time/duplicate checks before gate; eligible denials consume replay; queue/rate refusal after gate does not refund. |
| A3 | F4/F6: sticky uncertainty blocks older release; raw gate reentry refuses; caller-input mutation cannot replace private metadata/bytes. |
| A4 | F1/F2: bridge/nonbridge separation, legacy-take refusal, one pending metadata ticket, FIFO/age custody and one-use claim. |
| A5 | F3/F9: pending-pressure denial persists through real coordinator; actual supervisor reconstruction retains denied-key replay and loses unsent bytes. |
| A6 | F2/F6: all twenty ticket fields individually altered; original age and separate consent equality boundaries; invalid/stale/reused claim gives zero callbacks. |
| A7 | F6: consumed marker before callback; nine mutators refuse reentry; borrowed bytes survive; original caller-ticket mutation is irrelevant; later authority loss reports invocation truthfully. |
| A8 | F8/F9/F10 subset: pending plus queued retirement on suspension, revocation, disable, shutdown and injected uncertain-submission/rearm/radio faults; actual reconstruction and replay retention; local grant expiry is distinct from queue expiry. No real completion/rearm adapter is tested. |
| A9 | F4/F5 subset: uncertain gate observation saves/readbacks before containment; pending-pressure replay save failure contains; gate callback authority change cannot release older work. No airtime ledger is implemented. |
| A10 | F3/F10/F11 subset: N=1/8 by M=1/255, candidate zero/MAX/MAX-1, foreign instance, ten admitted/prepared/claimed frames with zero observed heap allocation; status contains bounded coarse fields. |

Host ABI measurements are controller 4,840 bytes, coarse status 56 bytes and
metadata ticket 136 bytes. These are this compiler/host's `sizeof` values, not a
selected target RAM or stack budget. The forwarder adds a fixed 255-byte local
snapshot and metadata scratch; bounded return/callback values also consume stack.
The allocation fixture covers the hot path, not target adapters or storage.

P3-B remains unimplemented and requires separate exact seven-file authorization.
It must compose this actual bridge and implement fixed private resource ledgers,
source/global quotas, irreversible airtime debit, bound attempt identity,
terminal/rearm proof and bounded private diagnostics. Any interface mismatch
returns for review. P4 authentication/protected storage and P5 physical/radio/
power/wear/region evidence remain open; no RF, real enrollment, release or
production mesh capability is accepted. No version, V1 score or public website
status changes from this host prerequisite.


### P3-A final results and evidence

The authoritative final matrix passed all fifteen strict GCC 16.1.0 C++17
executables (`-Wall -Wextra -Wpedantic -Werror -O2`), with empty compiler stderr:
**128 reported scenarios/groups = 118 prior + ten new P3-A groups**. Forwarder
reports twelve (nine legacy plus A1-A3), lifecycle nineteen (twelve legacy plus
A4-A10), and unchanged P1 replay eleven. All original forwarder/P2 test bodies
are byte-identical after newline normalization. Matrix source hashes remained
stable across compilation/execution. The original runner and Test-Host.ps1 are
unchanged; an evidence-local recipe changes output and suite selection in memory.

Independent security review found no remaining blocker. Test review prompted
local deadline checks during prune and corrected three fake-gate declaration
orders so gates outlive their controllers. Both corrections are present in the
final matrix inputs. Exact-byte/provenance snapshots, consumed-before-callback
ownership, stale ticket refusal, persisted pressure, uncertain-state retirement
and no-allocation hot-path fixtures passed. Focused attempts remain preserved as
intermediate passing evidence; the final matrix is the authoritative frozen
source run. Documentation checks and all eighteen documentation tests are
recorded in the final verification receipt.

Evidence is retained under `build/repeater-baseline/p3a-implementation-20261028`:
`before.json`, nine `preimages`, `tracked-before-hashes.json`,
`prior-evidence-verification.json`, focused attempts, `matrix/results.json`,
`matrix/source-hashes.json`, toolchain receipt, documentation/review receipts,
`p3a-implementation.patch` and `verification.json`. The verification receipt
binds exact changed-file, patch, artifact and input hashes; its own SHA-256 is
reported at closeout to avoid a self-referential manifest.

**Housekeeping:** no files removed and no cleanup necessary. Prior baseline,
P1/P2/preparation evidence, all dirty source and historical attempts remain
preserved. The existing detached worktree remains at the same revision; no new
worktree, other-worktree mutation or removal. Only the approved nine files change
in this task; older backlog/plan/P1/runner dirt remains unchanged. Task-owned
preimages, executables, manifests, receipts and patch are retained for review and
reproduction. No cleanup requires owner approval. P3-A stops here; P3-B requires
its own explicit authorization.


## P3-B implementation decision candidate

Prepared October 28, 2026 under the conditional P3-B preparation/execution task.
**BLOCKED for implementation; READY for owner decision on the contract, host
fixture profile and exact seven-file grant below.** P3-A is accepted and
host-validated; its grant did not approve B. The earlier budget table explicitly
requires owner acceptance. D1-D7 are unchanged and need no repeat approval.
No B source, host dispatch contract, test suite or runner registration is created
by this preparation. Only this report and the existing progress log change.

### Current candidate, authority and existing accounting seams

Candidate remains the isolated detached Repeater worktree at
`c96d93ea459a7c41cf453e27edea082985650fa6`. P3-A verification receipt SHA-256
`895dedc06e0d75bcb948e630509a39d6410316642443aa792b7d2c01ab8f0758`, patch
`9d2a1b9a9180f48b292e76230c32e5ffeb01201fb383e284e614dca39507a1fd`,
all nine current file hashes, 43 artifacts and 255 matrix inputs matched before
edits. The 15-executable/128 reported scenario/group baseline is reused as
hash-bound evidence, not rerun or credited as B implementation.

The current parent portfolio tracking map records the October 28 cutover to
[Trail Repeater Project #9](https://github.com/orgs/Limited-Underground/projects/9).
The retained migration mapping links original database item 271124158 to current
item 286137273 (`PVTI_lADOE0PZG84BpI_OzhEOG7k`). Original Project #1 history is
archived. A direct read of the mapped private item returned GitHub Page not found
in the signed-out browser; current remote status/decisions were not verified.
Earlier Project #1 Todo/Needs Decision statements above are historical snapshots,
not the current board authority. No board write, registration or access change
was attempted. Owner approval of this candidate must identify any newer
conflicting board decision before execution.

| Existing interface | Reuse and remaining boundary |
| --- | --- |
| P3-A `prepare_dispatch`, `claim_dispatch`, `prune_expired` | Actual private P2 pending bytes, original provenance/age and one-use final admission. B may wrap these; P1/P2/A source stays read-only. |
| `SingleRepeaterAdmissionGate` | Post-authenticated/new-key bounded accounting seam. P2 persists denial/uncertainty through the unchanged replay coordinator. It is not itself a ledger. |
| Existing policy / fixed queue | N=1..8, frame ceiling 255, original age and packet-rate checks already exist. Rate-window counters are not durable source or airtime allowances. |
| `calculate_lora_airtime` | Existing pure integer helper calculates a cost; no debit, refill, attempt or completion ownership. Unchanged four-group suite passes. |
| `RadioTransport::send` / `SendResult` | Copies/queues opaque bytes and reports accepted bytes; no exact attempt terminal, no-emission or rearm proof. Do not adapt it by guessing completion. |
| Evaluation driver / priority delivery / logger | Not admitted B owners. Reuse would introduce unapproved evaluation authority, retry/priority policy or privacy assumptions. |

### Proposed finite host profile requiring approval

All numbers below are synthetic fixtures or existing host ceilings. None selects
production radio, legal airtime, installation, power, RAM or timeout values.
They refine the already proposed B table, not D1-D7 or Packet-v1.

| Item | Exact proposed host rule / boundary fixture |
| --- | --- |
| FIFO and bytes | Core N=2, M=255, logical occupancy byte cap L=N*M=510; boundary N=1/8 and M=1/255. Count queued + P2 pending + B in-flight ownership against N. Refuse eligible new admission while pending/in-flight/rearm exists through replay persistence. No second FIFO. |
| Roster and counts | Exactly K=2 explicitly seeded synthetic verified source entries, S=1 admission credit/source and G=2 global credits/budget epoch. Boundary K=1/full/K+1, zero/overflow/mismatched configurations refuse; no allocation, eviction or borrowing. No caller-parsed payload identity. |
| PHY and cost | Existing reference-vector settings: bandwidth 125000 Hz, preamble 8, SF7, coding-rate denominator 5, explicit header and payload CRC true, LDRO false. Maximum-frame C=calculate_lora_airtime(profile,255)=399616 us, verified by unchanged helper. A fixtures 399615/399616/399617/799232 us (C-1/C/C+1/2C). Full C is charged even for shorter frames. Invalid/zero/overflow cost refuses. |
| Time | Queue-age T=100 ms and authority/consent deadline 10000 ms reuse synthetic P2 fixtures. Proposed terminal and rearm timeout interval H=1000 ms is a host fault-injection value only: test H-1/H/H+1. Intervals are positive finite configuration, checked addition/subtraction and nondecreasing logical time required; equality expires. No hidden wall-clock or default timeout. |
| Fixed memory | Charge the actual full eight-slot FIFO regardless of N, private P2 scratch/pending, one radio-port copy <=255 bytes, K=2 records, one attempt/receipt and fixed diagnostics. Keep the known P3-A host sizeof=4840 bytes separate from B's future measured sizeof; raw forwarder also has a 255-byte local snapshot. L limits logical occupied payload, not total allocated RAM. Record actual aggregate/static and bounded transient copies under final compiler; no arbitrary target byte ceiling is selected and no steady-state heap/event backlog is permitted. |
| Work / diagnostics | At most one candidate/claim/submit, one corresponding ledger transaction per phase and one terminal/rearm event per public service call. Expiry visits at most eight heads; no internal drain loop. Fixed uint32 saturating counters + overflow flag/coarse fault; event-record capacity zero, no source/payload/key/location history or logger/export. Diagnostic clear cannot alter authority/replay/ledger/attempt state. |

Nominal synthetic terminal success must occur no earlier than the modeled
maximum-frame cost (ceil(C/1000)=400 ms after claim), then explicit rearm; H=1000
ms leaves room within the modeled terminal deadline. Test late/missing completion
separately. With 2C, admit a fresh second-source frame after the first terminal and
rearm, rather than silently extending a previously queued frame beyond T=100 ms.
This is deterministic host timing, not measured RF behavior.

The exact timeout fixture H is a newly stated proposal, not an existing approved
requirement. No implementation may treat this table as acceptance by proximity.

### Proposed ledger, attempt and cleanup contract requiring approval

1. B is nonmovable and privately owns actual P2 and its gate. It exposes guarded
   lifecycle/admission/service wrappers, never a raw mutable P2 escape. External
   authority/role/replay/ledger/radio ports outlive the entire graph; finite,
   serialized, nonthrowing calls and reentrant refusal are required.
2. The synthetic ledger is preseeded by explicit trusted host fixtures, never
   initialized or replenished by B. Recover exact retained records before
   readiness. Domain/budget-epoch/ledger sequence and source/context bindings are
   explicit, nonzero and nonwrapping; missing/damaged/legacy/mismatched/uncertain
   state stays NOT_READY. Global credits/airtime survive P2 instance, consent,
   lifecycle/context changes and reconstruction. No refill API, epoch rollover,
   quiet-period reset, fresh-credit fallback or spent-entry eviction in this slice.
3. Source/global admission and airtime are distinct irreversible transactions.
   Gate commits/readback-verifies source/global debit for eligible new keys;
   actual coordinator then persists replay, including denials. Later queue/rate
   refusal or replay-save failure refunds nothing. Airtime full C commits and
   exactly reads back a bound receipt before P2 final claim. Compare expected
   prior sequence/domain/remaining amounts and full resulting record; partial,
   unknown-commit or readback mismatch contains before any submit. No retry to
   resolve an uncertain transaction. Ordinary definite exhaustion refuses safely.
4. P2 remains the final authority/age/one-use claim owner. A budget receipt is
   never a transmission grant. Before claim, authority loss means zero submits;
   after the linearization point at most one possible emission is truthfully
   recorded. After ledger/provider callbacks and immediately before P2 claim,
   recheck the originally bound driver generation and quiescent receive-ready
   proof; changed/unknown proof means zero submits with spent debit retained.
   P2 validates lifecycle/age, not radio readiness. Inside the claimed callback,
   the synthetic submit port must atomically enforce that same expected driver/
   readiness tuple before accepting its copy; a changed tuple gives zero submits
   even though the P2 callback was invoked. B records these as distinct facts.
   Final-claim refusal retains consumed airtime. B's callback copies
   only the borrowed exact opaque bytes into the declared bounded host port.
5. One candidate creates at most one submit. Attempt ID, driver generation,
   original P2 instance/lifecycle/candidate binding and budget domain/epoch/receipt
   sequence match every outcome/event. IDs never wrap or reuse. Outcomes are
   accepted-pending, definite-busy/reject, partial and uncertain; partial/unknown
   acceptance contains with possible emission. No retry or refund for any
   outcome, even definite rejection. Accepted bytes are not completion or ACK.
6. Matching terminal evidence must precede a separately bound successful receive
   rearm/readiness result. Definite rejection still requires explicit rearm
   evidence. Wrong/stale/duplicate events grant nothing; missing events expire
   at H and contain. No cooperative fake service may silently transmit/rearm.
   New instance recovery must retain radio-port uncertainty or present exact
   quiescent/terminal+rearm proof; a new default-ready fake is not evidence.
7. Expiry service remains bounded while ports are unavailable. No A interface
   drops only a live pending candidate: on exhausted airtime or failed final
   dispatch setup, B conservatively suspends/retires the whole volatile P2
   lifetime via existing notification, not a hidden new discard API. Extra
   unsent loss is accepted by this proposal; replay/source/airtime remain spent.
   Revocation/suspension/restart also retire all RAM and require explicit recovery
   plus fresh consent. Shutdown is terminal, no late ledger mutation/readiness;
   already emitted bytes cannot be recalled. No automatic restart/retry.
8. A contains no real completion or retained budget owner. B defines synthetic
   seams in its new header with fake implementations only in its host tests.
   There is no real radio adapter, protected-storage/key access or crypto claim.
   Any A API mismatch stops for owner scope review rather than modifying A.

### Exact proposed seven-file implementation grant

Approve these together with the preceding profile/contract; no other source file:

1. New `firmware/components/delivery/include/opentrail/repeater_dispatch_controller.hpp`
2. New `firmware/components/delivery/src/repeater_dispatch_controller.cpp`
3. New `tests/host/repeater_dispatch_controller_tests.cpp`
4. New `docs/protocol/REPEATER_HOST_DISPATCH_V0.md` (C++ host API only).
5. Existing `tools/Test-Host.ps1` — register one new composed executable only,
   linking actual P3-A owners and unchanged airtime helper.
6. This existing OT-0266b report.
7. Existing `docs/PROGRESS_LOG.md`

No four proposed new files are created now. No new task registration, publication,
P4/P5/P6 or production adapter is requested by this implementation grant.

### B acceptance matrix and approval request

F1-F12 retain the existing reviewed matrix labels; the following B execution criteria are
**proposed and NOT RUN**. The current preparation is not a dispatch test result.

| Cases | Required observable B pass/fail |
| --- | --- |
| F1/F2/F3 | Compose actual private P2/A: all nonactive/stale states zero submits, original FIFO age/exact bytes, shared N/L bounds with pending/inflight, pressure replay survives reconstruction, no second queue/heap. |
| F4 | K/S/G boundaries and unknown source refuse; no debit on unauthenticated/duplicate keys, no refund after queue/rate/save failure; new context/consent/instance never invents global credit or evicts spent source records. |
| F5/F6 | A=C-1 zero submit; A=C exactly one paid opportunity; C+1 leaves one us, 2C two independently paid opportunities only after terminal/rearm. Inject before-write/possible-commit/readback-error/mismatch in both ledger phases; zero submits without exact debit. Revoke at every callback boundary; preclaim zero, postclaim <=1. |
| F7/F8 | Busy/reject/partial/unknown never retry/refund. Accepted-pending waits for exact tuple terminal and explicit rearm; wrong/duplicate/stale/missing events never reopen. H-1/H/H+1, overflow/rollback and identity exhaustion refuse safely. |
| F9/F10 | Reconstruct actual P2 + retained ledger + radio port at each transaction/claim/possible-emission boundary, no default-ready/fresh-credit state. Entire volatile queue retires on dispatch budget exhaustion, uncertainty, revocation/restart/shutdown. Bounded expiry/control remains callable while waiting. |
| F11 | Fixed coarse saturated counters/overflow, zero event records/export. Privacy sentinels absent; clear leaves authority/replay/budget/attempt hashes unchanged; stopped object rejects late events without state, counter or ledger writes. |
| F12 | Existing 15-executable/128 reported cases plus unchanged four-group airtime suite and new B suite all pass strict compile. Exact seven-file scope, source/patch/input hashes and independent security/test review. Future totals recorded from actual runs only. |

**Exact next owner decision:** accept this P3-B synthetic profile and eight-part
ledger/claim/terminal/rearm/cleanup contract, including irreversible debit and
whole-volatile-queue retirement on dispatch budget exhaustion; authorize only the
seven files above for host-only implementation and validation. Confirm any newer
Project #9 decisions do not conflict. No production numeric budget or hardware
choice is needed for this synthetic slice. Until then implementation is BLOCKED.

Preparation evidence: `build/repeater-baseline/p3b-preparation-20261028` retains
preimages, P3-A verification, unchanged airtime suite/probe receipts and hashes,
independent reviews, documentation checks, incremental patch and final receipt.
The probe merely measures an existing pure helper; it is not B implementation.
**Housekeeping:** removed nothing; no cleanup necessary. Existing detached
candidate and all thirteen prior dirty entries remain, with only report/progress
changed now. All old source/evidence remains retained; new task-owned evidence
is kept for review/reproduction. No worktree operation, unrelated cleanup or
cleanup needing owner approval. No V1 score/version/website status change.


## P3-B host dispatch implementation

Owner authorization October 28, 2026 accepts the P3-B contract, synthetic profile
and exact seven files above. Earlier BLOCKED/proposed sections are preserved as
historical preparation. This implementation does not authorize P4/P5/P6, target
adapters, production values, hardware, Packet-v1 changes or publication.

The candidate remains the existing isolated detached Repeater worktree at
`c96d93ea459a7c41cf453e27edea082985650fa6`. P3-A patch/source, 43 artifacts and
255 inputs verified before editing; the P3-B preparation receipt SHA-256
`f6058feaecb26f8ab1a549e42c394d148120833047aa794305e8ad92666e830f` and its
artifacts/current document hashes also matched. Three existing-file preimages,
four confirmed-absent new paths and a 3,077-entry preservation manifest are
retained under `build/repeater-baseline/p3b-implementation-20261028`.

Dedicated private Project #9 item 286137273 remains the mapped task authority;
the read-only GitHub API attempt returned HTTP403/rate-limit, so no current
remote status is claimed. The explicit human seven-file grant supplies local
execution authority. No GitHub record/status or access setting is changed.

### P3-B source and ownership boundary

Only these seven authorized paths change in this slice:

1. `firmware/components/delivery/include/opentrail/repeater_dispatch_controller.hpp`
2. `firmware/components/delivery/src/repeater_dispatch_controller.cpp`
3. `tests/host/repeater_dispatch_controller_tests.cpp`
4. `docs/protocol/REPEATER_HOST_DISPATCH_V0.md`
5. `tools/Test-Host.ps1` — only one new composed host executable entry.
6. This existing OT-0266b report.
7. `docs/PROGRESS_LOG.md`

The [host dispatch contract](../protocol/REPEATER_HOST_DISPATCH_V0.md) describes
private P2/gate composition, retained ledger and synthetic radio ownership. The
forwarder remains the sole FIFO; P2 owns original metadata/age, pending bytes and
final one-use claim. B supplies no new payload parser, protected storage schema,
endpoint key access, real radio adapter, production target or priority/retry path.


### P3-B dispatch and budget behavior

B privately owns actual P2 and its admission gate; existing forwarding, replay,
checkpoint and lifecycle sources/tests are byte-preserved. The fixed roster
admits at most one opportunity per source and two globally. Source/global debit
is an exact full-record compare-exchange plus readback before actual replay save;
both are spent even if later queue/rate limits refuse. Unknown sources allocate
nothing. Pending/in-flight/rearm pressure reaches the real replay gate and stays
duplicate after reconstruction.

The unchanged reference helper charges full maximum-frame 399616 us even for
shorter frames or M=1 input ceilings. Airtime is debited/readback-verified before
one P2 claim. No refund/refill/retry exists, including definite rejection or
failed claim. Exhausted airtime retires the entire volatile P2 lifetime. Stored
records remain spent; missing, malformed, wrong-domain or uncertain state never
initializes a new allowance. Actual reconstructed owners reuse retained fake
ledger/replay/port state and fresh explicit lifecycle recovery/consent.

One original attempt binds lineage/driver/readiness/quiescence, full P2 ticket
and budget domain/epoch/receipt. Original proof is rechecked after ledger
callbacks and atomically enforced by submit. Callback invocation, port request,
accepted copy and possible emission are distinct. Accepted-pending needs exact
terminal evidence, then explicit advanced receive rearm. Busy/reject with zero
accepted bytes supplies the explicit no-emission terminal boundary but still
requires rearm. Partial, contradictory or unknown outcomes contain; no same
object resubmission. Each H=1000-ms phase retains its own deadline; equality,
rollback and overflow refuse safely. Nominal completion is >=400 ms after claim.

The retained synthetic port now has bounded readiness invalidation. Submission,
uncertain notifications, timeout and unresolved shutdown cannot leave an old
ready proof available for reconstruction. An independent reconciliation-required
latch covers pre-submit unavailable/rearm faults without inventing an RF-emission
claim. Same-object recovery requires advanced proof, and rereads it after P2
callbacks. A new B still relies on the trusted retained-port producer; this is
not protected antirollback or production persistence. No payload outbox is added.

Logical occupancy counts the one candidate once during synchronous P2-to-port
copy; both fixed buffers are charged to memory. Status byte charge is conservative
occupied*M, not summed actual lengths. The strict host ABI reports B=5624 bytes,
ledger=120, attempt=200 and status=128; P2's in-object cost is included in B. Caller
radio storage adds its fixed255-byte copy; existing forwarder admission adds a
fixed255-byte local stack snapshot, alongside finite local record/ticket copies.
These measurements do not select or qualify a target RAM/stack budget.

Diagnostics contain coarse state/occupancy, saturated numeric counters and an
overflow flag only. There is no event-record history, logger/export or source/
protected-payload field. Diagnostic clear leaves security state untouched;
terminal shutdown rejects late operations without changing counters, ledger,
replay or port invalidation. Privacy testing covers this typed API projection,
not a broad telemetry or physical side-channel assessment.

### P3-B final host validation and independent review

Authoritative final strict GCC16.1.0 C++17 matrix passed **17 executables / 147
reported scenarios or groups = 128 prior + four existing airtime + fifteen B**.
All compile/run exits are zero and compiler/runtime stderr is empty. Source
hashes remained stable across the run. Existing forwarder12, replay11 and
lifecycle19 groups passed unchanged. New runner registration is one entry only;
the evidence-local recipe selects those existing suites plus B and airtime.

| Group | Exercised contract/fault behavior |
| --- | --- |
| B1 | Actual P2 startup/admission, exact opaque bytes, full-C debit order, terminal >=400ms and explicit rearm. |
| B2 | Source/global exhaustion independently, unknown/full roster, duplicate/invalid input and no debit/refill. |
| B3 | A=C-1/C/C+1/2C and whole volatile queue retirement on exhaustion. |
| B4 | Both transaction phases: before-write, possible-commit, readback failure/mismatch; actual B destruction/reconstruction preserves each retained state, duplicate replay and no extra CAS/submit. Replay-save failure is separately contained. |
| B5 | In-flight/rearm pressure persists denied replay, and actual supervisor reconstruction retains it. |
| B6 | Authority and original driver proof changes during ledger/claim callbacks; atomic tuple refusal cannot be counted as accepted bytes. |
| B7 | Busy/reject/partial/uncertain/unknown or contradictory results preserve debit and never retry. |
| B8 | All 28 attempt binding fields corrupted independently for terminal and rearm, stale/duplicate/order refusal and one-use events. |
| B9 | H-1/H/H+1, too-early terminal, failed rearm and original phase deadlines. |
| B10 | Actual retained ledger/radio/replay reconstruction; no fresh credit/default ready; pre-submit uncertain/unavailable/rearm notification invalidates port; same old tuple cannot recover until proof advances. |
| B11 | Suspension/revocation/expiry/shutdown cleanup; unavailable radio still permits expiry pruning without airtime debit or submit. |
| B12 | Ten mutators refuse callback reentry, read-only occupancy counts one candidate, and two-source admit/debit/service/terminal/rearm hot path allocates no heap. |
| B13 | N=1/8, M=1/255, roster/config/identity exhaustion; actual queue-full and rate-limited denial after ledger debit preserve spent allowance and replay across reconstruction. |
| B14 | Typed diagnostic privacy/saturation/clear and immutable terminal shutdown/late events. |
| B15 | Logical rollback and checked deadline arithmetic/overflow refuse without new attempt. |

Independent security review cleared the retained-uncertainty hardening and test
boundaries. Test review prompted real-B rate-denial, both-phase reconstruction
and unavailable-radio expiry fixtures; all are included in the final frozen run.
Implementation review also added lineage binding, unknown-outcome possible-
emission truth, pre-callback conservative emission status, retained port
invalidation/fresh-proof recovery and removed an unused refusal counter. These
are bounded corrections within B's approved synthetic port/ownership contract.
No A/P2 file scope was expanded.

Focused attempts1-4 are retained as passing intermediate evidence, including
source changes/fixture extensions between attempts. Final matrix inputs, not an
earlier focused receipt, bind the complete accepted run. Documentation checker,
all eighteen documentation tests, added local links/anchors, incremental whitespace
and exact scope/preservation checks are recorded in final `verification.json`.

### P3-B evidence, remaining gates and housekeeping

`build/repeater-baseline/p3b-implementation-20261028` retains before/preimages,
API authority-read refusal, core/focused compile receipts, all four composed
focused attempts, final matrix executables/results/source hashes/toolchain,
documentation/review receipts, `p3b-implementation.patch` and final verification
receipt. The receipt binds exact seven changed-file, patch and artifact hashes;
its own SHA-256 is reported separately to avoid a self-reference. Prior P1/P2/A
and B preparation evidence is preserved.

P4 real relay-capable authentication, protected ledger/role storage, trusted time
and rollback/key custody remain open. P5 target/driver copy/lifetime/retry/rearm,
region/antenna/PHY, power profiles, physical cuts, RAM/latency/wear and duration
remain unselected/unverified. P6 physical, deployment/support and release gates
remain separate. No production budget, radio activation, multi-hop topology,
version/V1 score or public website status change is claimed. No commits, merge,
push, GitHub status change or P4/P5/P6 work was performed.

**Housekeeping:** no files removed and no cleanup necessary. The existing detached
candidate remains at the same revision, with seventeen dirty entries: seven
current files plus ten preserved prior dirty entries outside this scope. No
other worktree or source checkout was modified. All task-owned preimages,
source/patch/manifests, executables, intermediate receipts and security/recovery
evidence are retained for review/reproduction. No cleanup needs owner approval.
P3-B stops here; later security/physical tasks need their own explicit authority.


## P1-P3 consolidated integration candidate

Consolidated October 28, 2026 under the explicit host-only integration task.
Current candidate is the existing Git-managed detached worktree
`C:\Projects\LimitedUnderground\.worktrees\repeater-host-baseline-20261027`,
base `c96d93ea459a7c41cf453e27edea082985650fa6`. Common Git is
`C:\Projects\LimitedUnderground\Trail\.git`; this does not select Trail Live
as the integration workspace. No new worktree, commit, merge, push, hardware or
production activation is performed. Only this report and the existing progress
log change during consolidation; all runtime sources/tests remain byte-preserved.
The consolidated patch includes all seventeen existing candidate paths, including
preserved planning/handoff changes and eight previously untracked source/contract
files. No prior dirty file is discarded or silently omitted from recovery.

**Corrected current result: 17 executables PASS / 137 reported scenarios or
groups.** This supersedes earlier aggregate count labels in this report and
receipts, without rewriting their historical bytes. Independent integration
review identified an inherited ten-group arithmetic overcount. The old 147 total
was incorrect; no test failure, removed test or source regression caused this
correction. Counts are suite PASS summaries, not nested assertions, parameter
iterations or physical trials. Two suites explicitly report one integration
scenario each. All current compile/run exits are zero and stderr is empty.

### Corrected evidence lineage and fresh matrix

Eight retained prior verification receipts and all 196 listed artifact hashes
matched. Previously recorded implementation receipt anchors and latest seventeen
candidate hashes matched; all 260 final B matrix inputs were present together.
The fresh consolidated matrix reran the complete affected set with unchanged
GCC16.1.0 C++17 strict flags `-Wall -Wextra -Wpedantic -Werror -O2`; its inputs
remained stable. This is the complete affected Repeater matrix, not every suite
in the much broader repository-wide Windows wrapper.

| Stage | Executables passing | Corrected reported scenarios/groups | Old aggregate label (superseded) |
| --- | --- | --- | --- |
| Original host baseline | 14 | 94 | 104 |
| P1 | 14 | 96 | 106 |
| P2 | 15 | 108 | 118 |
| P3-A | 15 | 118 | 128 |
| P3-B | 17 | 137 | 147 |
| Fresh consolidated integration | 17 | 137 | No old label reused |

| Fresh suite | Reported count |
| --- | --- |
| Radio transport | 8 |
| Packet codec | 6 |
| Packet/transport integration | 2 |
| Delivery controller | 9 |
| Duplicate window | 6 |
| Duplicate checkpoint codec | 7 |
| Duplicate checkpoint store | 10 |
| Duplicate checkpoint key/value storage adapter | 9 |
| Lost-ACK delivery integration | 1 |
| Controlled forwarding | 8 |
| Immutable single repeater | 12 |
| Reboot-safe replay coordinator | 11 |
| P2/P3-A lifecycle supervision | 19 |
| P3-B dispatch/resource accounting | 15 |
| Priority queue regression | 9 |
| Emergency priority/delivery integration | 1 |
| Existing LoRa airtime helper | 4 |
| **Total** | **137** |

`count-audit.json` records all six stages' parsed PASS summaries and the explicit
singleton counting rule. Historical manifests/receipts remain immutable recovery
and execution evidence; hash verification does not endorse their old total fields.
No V1 score or public website capability was derived from these host counts.

### Integrated ownership and recovery findings

| Boundary | Actual owner and verified cross-component behavior |
| --- | --- |
| One FIFO | `SingleRepeaterForwarder` owns the sole eight-slot fixed queue; no B payload queue. Verified metadata and exact opaque bytes retain original enqueue/age. |
| Replay persistence | The existing coordinator saves/readbacks every eligible new observation, including queue/rate/gate pressure denial, before release. Save failure contains and retires volatile P2 work. |
| Pending and final claim | P2 privately owns one pending frame and original authority/provenance; consumes its ticket before one bounded callback. Stale/tampered/reentrant claims cannot bypass admission. |
| Resource accounting | B's private gate debits one/source and two global credits; a separate exact full-C airtime debit/readback precedes P2 claim. No refund/refill/retry on failure, uncertainty or reconstruction. |
| Radio handoff | Atomic synthetic port proof prevents stale driver/readiness/lineage acceptance. Callback, request, accepted bytes and possible emission remain distinct; exact terminal then explicit rearm is required. |
| Restart / uncertainty | Actual B/P2 reconstruction retains ledger/replay/port evidence, loses unsent RAM and requires explicit recovery plus fresh consent. Invalidation and reconciliation latch prevent stale ready-proof restoration. |
| Expiry / retirement | Original age and grant deadlines cannot renew. Unavailable radio still permits expiry pruning. Budget exhaustion, suspend/revoke/fault and shutdown retire whole volatile P2 work. |
| Terminal closure | Stopped instances reject late callbacks/events without reopening readiness or changing ledger/counters. New instances need separately admitted lifecycle and verified retained state. |

The fifteen B groups already compose actual P2/A/replay, including both ledger
fault phases across destruction/reconstruction, persisted rate/busy denial,
readiness races, all event bindings and full two-source no-allocation hot path.
Independent reviews found no missing integration fixture requiring source edits.
The old priority suites remain regressions only; B does not import priority or
retry behavior. Protected payloads are neither parsed nor modified; endpoint
keys, Packet-v1 and the accepted two-node V1 production boundary are unchanged.

### Reproduce the accepted integration set

Run from the selected candidate root, with Python3 and installed MSYS2 UCRT64
GCC16.1.0 (`C:\msys64\ucrt64\bin\g++.exe`) available. Do not move SDKs or
write into Trail Live. The task-owned standalone `reproduce.py` reads only tracked
`tools/Test-Host.ps1` source recipes and current project inputs; it has no
runtime dependency on earlier ignored baseline scripts. Its seventeen compiler/
source recipes were checked against every command in the fresh executed matrix.
The output directory must be fresh; it refuses an existing destination.

```powershell
$repo = (Get-Location).Path
python -B build/repeater-baseline/p1-p3-integration-20261028/reproduce.py --root "$repo" --output build/repeater-baseline/p1-p3-integration-20261028/rerun-1
```

Use a new task-specific output name for another run; never overwrite accepted
receipts. The canonical full-repository command is `& .\tools\Test-Host.ps1`
(`OPENTRAIL_MSYS2_ROOT` can select the installed root). That wrapper includes
many additional CMake/Windows-loader/security/structural dependencies and was
not run as part of this seventeen-suite acceptance. Its whole-repository result
must not be inferred from this matrix. The focused driver records project input
hashes plus its own recipe hash separately; the executed historical driver also
records its ignored baseline-script provenance among the 260 inputs. That old
helper is an evidence dependency, not an extra production source requirement.

### Consolidated patch recovery and runtime restart instructions

`build/repeater-baseline/p1-p3-integration-20261028` holds `consolidated.patch`,
`source-manifest.json`, `patch-recovery-check.json`, `count-audit.json`, fresh
matrix, standalone reproduction recipe, reviews and final verification receipt.
The source manifest binds exact base, all seventeen candidate file SHA-256/blob
identities, project/matrix inputs, prior receipt hashes and patch hash. The final
verification receipt additionally binds current evidence artifacts; its own hash
is reported separately to avoid self-reference.

Patch validation uses an alternate index and object directory entirely under
this task's `patch-check` directory, with canonical objects available read-only.
It stages raw blobs without filters, emits a full-index/binary patch against the
exact base, resets only the alternate index and applies the patch there. All
seventeen restored blob SHA-256 values must exactly match current raw files,
including LF/CRLF bytes. The actual candidate and canonical Live index hashes
must remain unchanged. No commit, ref, real checkout or real index is written.
The retained alternate Git data is patch-check evidence, not a managed worktree.

For later recovery, use only a separately owner-selected disposable checkout at
that exact base, with no unrelated changes. Never apply over Trail Live or an
active session. Validate `git apply --check --binary <consolidated.patch>` first,
then apply and compare all seventeen files to the source manifest. Stop on any
base/hash/line-ending mismatch rather than normalizing evidence silently. Copy
the sealed standalone recipe from this evidence bundle and use a fresh output
directory to rerun the accepted matrix. Prior evidence remains at its original
hash-bound locations; the consolidated patch is source/handoff recovery, not a
claim to regenerate historical binary hashes across arbitrary toolchains.

Runtime reconstruction instructions are distinct from Git recovery: preserve
actual retained role/replay/ledger and radio-port objects; discard volatile
queued/pending frames; read exact current ledger/domain/epoch and reconcile
uncertain port ownership. Require advanced verified quiescence (or exact
terminal+rearm reconciliation), then explicit authenticated-host recovery and
fresh context-bound consent. A saved-but-unsent key stays spent even though its
payload is lost. Never seed missing media, refill credit, restore an old ready
Boolean or resubmit an old claim to make recovery succeed. Synthetic stores and
trusted port proof producers remain host-test seams, not qualified target stores.

### Next bounded task and remaining gates

The smallest existing hardware-independent successor is the proposed **P4 shared
protected-adapter/state-binding decision packet**, after separate owner approval
of its existing four-document scope: this report, `tasks/OPTIONAL_PRODUCTS_PLAN.md`,
`tasks/BACKLOG.md` and `docs/PROGRESS_LOG.md`. This consolidation only identifies
that task; no P4 work or new GitHub task/status is performed.

Its measurable deliverable is a shared Trail security-owner decision identifying
an actually relay-eligible credential/verifier, authoritative role/replay/budget
storage owner, atomicity/retention/rollback/boot-time guarantees and retained radio
reconciliation producer. Enumerate interrupted-write/restart cuts and expected
NOT_READY behavior, plus exact future adapter/store files and vectors for later
approval. If no accepted construction supports relay, report that blocker to the
shared owner; do not invent a competing protocol or admit direct-only OTSL0.
No adapter, key, storage schema or protected-media implementation is authorized.

Production authentication/antirollback/storage, target/radio semantics, power-cut
behavior, region/antenna/PHY, measured RAM/latency/wear/energy, installation/support
and release gates remain open. Fixture budgets remain synthetic. No target, RF,
physical, production mesh, V1 score, version or public website status change.
Dedicated private Repeater Project #9 remains the mapped task authority; local
explicit authorization governs this consolidation, with current private-board
read access unavailable and no remote write or status claim.

**Housekeeping:** temporary files removed: none; no cleanup necessary. Retain
prior evidence and this task's preimages, raw patch-check objects/index, recipes,
executables, manifests, count correction and review receipts for reproducibility.
Worktree: existing detached candidate, seventeen dirty paths retained; only the
two maintained documents edited now. Evidence and recovery material preserved;
no source rewrite, other-worktree mutation or worktree removal. Cleanup requiring
owner approval: none; no retirement is proposed.


## P4 security and storage decision packet

Prepared October 28, 2026 under the explicit bounded P4 preparation task and its
existing four-document scope: this report, `tasks/OPTIONAL_PRODUCTS_PLAN.md`,
`tasks/BACKLOG.md` and `docs/PROGRESS_LOG.md`. No security/storage implementation,
new authentication framework, protocol/schema change, target or device operation
is performed. Recommendations below require named owner acceptance; preparation
is not production admission. **READY for owner/security review; BLOCKED for a
production relay verifier or protected-store adapter implementation.**

### Verified input and authority boundary

The consolidated candidate remains the same detached Repeater worktree at
`c96d93ea459a7c41cf453e27edea082985650fa6`. Its receipt SHA-256
`082b822cb51c1e02fadc37cdf8b3653c14c88884f93307b3534a6bc446b7c2dd`,
source manifest `b5eb081051c637ea44b059c1a01988c0381e5da873a38066d3f2dad56786a6ee`
and patch `40cd78a4cb8721bfb4e1a7cc7bb624802461d21d9baaff1cc2fef3db11d8bc2f`
matched. All seventeen candidate file hashes, 52 consolidation artifacts, eight
earlier receipts/196 listed artifacts and 260 matrix inputs matched before
preparation. The accepted host count is **17 executables / 137 reported groups**;
the historical count correction above remains authoritative. Runtime results
are reused after hash verification, not rerun or credited as new P4 tests.

Dedicated private Repeater Project #9/item286137273 is the maintained mapping;
read-only API access again returned HTTP403/rate-limit. Current remote status
was not verified. The current human grant controls local preparation only;
no board registration/status or other-worktree modification is performed; only
read-only canonical status/index preservation checks are used.

### Authoritative shared contracts and reuse inventory

| Existing owner / source | Reuse without modification | Explicit limit |
| --- | --- | --- |
| [Decision0033](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md), [Decision0035](../decisions/0035-host-tested-secure-lora-key-transport-contract.md), [Decision0036](../decisions/0036-post-v2-public-lane-and-assistance-direction.md), [OT-0265a review](OT-0265a-REPEATER-SECURITY-REVIEW-2026-09-29.md) | Existing topology, identity/key separation, source/membership/permission and immutable forwarding requirements remain controlling. | Current OTSL0/v0 is exact pairwise direct traffic, not relay authority. No reserved-bit reinterpretation, key export or invented wrapper. |
| [EnrollmentIdentityVerifier](../../firmware/components/security_evaluation/include/opentrail/enrollment_identity_binding.hpp), [ProductEnrollmentActivation](../../firmware/components/security_evaluation/include/opentrail/product_enrollment_activation.hpp), [independent traffic endpoint](../../firmware/components/security_evaluation/include/opentrail/independent_peer_traffic_endpoint.hpp) | Reuse existing verifier/activation ownership and negative-case patterns where their accepted scope applies; verification roots are externally trusted pins, not keys selected by a received proof. | Host/evaluation compositions are not a product trust-root producer, current-membership proof by themselves, relay-object verifier or retained traffic-key resume mechanism. Activation receipts are not traffic keys. |
| [Traffic context encoder](../../firmware/components/security/include/opentrail/traffic_key_context.hpp), identity/group/epoch owners | Existing full-identity/purpose/domain separation remains authoritative. | Public context encoding is not a KDF or authentication proof; it does not alone bind destination. Short aliases are not full trusted identities. |
| [Replay coordinator](../protocol/SINGLE_REPEATER_REPLAY_COORDINATOR_V0.md), [checkpoint KV adapter](../../firmware/components/delivery/include/opentrail/duplicate_checkpoint_kv_storage.hpp), P2/A/B owners | Keep actual save/readback, first-use/known-degradation recovery, opaque FIFO/pending custody, one-use claim and irreversible ledger/port semantics unchanged. | ODS0/v1 is 704 bytes with CRC, not authenticated or rollback-proof storage. P2 role and B ledger are ordinary host records with different interfaces, not selected target schemas. |
| [PersistentStorageKv](../../firmware/components/persistence/include/opentrail/persistent_storage_kv.hpp), [target boundary](../persistence/PERSISTENT_STORAGE_KV_TARGET_ADAPTER_V0.md) | Reuse stage/commit/durable-read/error/namespace-isolation patterns under the actual owner. | Five fixed domains use 64-byte slots; names do not provide confidentiality, authenticity or freshness. Do not squeeze 704-byte replay or host-ABI120-byte ledger into them, or serialize padded C++ structs as a new format. No protected backend is selected. |
| [Companion protected store](../../firmware/components/companion/include/opentrail/companion_authorization_protected_store.hpp), [KV media](../../firmware/components/companion/include/opentrail/companion_authorization_protected_kv_media.hpp), [storage contract](../platform/COMPANION_AUTHORIZATION_STORAGE_V0.md) | Existing two-slot/full-record verification and independent-floor sequencing are reusable engineering patterns. | OAP0/v0 is companion-specific; ot_auth/ot_owner/oap_slot_a/b are not Repeater slots. The implementation and namespace do not supply encryption, authenticity, floor hardware or target admission. Do not copy that record/backend as a generic budget store. |
| [Membership store](../../firmware/components/security_evaluation/include/opentrail/peer_membership_store.hpp), [session generation allocator](../../firmware/components/security_evaluation/include/opentrail/session_generation_storage.hpp) | Reuse public-metadata/generation consumption and failure patterns within evaluation scope. | CRC is not hostile rollback defense, and retained membership/session metadata cannot resume old secrets or grant current relay membership. |
| [Decision0103](../decisions/0103-adopt-ot168-v1-factory-reset-and-boot-pairing.md), [factory-reset v1](../platform/DEVICE_FACTORY_RESET_V1.md) | Existing authenticated/physical reset authority, complete user-domain wipe and verified-unowned boot remain controlling. | P4 changes no reset/pairing semantics and does not demand retention of user state the reset contract requires erased. A reset is not implicit Repeater enrollment or fresh-budget authorization. |
| [RadioTransport](../../firmware/components/radio/include/opentrail/radio_transport.hpp), [PairRadioDriver](../../firmware/components/security_evaluation/include/opentrail/pair_radio_driver.hpp), [B host contract](../protocol/REPEATER_HOST_DISPATCH_V0.md) | Existing opaque transport boundary and B proof/invalidation/terminal/rearm contract remain read-only inputs. | send/status proves queue acceptance only; PairRadioDriver is exclusive evaluation machinery, not an accepted Repeater producer. Physical readiness is not session authority. |

The selected baseline's experimental `packet_codec.hpp` v0/CRC is not a
Packet-v1 security verifier. Other active Packet-v1 worktrees are not inspected
or modified by P4. Shared Trail protocol/security owners retain the only authority
to identify an accepted compatible construction. No current selected-candidate
component supplies production relay authentication to `verify_frame`.

### Recommended owner decisions

All P4-R labels below are decision labels, not newly registered tasks. D1-D7,
Packet-v1 and accepted P1-P3 behavior remain unchanged.

| Decision | Recommended disposition | Required approving owner / unresolved gate |
| --- | --- | --- |
| P4-R1 Relay eligibility | Keep production relay admission disabled (`NO_RELAY_AUTH_ADAPTER`) until the shared owner supplies an accepted source-verifiable protected-object contract with current membership/epoch and immutable forwarding permission over exact bytes. OTSL0/direct endpoint sessions and group symmetric-key possession cannot provide that permission. | Shared Trail protocol/security owner must identify the actual approved construction and compatibility evidence. If a wire/Packet-v1 change is required, stop and return to that owner; P4 approves no change or algorithm. |
| P4-R2 Trusted authority producers | Local device identity comes from its provisioned shared identity owner; peer identity from pinned verification roots plus current activated membership; group/epoch from the current durable membership/rekey owner; role from explicit authorized Repeater enrollment; operator request/consent from the authenticated control owner. Only their typed current results may populate P2 handles/snapshots/verified metadata. | Owner names concrete shared APIs, freshness/serialization/lifetime rules and revocation producer. BLE bond, phone request, alias, packet parse, host Boolean or old verification result alone is insufficient. No production provider is selected by this packet. |
| P4-R3 Key custody | Repeater receives only approved nonsecret verification material, opaque references and its own separately provisioned local credential handles if required. Endpoint plaintext and endpoint private/traffic/ACK secrets never cross into Repeater. Existing shared key owner performs any allowed cryptographic operation; no Repeater credential/KDF/key-export framework. | Shared security/key-custody owner defines any local relay credential issuance/use/revocation and verification-root provisioning. No endpoint-key copying, cipher/signature/wrapper selection, raw-key provisioning or access code authorized. |
| P4-R4 Durable state owner | Require one explicitly named shared persistence/protection owner to bind device, role generation, context/epoch, replay-retention policy and stable budget domain/epoch/sequence. Preserve separate role/replay/ledger transaction semantics; do not claim global atomicity. Use existing owners unchanged behind the eventual approved backend. | Shared persistence/security owner must supply the concrete API, exact domain/namespace/record ownership, full-record CAS/readback, locking, error taxonomy, integrity/access/confidentiality policy and cross-record cut recovery. No generic protected B-ledger API currently exists in this candidate. |
| P4-R5 Rollback threat | Until the Repeater threat profile is explicitly chosen, make no hostile-flash rollback-resistance claim and keep production storage admission blocked. Require refusal for every detectable stale/lower/conflicting generation and no software/ordinary-restart reset of replay or credit. | Owner must explicitly choose a disclosed practical physical-writing limitation or stronger authenticated freshness outside the rollback domain. Decision0033 permits the former for base V1 and supersedes Decision0028's hardware-floor prerequisite there; that does not silently select an unattended Repeater threat profile. A secure element/eFuse provider is not selected or made a new base-V1 gate. |
| P4-R6 Time, consent and boot | Persist role/disabled state and consumed opportunities, not an auto-resume consent Boolean. Each boot starts disabled; recover verified retained state and obtain fresh context/boot-bound request plus consent. Old deadlines/generations never become fresh merely because uptime restarted. | Shared authority/time owner must define nonreusing boot scope and trustworthy local monotonic observations or independent freshness. If elapsed time/continuity cannot be established, do not age spent protection away, resume an old lease or refill a budget; stay NOT_READY or retain conservative tombstones under an explicitly bounded approved policy. No RTC is presumed. |
| P4-R7 Radio reconciliation | Existing Trail radio/target owner supplies B's retained-port producer: exact copied-byte ownership, original lineage/generation/receipt/attempt tuples, explicit terminal/rearm and invalidation across supervisor loss. Unknown emission remains spent. Quiescent now never proves no earlier emission. | Owner must name the actual driver binding and evidence origin. No status/default-ready/receipt Boolean, ordinary storage flag or evaluation-driver reuse establishes this proof. Target behavior/hidden retries/buffer lifetime and power-cut reconciliation require P5 evidence. |
| P4-R8 Fail-closed recovery | Missing, corrupt, legacy, wrong-bound, revoked, time-uncertain or commit-uncertain authority/state grants no admission. Retire volatile work; preserve known spent state. Allow only existing exact-context first-use/known-slot repair with explicit authorization; never auto-erase, migrate, reinitialize or refund to recover availability. | Owners approve explicit reconciliation/service/reset procedures and evidence. Unavailable protection or unresolved cross-record cuts remain NOT_READY. Physical reset is not a software bypass to a fresh role or allowance; existing shared factory-reset/pairing semantics are not amended. |

Secrets excluded from Repeater include endpoint identity signing seeds/private
agreement keys, ephemeral handshake secrets, conversation roots, directional data
and ACK keys, nonce/counter-domain secret material, phone BLE bond/LTK material,
and endpoint plaintext. Public pins/fingerprints must still come from the trusted
provisioning owner, not the object being verified. Private references, metadata,
proofs and state images remain off ordinary diagnostics/export. No key material
was read from devices, copied or provisioned by this task.

### Proposed admission and persistence handoff

1. Authenticate the local operation through the existing control authority.
   Bind one-use request to exact device/instance/boot, context/epoch, role and
   original deadline before any first-use write or role mutation.
2. Obtain current activated peer/group/epoch and local Repeater role from the
   named shared owners. Revalidate original scope around every storage or
   cryptographic callback; revocation never adopts a newer implicit grant.
3. Verify the exact immutable protected bytes against the accepted relay-capable
   construction before producing source/destination/message/permission metadata.
   P2's private scratch remains the custody boundary. Unsupported/direct-only
   traffic receives zero production relay admission regardless of valid pairing.
4. Recover the actual role record, exact ODS0 replay context and stable B ledger,
   plus fresh boot/time and retained radio proof. Role plain read/write,
   replay two-slot save/readback and B full-record CAS are distinct transactions.
   A partial cut need not restore availability: it must never restore permission.
5. Preserve ordering: source/global debit may commit before replay save; replay
   save failure loses that opportunity without refund. Full airtime debit may
   commit before a refused claim; it stays spent. No recovery rolls either back
   to obtain a matching optimistic cross-record state. Disable/revoke uncertainty
   publishes no active authority. Unknown domain/generation agreement stops.
6. Require fresh consent only after all owners reconcile. Never reconstruct a
   saved-but-unsent payload or resume an old traffic session from a receipt.
   ODS0 remaining-lifetime restoration is conservative host duplicate retention,
   not a trusted cross-boot clock, cryptographic replay proof or renewable lease.
   A full bounded replay table without safe expiry authority requires refusal,
   not eviction of spent protection to improve availability.
7. Dispatch remains actual B/P2: exact ledger debit/readback, final claim and
   atomic expected radio proof. Driver copies bytes, then proves terminal and
   receive rearm under original deadlines; interruption/inconsistency invalidates
   retained readiness. No endpoint-generated ACK is forged by Repeater.

Normal restart/recovery and an explicitly authorized destructive factory reset
are different operations. Decision0103/factory-reset v1 owns the latter, including
wiping required user/cryptographic/replay state and verified-unowned boot. This
packet does not override that wipe or retain those user records against it.
After a verified reset, Repeater remains disabled until the shared owners grant
new enrollment/context, explicit first-provisioning and a separately authorized
budget domain/epoch. Missing media by itself proves none of those conditions;
old membership, consent or claims cannot resume. Binding the future Repeater
store into the existing reset owner is a separately scoped implementation gate.

Device identity, peer authenticity, current membership, role and storage integrity
are separate predicates. A successful peer handshake cannot supply local role,
a protected namespace cannot supply peer authenticity, and a fresh radio cannot
repair stale keys/time/state. The selected owners must jointly prove the original
binding, not manufacture those predicates from one general READY Boolean.

### Conditional smallest implementation successor and exact files

The smallest useful implementation is **one B-ledger port binding to one already
approved shared storage owner**, separately host-validated, with all P2/B/replay/
session/Packet definitions read-only. It is independent of choosing physical RF
hardware and must remain unable to enable production relay while P4-R1 is closed.
It must not combine credential issuance, role/replay redesign and a new storage
framework in one task.

Conditional proposed seven-file allowlist (paths reserved for review only):

1. New `firmware/components/delivery/include/opentrail/repeater_budget_storage_adapter.hpp`
2. New `firmware/components/delivery/src/repeater_budget_storage_adapter.cpp`
3. New `tests/host/repeater_budget_storage_adapter_tests.cpp`
4. New `docs/protocol/REPEATER_BUDGET_STORAGE_BINDING_V0.md` (host/store API contract, not wire format).
5. Existing `tools/Test-Host.ps1` — one host executable registration only.
6. This existing OT-0266b report.
7. Existing `docs/PROGRESS_LOG.md`

**Not executable or approved now.** No proposed file is created. Before the
owner can grant this scope, P4-R4/R5/R6 must name the actual shared store API,
record/domain/schema ownership, freshness/threat policy, transaction/error and
boot recovery semantics. Its API must already satisfy B's full-record read/CAS/
readback and no-refill contract. A target NVS namespace or an OAP0 class name is
not that API. If none exists, stop for a separately scoped shared-owner contract;
do not create a replacement in Repeater. Any shared-store, P2/B or wire change
needs its own exact allowlist and owner approval. The adapter may prove host
binding/failure behavior; it cannot independently certify protected media.

The current four-document preparation grant supplies no seven-file implementation
authority. Credentials/verifier and real radio bindings require separate named
owner scopes; they are deliberately excluded from this smallest storage slice.

### Proposed host acceptance and fault-injection plan

All P4 successor cases below are **NOT RUN**, pending approved shared inputs and
implementation authority. Existing P1-P3/137-group evidence is not cryptographic
or protected-storage acceptance.

| Case | Required observable pass/fail |
| --- | --- |
| S1 Credential boundary | Direct-only/unknown construction, unpinned roots, stale membership or a caller Boolean yields zero production P2 admission, ledger mutation or radio copy. Public proof cannot select its own trust root. |
| S2 Exact binding | Independently alter device/peer identity, group, epoch, role, source/destination, message/permission and one immutable byte/length: reject before granting their corresponding authority; no valid sibling resource is released for reuse. |
| S3 Storage selection | Wrong device/domain/context/budget epoch/schema, missing/legacy/corrupt/ahead/duplicate-current/conflicting media refuses without erase/migration/initialization/refill. Existing ODS0 byte format and companion namespaces stay unchanged. |
| S4 Ledger transaction | Compose the actual approved shared-store implementation with a synthetic backend, not a mock saved=true result: exact expected sequence/full record CAS/readback; before-stage/staged/possible-commit/readback-error/mismatch/freshness-update/post-update cuts. Zero submit without exact debit. |
| S5 Each owner and cross-record cut | Repeat relevant stage/commit/readback/floor cuts for role, replay and ledger separately; disable/revoke/new role versus old replay/budget conflict has explicit NOT_READY disposition. Never infer a global atomic transaction from three successes. |
| S6 Actual reconstruction | Destroy/recreate over the same retained images at every cut; spent replay/credit stays spent, disabled role stays disabled, old consent cannot resume, unsent frames remain lost. No extra CAS, refund, erase or resubmit during recovery. |
| S7 Time/rollback | Same/older boot scope, lower generations/floors, clock rollback, untrusted elapsed time, timeout equality and ID exhaustion refuse. Unknown offline duration cannot expire protection or replenish allowances; stronger hostile-flash claims require their separate trusted provider. |
| S8 Callback races | Revoke at request consumption, role write/readback, replay save, ledger CAS/readback and final claim. Preclaim zero submission; postclaim at most one truthful possible emission. Reentry cannot replace binding, perform a second transaction or clear uncertainty. |
| S9 Radio retention | Lost/partial/unknown submit, failed or missing terminal/rearm, same old ready tuple, supervisor loss and actual reconstruction remain blocked until explicit advanced retained reconciliation. Successful quiescence does not refund prior debit. |
| S10 Bounds/privacy | Fixed buffers/roster/work limits, no steady-state heap or hidden logger/export; diagnostics/clear leak no payload/key/private reference and cannot change protected authority, ledger or port state. Test images are synthetic only. |
| S11 Regression/scope | All seventeen existing executables/137 reported groups remain passing with stable inputs, plus the actual new adapter suite; strict builds, exact allowlist, documentary checks and independent shared-security/storage/test review. Record actual future counts. |

S1/S2/S5/S7/S9 span shared authority/radio owners and are review/acceptance gates;
they are not promises that the seven-file budget binding alone implements them.
That slice's executable subset is S3/S4/S6/S8/S10/S11 for the ledger owner only.
Other domains remain read-only until their own approved contract/scope exists.

### Hardware evidence and exact next owner approvals

Physical evidence remains required for actual key/root provisioning and access
policy, selected protected partition initialization, commit/power-cut durability,
wear/latency, any independent monotonic source and its rollback domain, entropy/
boot identity/time across resets, driver internal retry/copy/IRQ/FEM/stop/rearm
semantics, and region/power/environmental limits. Configuration flags or a named
secure element are not evidence that these properties hold. The practical and
stronger rollback profiles have different claims; neither is selected from a
host fake or imposed on base V1 by this packet.

Owner/security decisions requested now: accept P4-R1/R2/R3 authority/key barriers;
name the P4-R4 persistence API/owner and its cross-record recovery contract;
choose and disclose P4-R5 threat profile; name P4-R6 boot/time/consent authority
and P4-R7 retained radio producer; approve P4-R8 refusal/reconciliation policy.
Until these inputs are concrete, production authentication/storage/radio binding
remains BLOCKED. Once the shared storage inputs are accepted, review and
separately authorize only the conditional seven-file budget binding above.
No Packet-v1 change is requested; incompatibility returns to its existing owner.

### P4 preparation validation and housekeeping

Detailed source references, hashes, four preimages, incremental documentation
patch, authority-read refusal, independent reviews, documentary checks and final
preservation receipt are retained under
`build/repeater-baseline/p4-preparation-20261028`. P1-P3 runtime suites are reused
only after all260 inputs and their sealed evidence match; no P4 runtime fixture
or production security acceptance is claimed. No version/V1 score/public website
capability or GitHub status change. This packet is the sole new P4 decision
record; task/backlog entries link here rather than creating another framework.

**Housekeeping:** temporary files removed: no manual cleanup; only self-cleaning
documentation-test fixtures. Prior source, consolidated patch/manifest, all
historical receipts and recovery material are retained. The existing detached
worktree keeps seventeen dirty paths; only the four authorized documents change
now, with thirteen other dirty files byte-preserved. New task-owned preimages,
source-reference hashes, patch and receipts remain for review/reproduction. No
other worktree/Live index change or worktree removal; cleanup requiring owner
approval: none. Stop before any conditional successor implementation.


### P4 shared-security dependency reconciliation - 2026-10-28

Live coordination owner: [OT-0266b-4, Repeater Project 9](https://github.com/orgs/Limited-Underground/projects/9?pane=issue&itemId=286304467). Shared selection coordination: [OT-0237e, Trail Firmware Project 1](https://github.com/orgs/Limited-Underground/projects/1?pane=issue&itemId=270978428). Append-only reciprocal dependency requests were saved and reloaded; acceptance/status were not changed. OT-0237e excludes V1 expansion and is not relay authentication authorization. No reviewed task establishes an accepted production relay verifier API. Shared-owner disposition must identify an accepted construction/producer or explicitly record its absence and separately scope shared work.

[OT-0237c](https://github.com/orgs/Limited-Underground/projects/1?pane=issue&itemId=270978262) owns actual-target counter/receive persistence acceptance, not a generic Repeater ledger service. Existing enrollment remains OT-0238a; optional Repeater requirements do not become reverse prerequisites for base V1. Actual interfaces remain RepeaterDispatchLedgerPort::read/compare_exchange (host ledger), DuplicateCheckpointStore (ODS0 replay), and TwoSlotCompanionAuthorizationProtectedStore::load_verified/compare_commit_and_verify (companion OAP0). These are distinct seams, not interchangeable protection guarantees.

Missing concrete shared-owner contracts: exact-object device/peer/group/epoch/role authority with current enrollment/revocation and one-use consent; protected ledger API/schema/domain/full-record CAS/durable readback/error/freshness; explicit rollback threat profile and boot/time refusal; real radio-owner submission/terminal/rearm/retained-proof reconciliation. Keep endpoint private/agreement/traffic/ACK/conversation keys out of Repeater. Reuse Trail identity and context ownership. Missing/uncertain inputs refuse admission without refund, refill or automatic recovery.

Implementation BLOCKED; relay admission disabled. Conditional seven-file ledger-binding successor remains the smallest future slice only after actual storage owner/API, schema/domain, CAS/readback, rollback/boot policy and exact authority are accepted. No P4 implementation, Packet-v1 changes or production acceptance occurred. Source/evidence verification and live readback are retained under build/repeater-baseline/p4-dependency-resolution-20261028/.


## P5 host driver-contract fixture

Owner authorization October 28, 2026 grants a bounded four-file exception to
[OT-0266b-5](https://github.com/orgs/Limited-Underground/projects/9?pane=issue&itemId=286304806):
new `tests/host/repeater_driver_contract_tests.cpp`, existing `tools/Test-Host.ps1`,
this packet and `docs/PROGRESS_LOG.md`. This is host fixture authority only.
The existing P4 dependency and broader P5 target/build acceptance remain open.

Before edits, the detached candidate at c96d93ea459a7c41cf453e27edea082985650fa6
retained seventeen dirty paths and verified all260 P1-P3 runtime inputs.
Preimages, exact source hashes, compile/run recipes, outputs, reviews and
incremental recovery patch belong under
`build/repeater-baseline/p5-driver-contract-20261028/`.

The fixture imports the existing dispatch test support through an explicit
renamed-main include; the old main is not executed. The imported test source is
a required build/hash dependency and its standalone baseline still runs
separately. No second radio driver, FIFO or lifecycle manager is introduced.

| Group | Driver-event sequence and assertions |
| --- | --- |
| D1 | Accepted maximum-size opaque frame; caller mutation cannot alter copied bytes; polling and wrong-phase rearm cannot replace explicit terminal; fresh post-terminal rearm releases pending occupancy. |
| D2 | Definite rejection spends allowance; explicit rearm cannot refund/retry; old replay key remains duplicate. |
| D3 | Uncertain submission and late success remain contained across actual reconstruction; radio proof must advance, fresh consent required, source credit/replay stay spent. |
| D4 | Missing rearm proof times out; explicit failure or unverified proof contains; late success never retries/refunds. |
| D5 | Suspension retires both saved-but-unsent queued frames; replay and admission debits survive restart; fresh consent cannot resurrect old work. |
| D6 | Old-attempt completion cannot finish a newer attempt; terminal shutdown rejects late completion/rearm/recover/consent and clears P2 volatile queue/pending custody while retaining the charged attempt reservation. |

Rearm has no uncertainty enum: unavailable evidence, invalid proof and bounded
timeout use the actual existing API. Synthetic binding checks do not prove the
temporal ordering or quiescence of a physical producer. Those real-driver
copy/lifetime, hidden-retry, completion/rearm and power-cut facts remain P5 gates.
P2 retains final admission/pending bytes; P3 retains irreversible accounting;
the forwarder remains sole FIFO. Synthetic consent is not P4 production
authority. Production relay admission stays disabled; no endpoint keys,
Packet-v1, protected storage, production budget, board or firmware changes.

Fault/shutdown occupancy distinction: P2 FIFO and pending bytes retire to zero;
B intentionally retains one charged attempt reservation and invalidates radio
readiness/quiescence. Aggregate B occupancy is therefore one, not a queued retry
or restored permission. Initial fixture assertions expecting aggregate zero
failed four times and were corrected against the existing status contract;
production behavior was not changed. Initial outputs are preserved.

Validation: five affected executables pass strict GCC C++17 warnings as errors:
forwarder12, replay11, lifecycle19, dispatch15 and driver6 = **63 groups**.
Remaining80 groups from consolidated137 reused unchanged, not rerun. Combined
evidence now covers143 groups across18 executables; only63 executed here.
Independent security/test reviews CLEAR, including charged-attempt custody.
Documentation checker and18 documentation tests pass. No production source changes.

GitHub owner exception recorded against existing OT-0266b-5. Fixture completion
is separate from broader P5 acceptance. Automatic approval review rejected an
attempt to mark In Progress as insufficiently explicitly authorized; status
unchanged, no bypass. Hardware/security/production gates remain open.
Housekeeping: no manual deletion; retain preimages, initial failed outputs,
final outputs, recipes, reviews, incremental patch/hashes. Existing17 dirty
paths retained plus one new fixture. No D: backup capture/coverage claim or
public V1/website status change.


## Owner-selected SenseCAP Solar Node P1-Pro - 2026-10-29

The owner selects **SenseCAP Solar Node P1-Pro** as the dedicated Repeater
development device. Board research/selection is settled and supersedes earlier
no-target-selected statements prospectively; historical findings remain intact.
Do not substitute a board, purchase hardware or repurpose Trail's two Heltecs.
The current integration owner is [OT-0266b-5](https://github.com/orgs/Limited-Underground/projects/9?pane=issue&itemId=286304806).

### Verified documentary hardware and software

- [Seeed specifications](https://wiki.seeedstudio.com/meshtastic_solar_node/):
  XIAO nRF52840 Plus, Cortex-M4F64MHz,256KB RAM,1MB internal flash plus2MB
  onboard flash; Wio-SX1262/Semtech SX1262 (862-930MHz), supplied868-915MHz
  2dBi antenna. Region/frequency plan is still unselected; chip coverage is not
  permission for a band, modulation, power or duty cycle.
- P1-Pro includes L76K GNSS and four3350mAh18650 NMC cells. CN3165 charger
  rated0.99A,5W panel and USB-C5V1A documented. These are specifications, not
  measured Trail loads, usable battery energy or endurance acceptance.
- [Official recovery guide](https://wiki.seeedstudio.com/get_started_with_meshtastic_solar_node/)
  documents USB DFU/UF2 and XIAO bootloader recovery with adafruit-nrfutil.
  It warns against NRF-OTA. No erase, reset, USB enumeration, update or recovery
  was executed. Installed bootloader/version, SoftDevice and unit revision
  remain unknown; do not infer them from public board settings.
- Upstream [board environment](https://raw.githubusercontent.com/meshtastic/firmware/master/variants/nrf52840/seeed_solar_node/platformio.ini)
  and [board JSON](https://raw.githubusercontent.com/meshtastic/firmware/master/boards/seeed_solar_node.json)
  define seeed_solar_node, Arduino/Adafruit nRF5, S1407.3.0 and
  nrf52840_s140_v7.ld; reference app limits815104 bytes flash/248832 bytes RAM
  differ from physical capacities. These mutable sources are review inputs,
  not a pinned or accepted Trail toolchain or received-device boot identity.
- [Variant](https://raw.githubusercontent.com/meshtastic/firmware/master/variants/nrf52840/seeed_solar_node/variant.h):
  SPI D8/D9/D10,CS D4,DIO1 D1,BUSY D3,RESET D2,RXEN D5,TCXO1.8V,DIO2 RF
  switch; QSPI P25Q16H. Freeze an immutable upstream revision and validate pin
  numbering against actual PCB before operation. Debug configuration lists
  J-Link; exposed physical SWD access is not verified. USB-C is documented.

### Minimum platform adaptation and authority boundary

Actual P1-P3 control sources are target-neutral C++17. Existing Heltec ESP-IDF,
ESP32 RadioLib HAL, NimBLE and NVS bindings are not an nRF52840 build. Portable
logic reuse is plausible, not ARM build/link/RAM/stack acceptance. Meshtastic
firmware/BSP is a hardware reference, not Trail protocol compatibility; do not
import its mesh/auth/routing behavior. No competing protocol or lifecycle.

First proposed build-only scope, requiring separate exact approval:
`firmware/targets/sensecap_solar_p1_pro_smoke/platformio.ini`,
`firmware/targets/sensecap_solar_p1_pro_smoke/src/main.cpp`,
`firmware/targets/sensecap_solar_p1_pro_smoke/README.md`, this decision packet
and docs/PROGRESS_LOG.md. Pin BSP/compiler/linker/dependency revisions, compile
and link actual P1-P3 with deny-all authority and no RF submission, retain
ELF/map/size/stack and source hashes, and enforce reserved boot/SoftDevice
regions. No default-ready port, fresh-credit ledger or production store.
New paths remain uncreated. Current owner selection resolves target identity,
not the existing task's explicit build-scope gate. Completed P5 fixture scope
does not authorize this new target or an adapter.

Later existing SX1262 driver binding must implement correlated submission,
terminal and receive-rearm events via RepeaterDispatchRadioPort; queued/send
success is not completion. Audit SPI/BUSY/DIO1 IRQ, TCXO/RF switch, private
copy lifetime and hidden retries; unknown outcomes retain spent allowances
and invalidate proof. Forwarder is sole FIFO, P2 owns pending/final admission,
P3 debits irreversibly. Suspend/shutdown retire P2 volatile bytes while B
retains charged uncertain attempt custody. P4 authority/storage stays gated.

### Power, sleep and physical gates

[Nordic power specification](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/power.html)
distinguishes System ON/OFF. System OFF wake resets execution. Continuous
receive therefore needs SX1262 RX retained with MCU System ON idle/interrupt
handling (engineering inference), not blind radio sleep or duty-cycled RX.
System OFF/reboot requires existing replay/budget reconstruction and fresh
consent; solar recovery cannot revive queue/credit/authority. No documented
end-to-end wake-to-Trail-ready bound exists. Measure TCXO start, IRQ service,
rearm, brownout/power cuts, dark endurance and battery thresholds before
unattended acceptance. GNSS can remain unused; do not assume GNSS time is
trusted continuity. External flash capacity supplies no protected CAS,
antirollback or key security. No production budget chosen.

Hardware, security and integration independent reviews support this record;
no real radio, hardware, firmware/configuration or bootloader was accessed.
Host runtime fixtures remain unchanged; reuse their verified63-group P5 and
prior consolidated evidence rather than rerunning unchanged suites.


## P1-Pro five-file build milestone - 2026-10-29

Owner authorizes platformio.ini, src/main.cpp and README.md in
firmware/targets/sensecap_solar_p1_pro_smoke plus this packet and progress log.
Actual eight portable sources compile directly; no imported mesh protocol or
production radio/store/authority adapter. Deny-all seams and zero invalid
configuration leave real dispatch contained; setup exercises refusal paths and
shutdown, with runtime observation NOT claimed. No RF/upload/key generation.

Build recipe/environment/dependencies and actual result are preserved under
`build/repeater-baseline/p5-p1pro-build-20261029/`. See target README for command.
Seeed platform is pinned to commit a6fd41debbbcda13b5340fff50c9092ed3289194.
Initial Git clone hit Windows long paths, then sandbox recursive-shell failure;
immutable commit archive avoids that path. PlatformIO6.1.18 installed task-locally.
Independent security source review CLEAR; ARM build/map/size gate recorded in
verification.json. Existing host suites are unchanged and not rerun.

Porting preflight: exact MCU/XIAO Plus chosen; Solar Node pins/region, installed
bootloader identity, continuousRX/power/IRQ and P4 custody still unverified.
No physical boot/flash tests apply to this build-only milestone. BSP/linker
layout is a build assumption, not device custody acceptance. Preserve no
secondFIFO, P2 pending/finaladmission, irreversible P3 debit and unknownradio
containment. No new backend/protocol or production budget. Broader P5 stays open.

Build milestone result: PASS ELF/HEX;68304 flash bytes and13348 staticRAM bytes against BSP app limits811008/237568. Eight portable sources unchanged; seven newlib POSIX stub linker warnings retained, not runtime accepted. Short task-owned tool copies required on Windows; no upload/RF. Exact commands/package hashes/map/symbol evidence retained in build/repeater-baseline/p5-p1pro-build-20261029/. Broader P5/P4 and hardware gates stay open.

ELF inspection: .text67512+.ARM.exidx8+.data784=68304 flash; .data784+.bss12564=13348 staticRAM. .heap222172 is a linker reservation, with2048 further stack reservation; not runtime heap/stack measurement. FLASH origin0x27000/length0xc6000; RAM0x20006000/length0x3a000. No installed-device layout acceptance.


## Owner-approved P1-Pro SX1262 adapter - 2026-10-30

Owner accepts exactly the nine-file adapter scope in
`build/repeater-baseline/p5-sx1262-scope-20261030/scope-request.md`.
Actual adapter header/source and focused host tests are added; target
platformio/main/README, Test-Host, this packet and progress log updated.
Eight portable core sources remain byte-identical. Use RadioLib7.1.2 and
actual P1-Pro pins verified against installed XIAO Plus BSP. No second FIFO
or lifecycle, no production authority, TX, keys, storage backend or hardware.

Initial controlled integration tests caught a real compatibility defect:
P3's normal post-submit readiness invalidation must preserve active attempt
receipt custody. Correct only the new adapter, not portable core. Hard
uncertainty/partial/inconsistent submission stays fail-closed and spent.
Independent review also required finite settings before SPI and retained
receive-only probes in ELF (without executing them) for physical interface
validation. Final tests, build size/warnings/map and hashes are recorded in
`build/repeater-baseline/p5-sx1262-adapter-20261030/verification.json`.

Next executable hardware milestone is connecting the dedicated P1-Pro for
a separately authorized identity/bootloader/revision and receive-only
SPI/BUSY/RESET/DIO1/IRQ/rearm observation. Preserve original firmware/config,
endpoint Heltecs and recovery custody. Real timing/quiescence/brownout/power
and P4 protection remain unaccepted. No additional host preparation packet
is required for the completed adapter scope.

Final adapter result (2026-10-30): PASS10 new controlled groups +63 affected
regression groups =73 executed groups; native strict C++17 warnings-as-errors.
Final P1-Pro ELF/HEX PASS:103716 bytes flash of811008,14892 bytes staticRAM
of237568. Seven existing newlib POSIXstub linker warnings remain; no stub
runtime or stack/power behavior accepted. Symbols/map retain RX initialize,
rearm and IRQ probes; setup never invokes them. Independent integration/security
review CLEAR. No portable source edits. Normal readiness invalidation preserves
current attempt receipts; hard uncertainty remains sticky, no retries/refunds.
Exact source/library/artifact hashes, initial failure outputs, incrementalpatch
and reproduction evidence retained under build/repeater-baseline/p5-sx1262-adapter-20261030/.


## P5 target-bound engineering result � 2026-10-31

Owner's autonomous host-only engineering task continues OT-0266b-5; it does not
accept hardware or expand direct-only endpoint V1. Candidate remains the isolated
`repeater-host-baseline-20261027` worktree at
`c96d93ea459a7c41cf453e27edea082985650fa6`. The prior nine-file receipt's source
and all 25 artifact hashes were verified before edits. 1,062 prior component/test
input hashes and all 328 retained RadioLib files match; eight portable P1-P3
sources remain unchanged. Existing dirty work and earlier evidence are preserved.

### Confirmed defect, correction and discrimination

Searchable symptom: **SX1262 IRQ SPI failure reported as zero/idle IRQ**.
RadioLib 7.1.2 `SX126x::getIrqFlags()` ignores the return from `SPIreadStream`.
Our target's old value-only `irq_flags()` additionally returned zero when not
initialized. Thus a physical diagnostic could not distinguish absent IRQ from
an unavailable or failed observation. Production admission was already refused;
this was not an observed RF incident or an admission bypass.

The target now exposes `read_irq_flags(uint16_t&)` with an explicit status.
It uses the already pinned Module's checked GetIrqStatus transaction, clears the
output before any call, propagates transport/BUSY errors, and retires local RX
readiness after error. A valid zero word requires a successful transaction.
No retry, readiness proof, completion receipt, refund or authority is inferred.
The retained bench hook uses the same checked signature. The existing library,
not a second SPI/radio implementation, still owns serialization and finite BUSY
waits. Physical timing and stuck-pin behavior remain unmeasured.

The original value-only semantics, placed behind a task-local compatibility
signature, fail eight assertions in the new fixture. The corrected implementation
passes nine actual ARDUINO backend/target-entrypoint groups. Controlled external
Arduino/SPI/RadioLib observations execute our real backend plus real `setup()`
and `loop()`; they do not execute Nordic hardware or prove library electrical
behavior. Tests cover exact call/pin/configuration custody, invalid settings
without IO, init/rearm failures, invalidation, valid-zero/nonzero IRQ, failed
transport, BUSY observations, terminal shutdown and hard-refused TX/proof.
Existing ten A1-A10 adapter groups pass. The unchanged 63 affected P1-P3/P5
regression groups are reused, not rerun. Strict native C++17 has zero warnings.
Independent review of the production change is CLEAR; primary review checks
fixture quality separately. The new fixture is registered in `tools/Test-Host.ps1`.

### Actual executable path and policy boundary

| Stage / owner | Actual target state | Remaining gate |
| --- | --- | --- |
| Startup / target | Constructs existing P2/P3 owners; zero configuration, denied authority, unavailable role/replay/ledger stores; exercises refusal, then terminal shutdown. | No real enrollment, persistent configuration, watchdog or autonomous recovery binding exists. |
| Receive / RadioLib backend | Explicit RX init/rearm and checked IRQ methods compile and remain in ELF. `setup/loop` never invoke them; no RX frame extraction caller exists. | Received-unit pin/electrical/timing proof and explicit RX bench execution authority. |
| Validate / shared security and P2 | `verify_frame` always refuses in target. Host tests provide trusted, exact-byte metadata. No LUProtocol relay verifier is integrated. | P4 accepted relay-capable authenticated context/epoch/member/role producer; direct-only OTSL0/Packet-v1 cannot be relayed. |
| Route / forwarder | Host-validated immutable zero/one-repeater permission, self/reflection and duplicate suppression, original-age expiry. No mutable TTL. | No repeater-first discovery, route aging, alternate relay election or multi-hop deployment is accepted. Do not add them. |
| Queue / forwarder and P2 | Sole fixed FIFO, per-source/global bounds, P2 pending bytes/final claim, persist-before-release replay. | Target protected persistence/time/rollback producer remains unavailable. |
| Dispatch / P3 | Irreversible full maximum-frame airtime/attempt debit; busy/reject/uncertain never refund or automatically retry. | Numeric values are host fixtures, not production RF/power limits. |
| Completion / driver + P3 | Synthetic exact-attempt terminal/rearm receipts are host validated. Physical submit and physical proof/receipt methods refuse. | Accepted physical terminal/quiescence/rearm reconciliation and P4 authority before any relay activation. |
| Suspend/restart/shutdown | Existing P2/P3 retire volatile work, preserve replay/charged uncertainty and require fresh consent; stopped instance cannot restart. | Real protected media, trusted clock continuity and power-cut recovery acceptance. |

Dedicated FIFO policy admits no emergency/SOS budget bypass or unauthenticated
priority override. Critical-message delivery/ACK remains endpoint-owned; a relay
cannot claim delivery. The separate historical TTL and routing simulators are
not production nRF52840 bindings. Offline host policy needs no server/internet,
but the actual target remains deliberately non-operational as a relay.

### Target, images, hardware and recovery

Target is the owner-selected **SenseCAP Solar Node P1-Pro**, nRF52840/SX1262.
No alternative board or endpoint device is selected. Platform commit remains
`a6fd41debbbcda13b5340fff50c9092ed3289194`; XIAO Plus Arduino/Adafruit nRF5
framework 1.10101.0 (1.1.1), CMSIS 5.7.0, S140 7.3.0 layout and retained xPack
GCC 12.2.1-1.2; RadioLib stays 7.1.2. No dependency upgrades. The unversioned
framework archive's exact prior package custody remains necessary for replay.

Two initially absent output-directory builds PASS and produce identical ELF,
HEX and application BIN hashes. Application flash is **103764/811008 bytes**,
static RAM **14892/237568 bytes**, +48 flash and no RAM increase from prior image.
Seven existing newlib POSIX-stub linker warnings remain; stack/heap headroom,
watchdog response, USB startup and runtime safety are not measured.
HEX checksum/address/nonoverlap/vector checks PASS: application bytes only at
`0x27000..0x40553`, initial SP `0x20040000`, Thumb reset `0x3a619`.
The first task-local validator omitted Intel HEX type-02 segment addressing;
that validator was corrected, not the firmware. No low-address image write is
present. The S140/bootloader layout is a build assumption, not installed-unit
identity. No UF2/DFU install package or matching restore image is accepted.

Official manufacturer-only references revisited: [Seeed P1-Pro hardware](https://wiki.seeedstudio.com/meshtastic_solar_node/),
[Wio module datasheet](https://files.seeedstudio.com/products/SenseCAP/Wio_SX1262/Wio-SX1262_Module_Datasheet.pdf),
[Wio carrier schematic](https://files.seeedstudio.com/products/SenseCAP/Wio_SX1262/Schematic_Diagram_Wio-SX1262_for_XIAO.pdf),
[Semtech checked IRQ reference](https://github.com/Lora-net/sx126x_driver/blob/master/src/sx126x.c),
and [Nordic POWER specification](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/power.html).
No Meshtastic or MeshCore source, architecture or implementation was consulted
for this increment. Historical references are retained as provenance only.
The generic Wio carrier drawing does not establish the exact installed Solar
carrier routing: preserve existing target pin assumptions without promoting them
to manufacturer-confirmed received-unit facts. Exact board revision, harness,
antenna, reset/BUSY/DIO1, DIO2 switch and 1.8V TCXO remain physical checks.

Manufacturer lists 1MB internal/256KB RAM, 2MB board flash, CN3165 charger,
5W solar, USB 5V/1A and four 3350mAh cells for P1-Pro. They are specifications,
not measured available capacity or a firmware storage API. External flash, GNSS,
charger/battery telemetry and power policy are not bound in this target.
Stationary/vehicle/battery profiles have no measured production budgets. System
OFF is not continuous receive and wake/reset cannot retain session permission;
System ON idle plus radio RX needs separate measured timing/power acceptance.
Do not infer a sleep/wake or solar restart policy from the inert one-second loop.

### Dependency map and next physical milestone

P5 target tests/build -> actual dedicated-unit identity/revision and bootloader/
recovery custody -> separately authorized receive-only initialization, checked
SPI/BUSY/RESET/DIO1/IRQ/rearm observations with approved region/antenna -> physical
radio reconciliation. Production forwarding additionally depends on P4
OT-0266b-4 (Project 9 item 286304467), shared OT-0237e selection coordination
(Project 1 item 270978428), and OT-0237c target persistence acceptance (item
270978262). Neither shared item is an accepted generic Repeater ledger/verifier
API. Accepted APIs for relay authority, protected ledger/role storage and
rollback/time remain required; no endpoint private/traffic keys may be copied.
OT-0267a/b continue owning physical interoperability/deployment/release gates.

Next useful milestone remains connecting the actual P1-Pro, not another host
planning packet. Inspect received-unit identity and existing boot/recovery state
first; a later explicitly authorized candidate installation/RX-only run must
bind exact artifact, board, recovery image and radio settings. No physical
connection, reset, flashing, debug-port or RF action was performed here.

Evidence/reproduction: `build/repeater-baseline/p5-target-bound-20261031/`.
This holds preimages, regression discrimination, actual-backend test results,
two build logs/images, image checks, hashes and incremental patch. Output is
separate from source and prior evidence. Firmware-porting preflight: source and
configuration linkage/image reproducibility checked; startup/ordering/refusal
host exercised; real storage/time/clock/power/device/USB/recovery checks remain
unavailable and were not simulated into acceptance. No V1 percentage or public
website status changes. Housekeeping: nothing deleted; existing dirty worktree,
new source and all task-owned output retained for review/recovery; no cleanup
approval needed. No commits, merges, pushes or D: backup operations.


## P5 target-bound engineering result (2026-10-31)

Owner host-only task continues OT-0266b-5 in isolated repeater-host-baseline-20261027,
base c96d93ea459a7c41cf453e27edea082985650fa6. Prior source and25 artifact hashes
verified;1062 prior component/test inputs and328 RadioLib files match. Eight
portable sources unchanged. Existing dirty work and historical evidence preserved.

### Confirmed defect and tested correction

Searchable symptom: SX1262 IRQ SPI failure reported as zero/idle IRQ. Pinned
RadioLib7.1.2 getIrqFlags discards SPIreadStream status; our value-only target API
also returned zero when unavailable. No physical incident/admission bypass observed.
New read_irq_flags(uint16_t&) returns checked Module transaction status separately,
clears flags and retires local RX readiness on error. Valid zero requires success.
No retry/refund/authority/completion or quiescence proof inferred. Probe updated.
Original semantics behind a task-local compatible API fail8 assertions; corrected
actual ARDUINO backend plus real setup/loop pass9 groups with controlled IO.
Tests cover exact init/config/pins, invalid settings/noIO, init/rearm/invalidation,
IRQ zero/data/errors, BUSY observation, terminal shutdown and refused TX/proofs.
The external IO is controlled; Nordic hardware/RadioLib electrical timing is not
executed or accepted. Existing10 adapter groups PASS; unchanged63 P1-P3/P5 groups
reused. Strict native C++17 zero warnings. Independent production review CLEAR;
primary separately reviewed tests. Test-Host.ps1 registers the backend fixture.

### Actual operation and dependency map

| Stage / owner | Actual target behavior | Remaining gate |
| --- | --- | --- |
| Startup / target | Denied authority, zero config, unavailable role/replay/ledger; refusal then terminal shutdown. | No real config/watchdog/power or restart binding. |
| RX / existing RadioLib backend | Explicit init/rearm/checkedIRQ retained in ELF, never called by setup/loop. No RX frame extraction caller. | Received-unit wiring/timing and authorized RX bench execution. |
| Validation / shared security and P2 | verify_frame refuses; host metadata trusted by injection. No production LUProtocol relay verifier. | P4 device/peer/group/epoch/role/source authority; direct-only OTSL0/Packet-v1 remain refused. |
| Route / forwarder | Immutable zero/one-repeater permission, duplicate/self/reflection suppression, original-age expiry; no TTL rewrite. | No accepted dynamic repeater-first discovery, route aging, alternate election or multi-hop. |
| Queue / forwarder and P2 | Sole bounded FIFO, source/global limits; P2 owns pending bytes/final claim; replay persisted before release. | Protected target storage/time/rollback producer unavailable. |
| Dispatch / P3 | Irreversible maximum-frame airtime/attempt debit; busy/reject/uncertain never retry/refund. | Production numeric budgets undecided; fixture values only. |
| Completion / driver and P3 | Bound synthetic terminal/rearm receipts; physical TX/proof/receipts refuse. | Accepted physical quiescence/terminal/rearm plus P4. |
| Restart/shutdown | Volatile work retired; replay/charged uncertainty retained; fresh consent, terminal stop. | Real media/time/power-cut acceptance. |

Dedicated FIFO grants no emergency/SOS budget bypass; endpoint owns ACK/delivery.
Legacy TTL/route simulators are not nRF52840 target integrations. Offline policy
needs no server but actual target is not an operational relay. No competing protocol.
P4 owner OT-0266b-4 (Project9 item286304467) remains Todo on current board. Existing
shared links: OT-0237e selection coordination (Project1 item270978428) and OT-0237c
actual-target persistence (item270978262), neither an accepted generic Repeater
verifier/ledger API. No live shared-owner acceptance obtained here. OT-0267a/b own
physical interoperability/deployment/release. Endpoint private/traffic keys stay out.

### Target build, power and recovery boundary

SenseCAP Solar Node P1-Pro/nRF52840/SX1262 remains selected. Platform commit
 a6fd41debbbcda13b5340fff50c9092ed3289194, XIAOPlus Arduino/Adafruit nRF5
framework1.10101.0(1.1.1), CMSIS5.7.0, S1407.3.0 build layout, retained xPack
GCC12.2.1-1.2 and RadioLib7.1.2 unchanged. Exact framework package custody remains
required; unversioned archive URL alone does not reproduce inputs.
Two initially absent-output builds PASS with identical ELF/HEX/applicationBIN.
Flash103764/811008, staticRAM14892/237568 bytes (+48flash/noRAM increase).
Seven existing newlib POSIXstub linker warnings remain; no runtime stack/heap,
watchdog/USB/power/timing acceptance. HEX checksum/nonoverlap/vector/range PASS:
103764 application bytes0x27000..0x40553, SP0x20040000, Thumb reset0x3a619.
Initial task-local validator omitted Intel HEX type02 segment addressing; corrected,
not a firmware defect. S140/bootloader layout is not installed-unit acceptance;
no UF2/DFU install or matching restoration artifact has been accepted.

Official references revisited: [Seeed P1-Pro](https://wiki.seeedstudio.com/meshtastic_solar_node/),
[Wio datasheet](https://files.seeedstudio.com/products/SenseCAP/Wio_SX1262/Wio-SX1262_Module_Datasheet.pdf),
[Wio schematic](https://files.seeedstudio.com/products/SenseCAP/Wio_SX1262/Schematic_Diagram_Wio-SX1262_for_XIAO.pdf),
[Semtech checked IRQ reference](https://github.com/Lora-net/sx126x_driver/blob/master/src/sx126x.c),
[Nordic POWER](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/power.html).
No Meshtastic/MeshCore source, architecture or implementation consulted this increment.
Historical references retained only as provenance. Generic Wio carrier drawing does
not establish exact Solar carrier routing; existing pin assignments remain assumptions
pending received-unit/manufacturer-specific confirmation, including antenna, reset/
BUSY/DIO1, RFswitch and1.8V TCXO. No alternate board selected.
Manufacturer specifications1MB/256KB,2MB board flash,CN3165,5Wsolar,USB5V1A,
four3350mAh cells are not measured available resources or a protected-storage API.
GNSS/externalflash/charger telemetry unbound. Stationary/vehicle/battery production
limits unmeasured. System OFF is not continuous RX; reset/wake cannot restore consent.
System ON idle plus RX needs measurements; inert delay is not a sleep/power policy.

Next milestone: actual dedicated-unit identity/revision and existing boot/recovery
custody, then separately authorized receive-only SPI/BUSY/RESET/DIO1/checkedIRQ/rearm
observations with exact region/antenna. Future install binds device/layout/image/restore
artifact. No new host planning packet needed. No connection/reset/flash/debug/RF here.
Porting preflight: linkage/config/images checked, two builds identical, composed
startup/refusal host exercised. Real storage/time/clock/power/USB/boot/recovery checks
unavailable, not simulated into acceptance. No V1/public website status change.
Evidence/preimages/reproduction/patch/images/hashes: build/repeater-baseline/p5-target-bound-20261031/.
Housekeeping: no deletions; existing dirty worktree/source/artifacts retained for recovery;
no cleanup approval or D:backup operations. No commits/merges/pushes.
