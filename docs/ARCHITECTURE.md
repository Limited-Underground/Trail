# OpenTrail Initial Architecture

Status: proposed foundation, 2026-08-08. This document records boundaries and evaluation criteria; it does not claim an implemented system.

## Goals and constraints

OpenTrail must remain useful offline, tolerate intermittent peers, isolate failures, fit ESP32-class resources, and respect LoRa bandwidth and regional radio regulations. Raspberry Pi-class hardware is outside the baseline unless measured requirements make it necessary.

## Logical layers

```text
User interface / alert presentation / map view
                 |
Group, message, position, and emergency services
                 |
Delivery policy: priority, retry, deduplication, TTL, store-forward
                 |
Versioned OpenTrail packet codec and authentication boundary
                 |
Transport interfaces: LoRa | local setup/transfer | test transport
                 |
Hardware interfaces: radio | GPS | display/touch | storage | power | entropy | monotonic clock
```

Application and protocol logic must depend on interfaces, not concrete boards. Packet codecs and delivery state machines should be host-testable. Board bindings and role composition belong in target applications.

The host-tested secure-randomness boundary uses explicit not-ready, ready, and
failed states, bounded 1-64-byte requests, and complete output or no change on
any failure. Deterministic output exists only under test support; exact ESP-IDF
entropy/DRBG binding, radio and ADC concurrency, cold boot, brownout, and other
physical evidence remain target acceptance gates.

The host-tested checked monotonic-clock boundary keeps boot-local elapsed time
separate from UTC, permits equal millisecond reads, preserves continuity across
temporary not-ready results, and latches rollback or source failure closed for
that boot composition. Target code obtains one successful timestamp per
cooperative cycle and supplies it to every time-dependent component in that
cycle. Exact ESP-IDF timer/task/deep-sleep/brownout and physical timing evidence
remain target gates.

The host-tested power-state boundary accepts one atomic, timestamped adapter
observation and keeps source health, external-power state, battery presence,
charge state, optional normalized percentage, and optional voltage separate.
Composition injects low/critical percentage and freshness policy; the common
component never derives percentage from voltage. Charge is orthogonal to the
battery band so low-and-charging remains visible. Missing, stale, future,
invalid, and fault observations conservatively request attention and deny
optional high-power work. Exact board adapter, charger control, shutdown
behavior, physical thresholds, battery life, and power-failure evidence remain
target gates.

The host-tested local-interface boundary sends fixed semantic status/action
frames rather than pixels to a target renderer and accepts normalized action-
slot events rather than touch coordinates or GPIO identities. Input resolves
only against the exact successfully displayed boot-local revision, preventing a
delayed event from activating a newly repurposed location. The canonical
critical-confirmation frame requires critical attention, confirm/cancel action
order, and a hold gesture; a local resolution remains a request, not radio
delivery. Exact renderer/input adapters, localization, accessibility,
readability, distracted-driving policy, performance, and physical behavior
remain target gates.

Update recovery presentation reuses that same semantic boundary. A valid
`OTRD0/v0` outcome maps to fixed status or system-fault notice enums; invalid
diagnostics fail visibly to a generic critical service frame. Only nonblocking
trial, rejected-transition, and cleanup notices offer acknowledgement, which
cannot confirm update health, execute cleanup, request service, or reboot.
Exact target scheduling, renderer wording, revision ownership, and physical
recovery behavior remain target gates.

The host-tested portable-client composition preflight now collects every
target-facing dependency required by the first self-contained client and
rejects missing bindings or incoherent product capabilities before application
startup. Its whole-contract review found that replay recovery needs a separate
704-byte `DuplicateCheckpointStorage` surface in addition to the existing
64-byte multi-domain `PersistentStorage`; both are explicit target obligations.
Power policy and display capability validation are shared pure functions rather
than duplicated target interpretations. Preflight reads only advertised radio
MTU and performs no mutable adapter I/O. GPS no-fix and entropy not-ready remain
valid structural states.

A concrete target still must construct one checked monotonic clock and share one
successful sample across each cooperative application cycle, keep UI failure
independent of radio service, and preserve separate storage namespaces and
failure semantics. No ESP-IDF application, task order, partition map, pin map,
protected backend, driver, or board build is supplied by the composition
preflight.

## Proposed node roles

- **Client:** originates and consumes user messages, alerts, and positions.
- **Client/Repeater:** a client explicitly configured to forward eligible traffic under controlled rules.
- **Fixed relay:** powered or solar relay optimized for availability and forwarding.
- **Command/interface node:** larger display or field-case role that aggregates group state without becoming a single point of failure.

Roles are configuration/composition choices, not separate wire protocols. A portable or vehicle-mounted form factor may use any appropriate role.

## Networking direction

The first protocol should be topology-neutral enough to test direct and controlled-forwarding behavior, but it must not assume unrestricted mesh flooding.

Every packet envelope should eventually include:

- protocol version and message type
- source node ID and message ID
- group/network identifier or scoped addressing information
- priority and flags
- creation time or monotonic age information where available
- hop limit/TTL when forwarding is permitted
- payload length and integrity protection
- authentication metadata once the security model is chosen

Core receive behavior should validate length/version/integrity, authenticate when enabled, suppress duplicates, apply group/address rules, deliver locally, and only then consider bounded forwarding. Acknowledgements are message-class policies rather than mandatory responses to all traffic.

Priority classes should reserve capacity for emergency and critical traffic. A
host-tested position scheduler now requires explicit start, stops immediately,
emits only a current validated fix, and schedules from actual accepted/deferred
work so delayed service coalesces instead of creating a catch-up queue. Stale,
unavailable, invalid, and unencodable fixes never reach its sink. An explicitly
experimental host sink now revalidates that payload, obtains injected ephemeral
packet-v0 metadata, encodes the exact 38-byte frame, and admits it to the tested
priority queue only as background position traffic. Queue creation/expiry uses
the scheduler's actual attempt time, and pressure remains visible to scheduler
retry. A single-owner handoff then peeks at the strict-priority/FIFO head and
commits that entry only after the delivery controller accepts its copy. Full or
rejected delivery admission retains the priority entry, the remaining queue
lifetime caps rather than extends delivery expiry, and an impossible commit
mismatch latches the composition closed to prevent duplicate retry. This proves
component composition only: packet v0 is unauthenticated, real coordinates
remain prohibited, and authenticated packet/priority plus direct-radio
composition is still required. Chat and position must not starve emergency
traffic. Exact cadence, airtime budgets, retry policy, and regional constraints
depend on measurement and review.

A host-only outbound coordinator now establishes one cooperative service order:
sample `CheckedMonotonicClock` once, optionally read location only while sharing
is active, service position scheduling, attempt one priority-to-delivery
handoff, service delivery, and finally service the opaque radio. A temporary
not-ready clock performs none of that downstream work; rollback or source
failure stops position sharing and latches the coordinator closed. Position
failure does not block already queued messaging, and handoff failure does not
block already accepted delivery work. This is not a target task, synchronization
primitive, inbound receiver, UI-input loop, or physical driver composition.

The same coordinator owns target-facing position commands. Start reads the
checked clock when the action is applied and passes only that exact successful
value to the scheduler. Temporary not-ready reaches no scheduler; rollback or
source failure stops sharing and latches the coordinator. Stop is immediate and
does not access the clock. The target-facing UI adapter therefore accepts no
caller-supplied timestamp.

A host-only position UI coordinator now owns boot-local revision allocation,
current presentation, one checked input poll, live Start/Stop application, and
the required post-action refresh. Temporary clock deferral retains the current
truthful Start frame. A failed post-action publication invokes immediate Stop
and latches further input closed; revision exhaustion does the same before an
action is polled. This proves cooperative sequencing only. One target task or
lock must still serialize UI and outbound service calls.

Before it polls input, that coordinator now derives a candidate from the live
outbound/scheduler owners and compares only user-visible frame semantics with
the last successfully committed frame. GPS wait/recovery, sink deferral, and a
permanent outbound clock fault therefore advance the revision before an old
action can resolve. Timestamp, deadline, and counter changes alone do not
refresh. If an observed-state frame cannot be committed, immediate Stop and a
latched UI fault contain the uncertainty. This remains cooperative host
ordering; it does not make the outbound and UI snapshots atomic under target
concurrency.

A separate `OTPD0/v0` diagnostics adapter converts one validated position UI
service result into a fixed 32-bit public event and fixed logger message. It
records only coarse event/outcome, the displayed position notice, a normalized
reason, and presentation/change/containment flags. Idle polls are suppressed;
revisions, timestamps, runtime counters, coordinates, packet/message content,
identity, addresses, credentials, and free text are not event fields. This
does not select target retention, persistence, export, or physical service
workflow. A separate host-only operator decoder accepts exactly the canonical
uppercase `OTPD0=XXXXXXXX` record, reruns the binary word validation, and emits
stable names for the coarse v0 fields. It performs no device, network, file,
retention, or export work and does not make target logging operational.

Target-facing position presentation must combine scheduler state with the
outbound coordinator status. A coherent latched clock rollback/source failure
overrides the scheduler's stopped state with a critical no-action frame, while
incoherent runtime/scheduler combinations also fail closed. Start is rejected
against that latched status even if it was resolved from a previously displayed
healthy revision; stop remains safe and idempotent. The lower-level scheduler-
only mapping remains a host component, not sufficient target composition by
itself. Exact target synchronization and physical rendering/input remain gates.

A separate host-tested position-sharing control adapter maps scheduler state to
the existing semantic local-interface boundary. It exposes start only while
stopped, stop while active/waiting/deferred, and no execution action for a
terminal scheduler fault. Start arms scheduling without servicing or sending a
payload; stop changes only scheduler state. Frame revisions keep delayed input
from activating an obsolete privacy action. Renderers own localized wording and
physical controls, while this adapter contains no coordinates, identity, radio,
emergency, update, or target-driver authority.

The host-only group-load model provides a shared accounting baseline for the
four-client, four-plus-repeater, and eight-plus-repeater field phases. It uses
the exact LoRa airtime calculator and exposes source attempts and forwarding
copies separately. It is a planning input only: collisions, channel access,
protocol overhead, RF behavior, and regulatory acceptance still require direct
measurement and review.

The first live-test composition is narrower than the protocol's eventual
capacity: exactly four identical standalone clients and no infrastructure
dependency during a one-hour session. The versioned `OTFP0/v0` plan records the
session classes, generated traffic, peer-delivery opportunities, provisional
acceptance limits, privacy declarations, and hardware freeze. Validation refuses
a `ready` plan until the exact client model and firmware satisfy self-contained
power, enclosure, GNSS, display, input, and USB recovery gates. Later
four-plus-repeater and eight-plus-repeater phases are separate evidence steps,
not assumptions inherited from this pilot.

One `OTPR0/v0` aggregate result is evaluated against one ready `OTFP0/v0` plan.
The evaluator separates an eligible measured failure from an ineligible setup
and malformed or privacy-unsafe evidence. Expected origins and peer-delivery
opportunities are derived from the plan rather than accepted from the result,
and exact frozen hardware/firmware plus topology/dependency checks precede the
acceptance thresholds. Raw captures remain outside this public boundary.

## Identity, joining, and security

Node identity, human-readable name, group membership, and cryptographic identity are distinct concepts. QR/join codes may simplify provisioning but must not expose long-lived group secrets in reusable plaintext. Encryption/authentication, key rotation, lost-node removal, physical reset, and recovery remain design decisions. Security must be threat-modeled before packet v1 is frozen.

The accepted crypto decision gate benchmarks Espressif's libsodium component
first against pinned ESP-IDF mbedTLS/PSA and Monocypher on the exact client.
Noise XK is only a leading invitation prototype when a signed invitation binds
the administrator identity to its separate X25519 Noise static key. Routine
group traffic remains a separate sender-key/nonce-counter design. No lifecycle
Boolean, received packet, display name, or parsed QR can manufacture production
authentication evidence.

Before a sender-specific traffic key can protect packet v1, its outbound nonce
domain needs rollback-safe allocation. The `OTCN` two-slot store commits a
64-bit high-water range before counters are returned and uses a persistence
domain separate from configuration, secret material, and ACK session state.
Restart may waste a reserved range but cannot reuse it under the same 128-bit
domain/group epoch. A host-tested boundary packs the adapter-supplied 32-bit
prefix and nonzero 64-bit counter only after full lease/key domain equality.
Exact cryptographic domain/key/prefix derivation remains part of the future
authenticated packet contract.

The public derivation context is now canonical: `OTKD/v1` binds the nonzero
group ID, epoch, full authoritative sender fingerprint, and one of three
purpose bytes for the group AEAD key, nonce prefix, or counter-domain ID. This
prevents alias/name substitution and cross-purpose reuse at the encoding
boundary; the audited KDF, epoch secret, output handling, and target vectors
remain unselected.

Packet-v1 sizing is modeled separately from wire-format implementation. The
current candidate accounting reserves 44 authenticated header bytes for
envelope/immutable-forwarding/fragment/epoch/sender/destination/message/counter/
length fields and a 16-byte tag per frame. Each fragment pays the full 60-byte
overhead and receives
its own counter. These are replaceable budget inputs, not frozen offsets,
algorithm identifiers, or authorization to fragment. Mutable hop/routing fields
cannot simply be changed under an end-to-end tag; the first-release header has
no TTL, while a future per-hop wrapper or
other authorized forwarding construction may increase this minimum budget.

After a future crypto adapter authenticates each fragment, a bounded reassembly
boundary may hold four concurrent messages with at most 16 fragments and 103
plaintext bytes per fragment. It binds group/epoch/sender/message/count,
reorders by index, treats exact duplicates idempotently, clears conflicts, and
releases only a complete message. Raw packets cannot enter this boundary, and
the host type does not itself supply cryptographic proof.

For the initial zero/one-repeater topology, Decision 0004 removes that mutable
field instead of inventing a tag exception: a validated repeater forwards the
exact immutable protected bytes once. Named sender claims additionally require
source authentication beyond common group AEAD access. A signed-group candidate
adds 64 bytes; pairwise symmetric protection is a unicast comparison. Multi-
repeater routing needs a new reviewed outer construction or packet version.

## Location and time

GPS is an optional provider behind an interface exposing fix validity, age, position, altitude, heading, speed, accuracy when available, and UTC. Messaging continues without a fix. Alerts clearly indicate missing or stale position. Time-dependent protocol logic must tolerate nodes booting without UTC.

## Offline maps

Map rendering and map-package ingestion are separate from radio networking. LoRa never distributes map packages. A phone/computer may transfer packages locally, potentially through Wi-Fi SoftAP or removable storage, after hardware evaluation.

The package contract should be replaceable and include format/version, coverage, zoom/detail limits, attribution/license metadata, integrity, and storage requirements. Public `tile.openstreetmap.org` is not an offline bulk-download source; a provider or self-hosted pipeline must expressly permit offline use and redistribution.

## External critical-alert interface

OpenTrail accepts normalized events, never raw CAN/J1939 frames. The boundary should support at least schema version, event type, severity, source/vehicle identity, event time/age, optional typed value/unit, validity, and diagnostic context. OpenTrail adds its own node and current GPS context before radio transmission. Producers are untrusted inputs: values, lengths, rates, and event types require validation and rate limiting.

The bounded v0 contract is now specified in
`docs/integration/OPENGAUGE_CRITICAL_ALERT_V0.md`. It uses an explicit 64-byte
codec, opaque lifecycle identities, canonical units, CRC-32 corruption
detection, and a transport-supplied authenticated/authorized producer context.
OpenTrail host tests enforce freshness, duplicate/conflict, monotonic-time, and
emergency-reserve rate policy. The contract alone does not validate a physical
transport or replace transport authentication and replay protection; OT-017D
and OT-017E add only the bounded host-mediated adapter evidence described below.

The mirrored `OGK0` acknowledgement contract records accepted or rejected
disposition, a canonical rejection reason, the original producer/event/
condition/lifecycle identities, consumer identity and boot-session sequence,
and bounded observed age in a separate 64-byte frame. Independent codecs in
OpenTrail and OpenGauge round-trip the same normative bytes. The CRC detects
corruption only; authenticated transport identity, persistent replay handling,
delivery-controller/outbox correlation, and on-device physical delivery remain
separate integration gates.

The host-tested ACK responder now accepts only a final alert-ingress decision
and its exact trust/time context. Accepted and byte-identical duplicate alerts
produce accepted/none; authenticated policy rejection maps to a canonical
negative reason; malformed, unauthenticated, producer-mismatched, and local
clock-rollback input produces no response. It adds monotonic elapsed alert age
and advances its boot-session sequence only after successful encoding.
Authenticated response transport and per-session sequence persistence remain.

A host-tested commit-last allocator now assigns the responder a durable nonzero
boot-session ID before use. Its 64-byte `OTAS` record binds consumer identity
and authorization epoch, alternates two protocol-state slots, and increments
the session on every successful boot allocation. Corruption, ambiguous equal
generations, epoch/identity changes, and uncertain committed state fail closed;
an explicit reset and new seed is required for rebind/recovery. The ACK sequence
remains RAM-only within each newly allocated session. Target storage binding,
trusted anti-rollback, and physical power-loss evidence remain.

A bounded host-mediated physical adapter now carried the exact 64-byte `OGA0`
alert through one Heltec, applied the real ingress/responder C++ path on the
host, and returned the correlated 64-byte `OGK0` through the other Heltec in
both role directions. The SenseCAP recorded exact aggregate +4 flood RX/TX and
all temporary state was cleaned. This proves byte transport and composition on
the bench, not authenticated peer binding, on-device OpenTrail firmware, a
repeater-required route, or field delivery.

OT-017E strengthened two later role-reversed cycles by feeding each returned
physical ACK into OpenGauge's real peer authorization, session binding,
replay/correlation ingress, and exact outbox completion path. Both reconstructed
outbox entries completed with one acknowledgement and zero remaining queued or
in-flight entries. The host reconstructed OpenGauge state only after receipt,
so this is cross-repository component composition evidence rather than a
persistent on-device pipeline.

OT-017F then exercised the negative branch over the same physical path. Two
role-reversed stale-policy responses remained exact correlated rejections;
OpenGauge processed both but recorded zero acknowledgements,
`outbox_completed=false`, and terminal remote-rejection failure. This proves the
host composition does not silently convert this rejection into delivery
success. Retryable rejection, persistent-state interruption, and target binding
remain separate gates.

OT-017G exercised the retryable negative branch. In two role-reversed physical
cycles, OpenTrail's real rate policy produced rejected/rate-limited after its
general allowance was filled. OpenGauge processed each exact response with zero
acknowledgements/completion, released one queued retry, and avoided terminal
failure. Durable backoff state and a later physical retry-to-accept sequence
remain unproved.

OT-017H added the subsequent physical retry and accepted response in both roles.
The composed OpenGauge verifier enforced not-ready at 25 ms, byte-identical
preparation at 26 ms, and final completion only after the next-sequence accepted
ACK. It still reconstructed the lifecycle after both responses; durable live
state across the physical wait remains a target gate.

OT-017I replaced post-hoc reconstruction with one live OpenGauge host process
started before the first radio send. Its real authorization, replay, and outbox
state remained alive through rejection, exact backoff, physical retry, and final
completion in both roles. Durable restart recovery and on-device target state
remain unresolved.

Initial transports to evaluate are a local serial interface and an authenticated local wireless interface. The semantic event schema should remain transport-independent.

## Persistence and diagnostics

Persistent configuration is schema-versioned, checksummed, recoverable to safe defaults, and separated from secrets. The duplicate window has a fixed canonical `OTD0` checkpoint codec carrying remaining lifetimes across monotonic-clock restarts plus a context/epoch-bound `ODS0/v1` two-slot generation/readback/recovery boundary. Legacy unbound v0 and mismatched group media require service rather than implicit restore or overwrite. No protected target storage binding, authenticated integrity, or secure rollback primitive exists yet. Store-forward queues require explicit capacity, expiry, and wear strategy. Logging uses compile/runtime levels `ERROR`, `WARN`, `INFO`, `DEBUG`, and `TRACE`; release builds can remove verbose paths. A production-facing RAM sink retains the newest 32 canonical records, assigns boot-local sequences, snapshots all retained entries oldest-first, and counts overwrite and rejection without allocating. It is not a serialized/persistent format and is not internally synchronized; exact target composition must serialize access. Diagnostics must not leak secrets.

Hardware-test publication uses a separate `OTFL0` boundary. Raw captures and
recovery journals remain local; the converter emits only aggregate counters,
latency, coarse environment, verified configuration, and cleanup under neutral
role labels. The public validator rejects identity-, secret-, transport-port-,
channel-name-, and precise-location-bearing fields before evidence is committed.

Update recovery uses a separate host-tested `OTRD0/v0` diagnostic adapter. One
coherent redacted boot/save/transition status becomes one fixed 32-bit word and
one canonical message through the existing logger. Generation values and all
identity, policy, checkpoint, key, address, and raw adapter detail are omitted.
Magic, version, reserved bits, enums, state/action/reason coherence, and the
mandatory redaction bit fail closed. The bounded RAM ring now supplies an exact
in-memory sink and retains/decodes `OTRD0` in host tests. A separate semantic
adapter maps decoded outcomes into the checked local-interface contract without
adding execution authority. Target task binding, concurrency, persistent
retention/export, rendering, power-loss behavior, and physical failure capture
remain gates.

## Failure boundaries

- GPS loss marks positions unavailable/stale but does not stop messaging.
- Map/storage failure leaves messaging and alerts available.
- UI failure must not wedge radio processing.
- OpenGauge loss removes vehicle-derived events only.
- Peer/repeater loss triggers retries/expiry without an infinite queue.
- Corrupt, incompatible, or unauthenticated packets are rejected safely.

## Architecture gates before product firmware

1. Confirm exact development boards, radio region, frequency plan, antennas, and legal operating constraints.
2. Measure two-node LoRa airtime, loss, latency, and usable payload behavior across candidate settings.
3. Define identity/security threat model and packet-size budget.
4. Freeze only a minimal experimental packet envelope, then validate direct and controlled-forwarding behavior.
5. Benchmark candidate display, storage, map, GPS, and local-transfer options before selecting UI/map technologies.
