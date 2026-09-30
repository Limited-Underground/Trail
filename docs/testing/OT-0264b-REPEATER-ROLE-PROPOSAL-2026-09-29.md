# OT-0264b Repeater role and first-topology proposal

Prepared 2026-09-29 for owner review. This proposes a specific first role and
topology; the owner's decision remains pending. No relay firmware is implemented.

## What is proposed

Recommend proving the first Trail relay path with three radios: a sender, one
explicitly authorized dedicated repeater, and a receiver. In this first profile,
the repeater performs forwarding and local administration, not simultaneous
end-user messaging. Retain optional combined client/repeater mode as the accepted
future direction; this recommendation does not replace it with a dedicated-only
product or rule out reusing the same eligible hardware later.

The narrower first profile separates forwarding failures from contention between
local client traffic and relayed traffic. Once forwarding is proven, combined
mode needs its own scheduling, power and concurrent-traffic acceptance. If
combined mode is the owner's immediate priority, select it now and include those
requirements in the first plan instead of treating them as an automatic upgrade.

Both endpoints must keep their existing direct path when the repeater is absent
or disabled. Relay support remains optional and outside current base V1.
No unlimited range, arbitrary mesh or guaranteed delivery is implied.

## Decision offered for owner acceptance

| Dimension | Proposed first profile | Combined-first alternative |
| --- | --- | --- |
| Topology | Exactly one configured authorized repeater between two endpoints, with direct/relay-disabled controls. | Same single-repeater topology, but its radio also serves a local client. |
| Role authority | Explicit authenticated provisioning enables the relay role; boot begins disabled until role, membership, epoch and durable replay state verify. | Same authority plus explicit local-client ownership and resource isolation. |
| First traffic | Only a specifically reviewed eligible protected frame class; no implicit forwarding of every Trail packet. | Same restriction; include the nominated local client traffic in fairness and overload acceptance. |
| Scheduling | One bounded relay queue and finite age/rate/airtime budget. | Separate bounded admission and an accepted priority/fairness policy; neither traffic source may monopolize the radio. |
| Local controls | Provisioning, disable/remove, status and recovery. | Also client interactions, with their own stale-input and delivery-state boundaries. |

Owner decision: accept the dedicated-first profile above, or choose combined-first
with the added concurrent-client gates. Either choice preserves the combined
capability direction recorded in [Future Concepts](../FUTURE_CONCEPTS.md).
Until that choice is accepted, this is a prepared recommendation, not a fixed
product decision. The live task's acceptance requires the topology/role boundary
to be fixed; final acceptance must settle this choice rather than silently infer it.

## Security and compatibility boundary

The [accepted inventory](OT-0264a-REPEATER-EVIDENCE-2026-09-23.md) distinguishes
host forwarding components from historical MeshCore physical results. Neither
establishes a deployable Trail repeater. The
[single-repeater policy](../protocol/SINGLE_REPEATER_FORWARDING_V0.md) provides
exact-byte, bounded forwarding, but its verified-metadata fields rely on a real
cryptographic adapter. Parsed Booleans and radio metadata are not authentication.

Current direct-only security must not be stretched into relay support by changing
reserved bits, mutating authenticated TTL, distributing endpoint keys for
convenience, or treating old group-policy tests as current packet compatibility.
OT-0265a must explicitly reconcile historical Decision 0004, current direct-only
OTSL0/v0 and the [secure transport decision](../decisions/0035-host-tested-secure-lora-key-transport-contract.md).
It must select a versioned protected construction and describe what the repeater
can authenticate and what, if anything, it can decrypt. No wire format or key
distribution change is approved here.

For the proposed profile, require one-hop exact protected-byte preservation,
current sender/membership/epoch and forwarding permission, replay refusal and
finite admission. Unknown, incompatible, malformed, unauthorized or expired
objects fail closed. A repeater cannot manufacture the receiver's acknowledgement.
Receivers continue normal endpoint authentication, replay and duplicate checks.
Multiple relays, route discovery and arbitrary hop counts remain outside scope.

## Proposed end-to-end flow and recovery

| Before state / owner | Trigger and required effect | Forbidden effect | Failure and recovery |
| --- | --- | --- | --- |
| Boot, disabled / role and persistence owner | Verify configured relay authority and recover durable replay state before admitting work. | Starting from an empty/default identity after damaged state. | Contain forwarding and show local recovery status. |
| Ready / secure input adapter | Authenticate eligible frame, current sender, context, epoch and immutable permission. | Untrusted parsed fields entering the verified-metadata path. | Reject before granting forwarding or poisoning another sender's replay identity. |
| Eligible / replay and queue owner | Apply replay/duplicate, rate, queue and age rules; persist the admitted replay checkpoint. | Assuming RAM insertion is durable or retrying dropped congestion indefinitely. | If save/readback is uncertain, do not release a frame. |
| Persisted / radio owner | Release exact protected bytes within the current finite transmission budget. | Rewriting authenticated fields or equating queued with transmitted. | Expire/retire failed work and explicitly restore receive readiness as the actual driver requires. |
| Receiver / endpoint owner | Authenticate and accept independently; only endpoint evidence can establish delivered status. | Repeater receipt or transmit success presented as end-to-end delivery. | Endpoint timeout remains a failure/unknown outcome, not a synthetic ACK. |
| Disable/remove / role authority | Prevent new admission and retire still-owned queued work; invalidate stale role work. | Old callbacks re-enabling forwarding or retransmitting after removal. | Already transmitted bytes cannot be recalled; record bounded local state without private packet contents. |
| Power loss/restart / persistence owner | Verify replay checkpoint before new forwarding; retain accepted duplicate suppression. | Resetting to an apparently fresh replay window to improve delivery rate. | Contain uncertain state; a persisted-but-unsent frame may be lost and must not be represented as delivered. |

The [replay coordinator](../protocol/SINGLE_REPEATER_REPLAY_COORDINATOR_V0.md)
is reuse evidence for save-before-release, not proof of physical durability.
Queue/rate denial can consume a replay opportunity under that host policy;
OT-0265a/b must preserve or explicitly review that tradeoff. Deadlines belong to
the checked local clock; regression or uncertain age refuses forwarding.
Queue sizes, retries, airtime and power figures await reviewed budgets and tests.

Future proof must include sender/relay/receiver packet accounting, relay-disabled
negative control, replay/duplicate/overload, power interruption, receive rearm,
restart and measured power. Combined-first additionally needs simultaneous local
and forwarded traffic. Three radios demonstrate the relay path; two cannot.
Exact devices, region, antennas, artifacts, restoration and owner readiness are
separate physical authorization prerequisites, not this proposal's inputs.

## Acceptance and next gate

The report supplies a concrete topology, role authority, non-goals and compatibility
boundary for an owner decision. Once accepted, OT-0265a can freeze the secure
forwarding review; the [registered plan](../../tasks/OPTIONAL_PRODUCTS_PLAN.md)
then owns scheduling, target selection, recovery, implementation decomposition
and later physical/release work. Approval of this proposal is not implementation
or hardware authorization.

Preparation reviewed existing source contracts and produced documentation only.
Repository document/link validation is reported with the batch closeout.
No relay device behavior, build, RF trial, range/runtime claim, V1 completion
credit, public website change or publication resulted from this task.
