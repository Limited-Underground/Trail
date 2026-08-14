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

The laptop-only [dual virtual-LCD simulator](testing/DUAL_VIRTUAL_LCD_SIMULATOR_V0.md)
is an injected host presentation and test-transport boundary. Two independent
WPF windows render only the fixed logical primitives emitted by a bundled native
host around the shared C++ `PortableUiShell`, compact `UiFrame`, exact-offer
`UiPresentationSidecar`, and `PortableUiRenderPlan`. Portable screen state,
fixed text templates, ordered action slots, revisions, read/selection state,
and typed request correlation therefore have one renderer-neutral C++
authority; Windows connection and evidence controls remain separate host
chrome. Keeping copied message/text presentation in the shell-owned sidecar
preserves the existing 24-byte `UiFrame` embedded ABI and result-object budget.
Offer/present/commit is two phase, so failed rendering or stale generation/
revision input cannot advance portable state or emit a request.

Its Core bridge owns bounded session/queue/message snapshots rather than C++
transport storage. Local-loopback delivery distinguishes queued, bridge-
accepted, bridge-observed, and bridge-acknowledgement-observed evidence. A
passive Windows VID/PID adapter can list public candidate labels without opening
or querying hardware; any USB session requires explicit selection and an exact
private binding/runtime recheck. The companion helper's current `OTS0` protocol
frames fixed quick-status and critical-alert traffic and can decode an inbound
correlated `OTS0:A` observation. The live application admits only the outbound
fixed quick/critical requests: USB acknowledgement requests, template/arbitrary
chat, archive, and position requests fail closed. An observed ACK can advance
only an already-outbound matching critical alert; an unmatched observation
cannot advance alert state. All of this is unauthenticated simulator test
traffic. No simulator state proves the production OpenTrail packet, LoRa
transmission, authenticated peer, physical renderer/input target, target
firmware, or supported hardware.

[Decision 0007](decisions/0007-shared-client-presentation-tracks.md) keeps the
planned Android companion and self-contained touchscreen tracks under the same
versioned protocol and reusable behavior in this repository. Platform and
hardware adapters remain separate, and evidence from one track cannot be
credited to the other. The current four-self-contained-unit first release and
its no-phone/no-laptop field boundary remain authoritative; the simulator is a
development reference, not a field dependency.

Update checkpoint persistence now has a backend-neutral
[key/value target boundary](update/UPDATE_CHECKPOINT_KV_TARGET_ADAPTER_V0.md).
It fixes the `ot_state` partition, `ot_update` namespace, two exact 64-byte
slot keys, and explicit backend commit after write or erase. Missing-key erase
is idempotent; wrong-sized values and every failed or uncertain commit stay
visible to the upper two-slot recovery store. This is NVS-ready host evidence,
not an ESP-IDF backend or physical durability result.

Update recovery presentation reuses that same semantic boundary. A valid
`OTRD0/v0` outcome maps to fixed status or system-fault notice enums; invalid
diagnostics fail visibly to a generic critical service frame. Only nonblocking
trial, rejected-transition, and cleanup notices offer acknowledgement, which
cannot confirm update health, execute cleanup, request service, or reboot.
Exact target scheduling, renderer wording, revision ownership, and physical
recovery behavior remain target gates.

The Windows loader has a separate
[selected-device bundle matcher](update/WINDOWS_LOADER_DEVICE_BUNDLE_MATCH_V0.md).
It requires a separately authoritative received-unit profile and compares only
exact hardware-profile ID, processor, target role, received revision,
bootloader schema, and image capacity with the inspected manifest. USB/runtime
labels and vendor-family baselines cannot supply that authority. A match is
compatibility evidence only and cannot grant release admission or Flash
permission. No current bench card has an authoritative profile.

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

One identity-free 12-byte `OTQ0/v0` payload now fixes four generic quick-status
meanings: I'm OK, Need assistance, Anyone online?, and Available to help. The
payload carries only magic/version/length, one semantic value, a canonical
reserve, and CRC-32. It contains no sender, group, location, timestamp, message
ID, acknowledgement, text, key, or route. CRC is accidental-corruption
detection, not authentication. Packet-v1 type/priority/replay/expiry/ACK,
outbound admission, revision-safe menu selection, and target/radio evidence
remain separate gates. See the
[quick-status payload contract](protocol/QUICK_STATUS_PAYLOAD_V0.md).

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

The optional archive uses a separate host-tested session around the same
current-fix-only scheduler. Explicit local start supplies an opaque nonzero
session ID that must increase within one object lifetime; stop is immediate.
Each accepted capture becomes one canonical 56-byte `OTBA/v0` record containing
only that session ID, a session-local sequence, boot-local capture time, the
existing exact 16-byte position payload, canonical reserves, and CRC-32.
Sequence advances only after the injected nonblocking transport accepts the
record; retryable pressure reuses the same sequence. The transport contract's
success is local acceptance, not a remote-server acknowledgement or persistence
claim. This boundary contains no stable device/participant identity, provider,
URL, credential, account, retention authority, or radio control. A server,
authenticated/encrypted transport, offline queue, restart-safe session source,
authorization, storage, export/deletion, target binding, and physical evidence
remain separate gates. See
[the archive session contract](location/BREADCRUMB_ARCHIVE_SESSION_V0.md).

The session's first concrete transport is a 16-record fixed-memory outbox. It
validates every exact `OTBA/v0` record, enforces contiguous sequence within
strictly increasing sessions, preserves FIFO order, and reports full without
overwrite or eviction. A cooperative uploader removes the exact head only when
an injected remote adapter reports `durable_ack`; not-ready, rejection, and
failure retain it. A post-ACK local commit mismatch latches upload closed for
reconciliation. Records are zeroed when committed or explicitly discarded, but
ordinary RAM zeroing is not certified secure erasure. Only session/sequence
ordering metadata remains after removal. This is volatile host behavior, not
protected persistence or remote durability evidence. See the
[bounded outbox contract](location/BREADCRUMB_ARCHIVE_OUTBOX_V0.md).

A separate checked-time retry coordinator prevents a target loop from turning
remote pressure into an unbounded upload loop. It reads the guarded boot-local
clock before any attempt, retries transient not-ready/failure outcomes with a
bounded doubling delay, permits an attempt at the exact deadline, and resets
the delay only after durable acknowledgement plus exact local commit. Temporary
clock not-ready defers without upload; rollback, source failure, remote
rejection, deadline overflow, or uploader ambiguity retain the FIFO and latch
this optional boot composition closed. It has no base-radio or capture
authority. See the
[checked-time retry contract](location/BREADCRUMB_ARCHIVE_RETRY_V0.md).

A pure presentation adapter reduces copied archive session, outbox, and retry
state to coordinate-free stopped, active, queued, waiting, full, or failed
semantic UI notices plus a bounded queue count. It offers no actions and does
not turn an optional archive failure into a base system-fault claim. Invalid
owner combinations fail visibly while impossible counts are redacted. See the
[privacy-safe archive presentation contract](location/BREADCRUMB_ARCHIVE_PRESENTATION_V0.md).

Target-facing capture of that presentation now uses one abstract status-source
call for the complete session/outbox/retry tuple. Common code never performs
three independent owner reads, refuses revision zero before source access,
retains the prior frame on temporary not-ready, and ignores partial data from a
failed or unknown source before emitting the generic action-free warning. The
interface requires a target implementation to serialize the copy but does not
provide an ESP-IDF task or lock. See the
[single-read archive snapshot contract](location/BREADCRUMB_ARCHIVE_SNAPSHOT_V0.md).

A target-shaped adapter now applies that contract to the three concrete archive
owners under one injected nonblocking lock. It publishes the tuple only after a
balanced successful release; contention defers with redacted output, while
lock/unlock uncertainty redacts and latches the optional snapshot path closed.
The target must still supply the real synchronization primitive and ensure
every writer uses the same domain. No ESP-IDF mutex, concurrent-writer proof, or
on-device evidence exists. See the
[serialized archive snapshot adapter](location/BREADCRUMB_ARCHIVE_SNAPSHOT_ADAPTER_V0.md).

The corresponding target-shaped runtime owner now constructs the concrete
capture session, outbox, uploader, retry coordinator, and snapshot adapter as
private members. Start, stop, capture, upload service, and snapshot access all
pass through the same injected lock, so target composition receives no direct
mutable-owner reference. Component rejection stays separate from lock failure;
unlock uncertainty after a mutation latches this optional path without an
unsafe compensating call. Target-exclusive workflow composition, ESP-IDF
binding, concurrent task proof, and physical behavior remain absent. See the
[serialized archive runtime owner](location/BREADCRUMB_ARCHIVE_RUNTIME_OWNER_V0.md).

Archive start/stop now has a separate local-only consent boundary. Start is a
hold on the exact active canonical archive-confirmation revision and requires
one checked monotonic sample; Stop is immediate and clock-independent from its
own exact local confirmation frame. Cancel, stale/wrong-screen input, clock
failure, and unsupported actions reach no runtime operation. The controller has
no radio/server/automatic input. Renderer, physical input, parent navigation,
and target call-path proof remain absent. See
[local archive consent](location/BREADCRUMB_ARCHIVE_LOCAL_CONSENT_V0.md).

The complete local archive workflow now keeps the serialized runtime, local
consent controller, snapshot-backed control screen, Start/Stop confirmation,
Cancel, and post-action refresh in one cooperative revision sequence. It reads
a fresh serialized snapshot before polling a control action; unknown or
incoherent state can expose Stop but never Start. Start remains hold-only and
Stop remains immediate. If a successful Start cannot be followed by a truthful
new frame, or if revisions are exhausted, the workflow attempts a privacy-safe
Stop and latches. The workflow has no radio/server/automatic Start input, but a
target shell must still own local entry/exit navigation and the runtime object
must remain inaccessible to unapproved call paths. See the
[archive workflow contract](location/BREADCRUMB_ARCHIVE_WORKFLOW_COORDINATOR_V0.md).
After exit, re-entry requires `open_archive_controls` already resolved against
the exact active parent revision; the same boot-lived coordinator retains its
session sequence rather than recreating it.

A cooperative archive UI coordinator now owns the corresponding semantic-frame
revisions. Every valid service call takes exactly one fresh serialized snapshot;
unchanged state consumes no display write or revision, temporary source/display
unavailability retains the last truthful frame, and failed or incoherent status
attempts the existing redacted warning. The owner has no capture, outbox,
upload, storage, input, or radio reference, so optional presentation failure
cannot control base operation. No target task/lock, renderer, or physical
display is implemented. See the
[single-owner archive UI contract](location/BREADCRUMB_ARCHIVE_UI_COORDINATOR_V0.md).

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

The [first-release capacity policy](testing/FIRST_RELEASE_CAPACITY_V0.md) fixes
the intended v0 product boundary at no more than eight active clients in one
group plus at most one optional authorized repeater. Support must be earned in
order: four standalone clients, four clients plus the repeater, then eight
clients plus the repeater. No server, internet, phone, laptop, vehicle, or
repeater may become a base-client dependency. This is a planned release ceiling
and acceptance sequence, not current field-capacity evidence.

The [product boundary map](PRODUCT_BOUNDARIES_V0.md) keeps one self-contained
base client separate from optional repeater, server/archive, OpenGauge vehicle,
offline-map/large-display, and post-session management roles. Optional roles
may exchange only their documented inputs/outputs and must fail without taking
base messaging, local status, critical presentation, or USB recovery with them.

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

The dated [offline-map architecture gate](maps/OFFLINE_MAP_ARCHITECTURE_V0.md)
now compares MBTiles 1.3, PMTiles v3, and a pre-rendered indexed-raster
reference. It fixes off-device preparation, visible attribution, immutable
read-only activation, prior-good recovery, and mapless degradation while
leaving the final container, renderer, storage, transfer, and limits open until
the exact display target is measured.

The separate [`OTMP0/v0` manifest](maps/OFFLINE_MAP_PACKAGE_MANIFEST_V0.md)
makes candidate admission executable off-device. It strictly binds source/
rights/attribution, experimental container/encoding, coverage, exact length,
SHA-256, reader, firmware, storage, and scratch requirements. Its verifier is a
read-only preflight—not package authentication, staging, activation, rendering,
or target compatibility evidence.

The bounded [map activation guard](maps/OFFLINE_MAP_ACTIVATION_GUARD_V0.md)
makes the lifecycle policy executable without choosing the persistence layer.
It admits only fully evidenced alternate-slot candidates, changes trial state
only after exact external selector-commit evidence, retains the prior package
until bounded healthy reads pass, and otherwise requires exact fallback or a
visible mapless state. It owns no filesystem, selector codec, authentication,
renderer, radio, messaging, alert, position-sharing, or USB authority.

The fixed 64-byte
[`OTM0/v0` selector checkpoint](maps/OFFLINE_MAP_SELECTOR_CHECKPOINT_V0.md)
serializes only stable/trial/fallback state, abstract slots/generations, bounded
trial policy, and CRC. Restore revalidates exact package evidence, resets
volatile health, bounds trial reboots, preserves prior recovery, and never
guesses missing media. CRC is not authentication or rollback protection; a
recoverable physical store and trusted-generation policy remain separate.

The [abstract two-slot store](maps/OFFLINE_MAP_SELECTOR_STORE_V0.md) prepares a
complete `OTM0` with a zero commit byte, commits byte 59 last, verifies exact
readback, alternates away from prior-good state, repairs known degraded peers,
and rejects unreadable media or equal-generation conflict. It accepts an
external trusted floor but does not own anti-rollback state.
Its service-grade clear attempts both selector erases and requires both records
to read back exactly empty; reported erase success alone is insufficient.

The [map selector boot coordinator](maps/OFFLINE_MAP_SELECTOR_BOOT_COORDINATOR_V0.md)
restores only into a private guard. Stable state can be released unchanged,
while a resumed-trial increment or boot-limit fallback must pass a new
commit-last write and exact readback first. Failures publish at most a fresh
mapless guard; an unpersisted candidate is never exposed. The coordinator does
not bind a physical backend, own the trusted floor, open a package, render a
map, or control communications.

The [trusted boot coordinator](maps/OFFLINE_MAP_SELECTOR_TRUSTED_BOOT_COORDINATOR_V0.md)
is the first composition that removes a caller-created floor from a selector
path. It inspects the protected-generation source before selector storage,
runs ordinary boot restore/save against a private guard, then rechecks an
unchanged value or atomically advances and exactly reads back a newer value
before publication. Nonzero trusted history with empty selector media is
service-required, and any uncertain trust advance keeps the saved selector
private. Target composition must serialize both storage and trust ownership;
reset/replacement remains a separate gate.

The [trusted runtime transition coordinator](maps/OFFLINE_MAP_SELECTOR_TRUSTED_TRANSITION_COORDINATOR_V0.md)
removes the caller-created current/floor values from live selector mutation.
It obtains both from protected history, runs the ordinary transition against a
private guard, and publishes only after an unchanged generation is rechecked or
a newly saved selector generation is atomically advanced and exactly read back.
Trust failure or change contains the current map. Verified selector clearing
after invalid fallback deliberately retains protected history and requires
service reconciliation rather than becoming a clean first-use condition.

The [runtime transition coordinator](maps/OFFLINE_MAP_SELECTOR_TRANSITION_COORDINATOR_V0.md)
requires the live guard to exactly match the newest persisted checkpoint and
caller-held generation before applying trial reads, trial time, fallback
completion, or prior cleanup to a private copy. Persistent changes become live
only after commit-last save and exact readback. Volatile health/time changes do
not create unnecessary writes. An invalid fallback verified-clears only the
selector records before becoming mapless; uncertain clearing remains mapless
and reconciliation-required. This lower-level coordinator remains independently
testable with scalar generation context; protected runtime composition is owned
by the trusted coordinator above.

The [candidate coordinator](maps/OFFLINE_MAP_SELECTOR_CANDIDATE_COORDINATOR_V0.md)
owns the replacement ordering boundary for an externally staged alternate-slot
package. It requires one stable active baseline, applies staging and selector
commit to a private guard, rechecks the exact preflight generation at save
time, and exposes trial state only after commit-last exact readback. Candidate
validation rejection leaves the current map unchanged; persistence uncertainty
fails mapless. It deliberately does not create the first map baseline because
a restart-safe trial requires an exact prior-good package. Calls require
exclusive selector-store ownership; the generation check is not a lock.

The [trusted candidate coordinator](maps/OFFLINE_MAP_SELECTOR_TRUSTED_CANDIDATE_COORDINATOR_V0.md)
removes caller-created generations from replacement. It reads protected history
before selector access, runs the ordinary candidate save against a private
guard, advances and exactly reads back trust after selector persistence, and
only then publishes trial state. Rejected candidates recheck unchanged trust
before retaining the active map. Post-save conflict or uncertainty contains map
exposure and requires fresh-boot reconciliation. Target composition must also
serialize physical package-slot staging and retention, which common host code
does not implement.

The [first-baseline coordinator](maps/OFFLINE_MAP_SELECTOR_BASELINE_COORDINATOR_V0.md)
provides the separate initial-install path. It accepts only a clean mapless
`no_selector` guard, exact policy, fully evidenced package, two readable empty
selector slots, and zero trusted generation history. It creates stable active
record generation 1 privately and publishes only after commit-last exact
readback. It cannot reset or reseed a used selector domain; any previous,
dirty, unreadable, or changed state remains mapless and requires service or
reconciliation. Calls require explicit provisioning authority and exclusive
store ownership, neither of which this host boundary implements.

The [protected first-baseline coordinator](maps/OFFLINE_MAP_SELECTOR_TRUSTED_BASELINE_COORDINATOR_V0.md)
removes the caller-created zero-history claim from that initial install. It
inspects protected history before selector access, permits only exact zero,
saves stable selector generation 1 against a private guard, then atomically
advances protected history from 0 to 1 and requires exact readback before map
publication. Retryable initial source failure preserves the exact clean
`no_selector` owner. Nonzero history blocks selector access, while every
post-save trust conflict or uncertainty fails ambiguous-mapless and requires
fresh-boot reconciliation.

The [service-reseed coordinator](maps/OFFLINE_MAP_SELECTOR_RESEED_COORDINATOR_V0.md)
is the separate recovery path for a dirty or previously used selector domain.
It requires a mapless service owner and a fresh exact-operation permit,
advances beyond the greatest observable/trusted generation, verified-clears
only the two selector records, rechecks exact emptiness at save time, and
publishes a stable baseline only after commit-last exact readback. Clean first
use and healthy active replacement are deliberately rejected into their normal
coordinators. The permit is consumed before store access, but the generation
floor remains a scalar input at this independently testable lower layer.
Physical package/storage behavior remains outside this host boundary.

The [protected service-reseed coordinator](maps/OFFLINE_MAP_SELECTOR_TRUSTED_RESEED_COORDINATOR_V0.md)
derives that floor from protected history before selector access and requires
the permit to match it exactly. Selector clear and replacement save happen on a
private guard. Protected history advances only after exact selector readback,
and the recovered map is published only after exact protected readback. Initial
source failure reaches no selector storage; every post-save trust conflict or
uncertainty remains ambiguous-mapless for fresh-boot reconciliation.

The [reset/replacement lifecycle policy](maps/OFFLINE_MAP_SELECTOR_RESET_REPLACEMENT_POLICY_V0.md)
keeps ordinary factory reset, authorized selector reseed, same-device protected-
source recovery, and whole-device commissioning separate. Ordinary reset
preserves selector records plus protected map history. Selector reseed is
routed only while protected history is intact; a temporarily unavailable source
blocks selector access, and a missing or replaced source on the same device
requires future independent external recovery instead of becoming first use.
Independently established blank replacement hardware may route only to future
fresh-domain commissioning, while retained selector import is rejected. The
fixed-shape classifier grants no erase, protected-reset, generation-lowering,
credential, or migration authority. Ten host groups pass; continuity evidence
and execution authority remain target gates.

The [protected-domain authorization handoff](maps/OFFLINE_MAP_SELECTOR_DOMAIN_AUTHORIZATION_V0.md)
derives either same-device domain replacement or blank-new-device commissioning
from that lifecycle policy before contacting an injected target verifier. It
requires local USB, an atomically consumed opaque handle, exact boot/route/media
binding, a nonzero proposed 128-bit domain, six confirmations, a committed local
revision, and a short checked-time window. Retained same-device selector state
may be marked only as quarantined; new-device commissioning requires verified-
empty media. The resulting non-copyable permit can be consumed only by the
domain provisioner and grants no general erase, reset, import, or migration
authority. Ten host groups pass; credentials, continuity proof, secure domain
generation, and durable replay/audit remain target gates.

The separate canonical
[`OTMD/v0` lifecycle record](maps/OFFLINE_MAP_SELECTOR_DOMAIN_RECORD_V0.md)
preserves the fixed `OTM0/v0` selector format. Its 80 bytes bind current and
retired 128-bit domains, a quarantined selector-generation floor, the accepted
selector generation, domain epoch, record generation, commit-last marker, and
CRC. Fresh commissioning permits only epoch-1 pending-first-baseline/active
state with no retired domain. Same-device replacement requires distinct current
and retired domains, pending-reseed/active state, epoch at least 2, and accepted
selector generation strictly above the retired floor. Ten codec groups pass.
The separate abstract
[two-slot domain store](maps/OFFLINE_MAP_SELECTOR_DOMAIN_STORE_V0.md) selects
only a unique newest valid record, requires exact next-generation and lifecycle
linkage, preserves the prior committed slot while committing byte 75 last, and
verifies exact readback. It supports exact maintenance repair,
pending-to-active, monotonic accepted-selector, and linked replacement
successors while refusing unreadable/conflicted/backward/reused-domain state.
It deliberately has no erase/reset API and grants no provisioning authority.
Ten store groups pass. The separate
[permit-consuming provisioner](maps/OFFLINE_MAP_SELECTOR_DOMAIN_PROVISIONER_V0.md)
requires exclusive ownership, burns binding/boot/time authority before I/O,
persists an exact pending lifecycle record before selector clear or protected-
source establishment, and verifies empty selector media plus protected
generation zero while keeping the map stopped. Its protected-source interface
can establish only independently uninitialized state and cannot reset or rebind
an initialized source. Matching pending state is resumable with a new exact
permit. Thirteen groups pass. The separate
[domain activation coordinator](maps/OFFLINE_MAP_SELECTOR_DOMAIN_ACTIVATION_V0.md)
uses selector generation 1 for fresh commissioning or exactly retired-floor-
plus-one for replacement, then persists selector, advances the exact protected
domain/generation, marks `OTMD/v0` active, and publishes only after final
agreement. Pending selector/source/domain steps and an exact already-active
stable baseline are restart-resumable. Fourteen groups pass. The separate
[active trust-domain boot coordinator](maps/OFFLINE_MAP_SELECTOR_DOMAIN_BOOT_V0.md)
then requires exact active-domain, protected-source, stable-selector, policy,
and package agreement; it rereads all three durable owners before publishing
and has no mutation authority. Thirteen groups pass. The separate
[domain-aware candidate coordinator](maps/OFFLINE_MAP_SELECTOR_DOMAIN_CANDIDATE_V0.md)
persists a private trial selector, advances the exact protected domain, advances
the active record's accepted generation, rereads all three owners, and only then
publishes trial state. Thirteen groups pass. The separate
[domain-aware trial boot coordinator](maps/OFFLINE_MAP_SELECTOR_DOMAIN_TRIAL_BOOT_V0.md)
accepts only synchronized state or an exact single-generation interruption gap,
persists resumed trial or boot-limit fallback state privately, advances
protected then accepted domain history as needed, and publishes only after
final three-owner agreement. It also restores canonical active checkpoints
left by interrupted runtime transitions. Fourteen groups pass. The separate
[domain-aware runtime transition coordinator](maps/OFFLINE_MAP_SELECTOR_DOMAIN_TRANSITION_V0.md)
keeps healthy-read progress, promotion, deadline/failure fallback, valid
fallback completion, and previous-package cleanup behind the same exact active
domain. Volatile and rejected operations recheck unchanged durable owners.
Persistent operations save selector generation `N+1`, advance/read back the
protected source, advance/read back `OTMD/v0`'s accepted generation, and reread
all three owners before publication. Invalid fallback evidence can clear only
selector records; retained protected/domain history routes to service rather
than first use. Eleven groups pass. Protected target adapters, physical package
operations, task locking, and physical durability remain separate gates.

The [reseed authorization boundary](maps/OFFLINE_MAP_SELECTOR_RESEED_AUTHORIZATION_V0.md)
can mint that non-copyable, single-use permit only after an injected local-
service verifier returns an exact-bound, short-lived grant and atomically
consumes its opaque handle. It rejects remote radio, wrong boot/scope/transport,
any policy/package/trusted-floor mismatch, missing service confirmation, and
invalid time windows. It carries no credentials or identity. Concrete USB or
local-wireless authentication, challenge generation, administrator policy,
protected replay state, audit, and physical confirmation remain target gates.

The [key/value target adapter](maps/OFFLINE_MAP_SELECTOR_KV_TARGET_ADAPTER_V0.md)
fixes one backend-neutral mapping onto partition label `ot_state`, namespace
`ot_maps`, and two exact 64-byte blob keys. Every prepared write, full-blob
marker rewrite, and erase requires a backend commit before success; the upper
store still owns exact readback and reconciliation. This is an NVS-ready host
boundary, not an ESP-IDF component or physical durability result. Exact target
partition layout, task/lock, encryption, trusted generation, service UI, and
power-interruption evidence remain separate gates.

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
remains RAM-only within each newly allocated session. A separate
[key/value composition](persistence/ACK_RESPONDER_SESSION_KV_COMPOSITION_V0.md)
proves exact `ot_proto` binding, fresh-instance restart rotation, correct
unapplied/applied-then-failed commit recovery, durable two-key reset, reseed,
and wrong-sized-value refusal. This is target-shaped host evidence, not trusted
anti-rollback, authenticated storage, or physical power-loss evidence.

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

Persistent configuration is schema-versioned, checksummed, recoverable to safe defaults, and separated from secrets. The [multi-domain key/value adapter](persistence/PERSISTENT_STORAGE_KV_TARGET_ADAPTER_V0.md) fixes separate configuration, secret-material, protocol-state, and outbound-counter namespaces while preserving the existing 64-byte flash-like erase/write/sync contract. Partial writes accumulate only in an erased RAM working image, sync writes and commits the complete blob, and uncertain mutation latches the slot until erase/restart. The rollback-safe outbound lease store is composed through this adapter in a separate [restart/uncertain-commit proof](security/OUTBOUND_COUNTER_KV_COMPOSITION_V0.md): only `ot_counter` is reached, and a durably applied marker whose commit reported failure causes the unreturned range to be skipped after restart. The namespace is structural isolation, not protected counter storage. The secret namespace name is likewise not a protected-storage claim. The duplicate window has a fixed canonical `OTD0` checkpoint codec carrying remaining lifetimes across monotonic-clock restarts plus a context/epoch-bound `ODS0/v1` two-slot generation/readback/recovery boundary. Legacy unbound v0 and mismatched group media require service rather than implicit restore or overwrite. No protected target storage binding, authenticated integrity, or secure rollback primitive exists yet. Store-forward queues require explicit capacity, expiry, and wear strategy. Logging uses compile/runtime levels `ERROR`, `WARN`, `INFO`, `DEBUG`, and `TRACE`; release builds can remove verbose paths. A production-facing RAM sink retains the newest 32 canonical records, assigns boot-local sequences, snapshots all retained entries oldest-first, and counts overwrite and rejection without allocating. It is not a serialized/persistent format and is not internally synchronized; exact target composition must serialize access. Diagnostics must not leak secrets.

The 80-byte map trust-domain record now has a separate non-erasable
[key/value target boundary](maps/OFFLINE_MAP_SELECTOR_DOMAIN_KV_TARGET_ADAPTER_V0.md)
using `ot_state` / `ot_map_domain` / `otmd_a|b`. It preserves the prepared
record and byte-75 marker order through full-blob rewrites without exposing
erase or reset authority. Protected rollback, authentication, locking, and
physical durability remain target obligations.

The context-bound 704-byte replay checkpoint has a separate
[key/value target boundary](persistence/DUPLICATE_CHECKPOINT_KV_TARGET_ADAPTER_V0.md)
using `ot_state` / `ot_replay` / `ods_dup_a|b`. Exact value length and explicit
backend commit are mandatory, missing-key erase is idempotent, and uncertain
commit remains visible for restart reconciliation. Namespace protection,
authentication, trusted rollback, locking, and physical durability remain
target obligations.

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
in-memory sink and retains/decodes `OTRD0` in host tests. A strict host-only
operator decoder accepts exactly one canonical uppercase logger record and
emits stable coarse category names without file, device, network, retention,
or export access. A separate semantic
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
