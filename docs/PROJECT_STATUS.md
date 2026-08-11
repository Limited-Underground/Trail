# OpenTrail Project Status, Assumptions, and Open Questions

Status date: 2026-08-11

## Conceptual goals

- Offline group communication and location awareness using ESP32 and LoRa
- Portable, vehicle, repeater, and larger touchscreen configurations
- Priority emergency/status messages, store-forward where useful, and graceful disconnection
- Offline local maps and a normalized OpenGauge critical-alert input

The close-range MeshCore path now has bounded transport, experimental OpenTrail packet-v0, and three-node MeshCore repeater hardware evidence including a software-forced route with a repeat-off negative control. Two strengthened role-reversed physical cycles carried 2/2 exact `OGA0` alerts and 2/2 correlated `OGK0` ACKs with zero loss/duplicates/errors and exact aggregate +4 SenseCAP flood RX/TX; each returned ACK then passed real OpenGauge peer authorization, session binding, replay/correlation ingress, and completed its exact reconstructed outbox entry. Fixed-capacity C++ radio, codec, identity lifecycle, group-access policy, non-secret configuration persistence, acknowledgement/retry/expiry, duplicate suppression plus canonical `OTD0` checkpoint serialization and the `ODS0` two-slot host storage boundary, controlled forwarding, priority admission, GPS fix validation/age handling, compact position encoding, LoRa airtime calculation, redacted diagnostics, the OpenGauge critical-alert ingress, mirrored `OGK0` acknowledgement codec, final-ingress-to-ACK responder, and commit-last ACK boot-session allocator have deterministic host tests. Cryptographic joining, target/physical/rollback-aware duplicate-checkpoint storage, persistent secret/group/message-counter state, authenticated acknowledgement/priority transport composition, on-device authenticated alert transport, physical field repeater behavior, physical GPS compatibility/performance, position scheduling/hardware transmission, maps, store-forward behavior, a direct SX1262 binding, rendered UI, and field performance remain unvalidated.

A deterministic group-load model now accounts for the planned four-client
standalone, four-plus-repeater, and eight-plus-repeater phases using exact LoRa
airtime plus explicit source and forwarding transmissions. It is host planning
evidence, not a field-capacity, collision, delivery, range, or regulatory result.

A fixed-memory position scheduler now adds explicit start/stop, current-fix-only
output, delayed-service coalescing, and separate cadence/retry timing around the
existing 16-byte payload. A host-only sink now carries its exact attempt time,
revalidates the current payload, obtains injected ephemeral packet-v0 metadata,
encodes one exact 38-byte frame, and admits it only as background position
traffic. Rate/capacity pressure feeds typed scheduler retry. This is
unauthenticated packet-v0 component evidence: real coordinates remain
prohibited, and no final cadence, authenticated packet/priority composition,
direct-radio/GPS binding, or physical position-sharing result exists.

A fixed-memory, single-owner handoff now peeks the priority/FIFO head and
removes it only after `DeliveryController` accepts the copy. Delivery capacity,
duplicate-ID, and frame-policy rejection retain the original entry; remaining
priority lifetime bounds delivery expiry and cannot be extended. Ten groups
plus 100 repeats include an exact scheduler-to-packet-to-priority-to-delivery
fake-radio flow. This is host composition evidence only, not authentication,
real-coordinate permission, direct-radio binding, or physical delivery.

A cooperative outbound coordinator now obtains one successful checked
monotonic sample and reuses it for location, scheduling, handoff, delivery, and
opaque-radio service in a fixed order. Sharing stopped means no GPS read;
temporary clock not-ready means no downstream call; rollback/source failure
stops sharing and latches the boot composition closed. Ten groups plus 100
repeats cover exact ordering, failure isolation, pressure recovery, and
same-cycle packet delivery to a fake peer. This is not ESP-IDF task/concurrency,
inbound, authenticated-packet, target-adapter, or physical-radio evidence.

A runtime-aware position safety overlay now prevents a permanent outbound clock
fault from appearing as ordinary stopped sharing. Coherent source-failure or
rollback status produces a critical no-action system-fault frame, incoherent
status fails closed, and a Start resolved from an older healthy frame is
rejected without scheduler access. Stop remains safe/idempotent. Ten groups
plus 100 repeats pass. Exact renderer, revision/task ownership, reboot behavior,
and physical input evidence remain absent.

The live coordinator now also owns target-facing position commands. Start reads
one checked sample at action application, defers without scheduler access when
time is temporarily unavailable, and shares the permanent rollback/source-
failure latch with outbound service. Stop applies immediately without reading
the clock. Ten command groups plus 100 repeats, and 100 repeats of the updated
safety suite, pass. Target task synchronization, concrete clock binding,
rendered retry behavior, reboot policy, and physical input remain absent.

A cooperative position-sharing UI coordinator now owns revision allocation,
initial/current publication, one checked input poll, live command application,
and post-action refresh. Temporary Start deferral retains the current truthful
frame. A failed result-frame write or exhausted revision space stops sharing and
latches input closed. Ten groups plus 100 repeats pass. This is not an ESP-IDF
task/lock, renderer, physical input, or concurrency result; exact target
serialization remains absent.

That owner now observes current outbound/scheduler presentation before polling
input. User-visible GPS wait/recovery, sink deferral, and permanent clock-fault
changes publish a higher revision first, making queued older input stale;
nonvisible runtime counters and timestamps do not churn the screen. Failed
observed-state publication stops sharing and latches UI input closed. Ten
additional groups plus 100 repeats pass. Exact target serialization, atomic
snapshots, renderer behavior, and physical timing remain absent.

One validated position UI result can now become a versioned 32-bit `OTPD0/v0`
diagnostic and fixed public logger message. It retains only coarse event,
outcome, displayed position notice, reason, and safety flags; idle polls are
suppressed and identifier/location/runtime detail is structurally absent. Ten
groups plus 100 repeats pass. Target log binding, retention/export/clear policy,
persistence, and physical service capture remain absent. A separate host-only
CLI accepts exactly the canonical uppercase logger record, revalidates the
complete word shape, and emits stable operator category names. It does not
read a device, log, file, or network service and does not establish an export
workflow.

The two strict diagnostic parsers now also sit behind one host-only
`opentrail_diagnostic_cli` entry point. Exact `OTPD0` and `OTRD0` prefixes
dispatch to their original validators and emit a leading stable record type;
malformed supported records and unsupported prefixes fail without echoing the
input. The unified tool adds no file, log, device, network, export, or execution
authority and changes no wire or logger format. Both canonical records plus
malformed/unsupported smoke cases pass in the complete local host gate and in
public GitHub Actions run `31502841481`.

Offline maps now have a dated architecture gate rather than an implied
provider or renderer selection. Public OpenStreetMap tile servers are excluded
from offline package creation; source rights and visible attribution are
mandatory. MBTiles 1.3, PMTiles v3, and a pre-rendered indexed-raster reference
remain candidates for an exact-target comparison. Packages are prepared off-
device, staged and fully verified, activated read-only with prior-good
recovery, and allowed to fail to a mapless UI without stopping communications.
No provider, package, target renderer, storage path, or on-device map result is
claimed.

The `OTFP0/v0` four-person standalone pilot plan fixes the first live-test
boundary at four identical self-contained clients, no repeater/server/internet/
phone/laptop/vehicle dependency during a session, at least three materially
different one-hour sessions, 300 message origins, and 900 peer-delivery
opportunities. Its privacy, traffic math, zero-dependency declarations, and
hardware-readiness transition fail closed under host validation. The plan is
`draft_blocked`: no exact client model or firmware is selected until four units
prove battery, enclosure, GNSS, display, local input, and USB recovery.

The companion `OTPR0/v0` result evaluator now derives the 900 expected peer
opportunities from the plan and emits `pass`, `fail`, `ineligible`, or `invalid`.
It rejects wrong hardware/firmware/topology/duration/dependencies, incomplete
evidence, identity/transport/location/secret fields, impossible deliveries, and
noncanonical shapes before applying the provisional reliability gates. Its six
base groups pass publicly. A fail-closed template generator now refuses blocked
plans and existing output, preloads only frozen public configuration, and leaves
all evidence confirmations false; the expanded nine groups pass publicly. No
live four-person result exists.

The latest bounded hardware run delivered 30/30 alternating two-second
three-radio messages with zero loss/duplicates/new errors, exact +30 repeater
flood RX/TX, repeat preserved, empty final client queues, 2/2 cleanup, and no
lease journal. Its public `OTFL0` record contains aggregate role-labeled evidence
only; the raw capture remains in ignored local build state.

## Decisions captured

- OpenTrail and OpenGauge are separate projects and must remain independently operable.
- No public product name is selected. OpenTrail remains the repository and
  engineering name, while funding material uses a replaceable public-name field.
  ECLU is not assumed in protocols, stored identifiers, device identity, URLs,
  hardware markings, or applications.
- A reusable funding-readiness packet now exists, but it is planning material,
  not an application or award. All cash, hardware, discount, loan, sponsorship,
  and service-credit activity is on owner-directed hold, including opportunity
  research for outreach, contact, submission, account connection, acceptance,
  shipment, and announcement. Legal applicant/payee, opportunity eligibility,
  exact bill of materials, dated quotes, and owner approval remain later gates.
- Website and hosting operations are maintained privately outside this public
  repository. Local device operation cannot depend on the website or any
  future service.
- The initial public field progression is four clients without a repeater, four
  clients plus one repeater, then eight clients plus one repeater. A host-only
  load model accounts for source attempts, forwarding copies, and configured
  LoRa airtime; it is not physical capacity or regulatory evidence.
- The first four-person pilot must operate without a phone, server, repeater,
  laptop, internet, or vehicle connection during the session. Its machine-
  validated plan cannot claim `ready` until an exact four-unit client hardware
  and firmware freeze is recorded.
- The cryptographic benchmark order is now fixed without selecting production
  cryptography: Espressif libsodium first, pinned ESP-IDF mbedTLS/PSA and
  Monocypher comparisons, Noise-C reference only. Noise XK is a leading join
  prototype only with signed invitation key pinning. Exact target benchmark,
  entropy, protected storage, target-bound rollback protection,
  interoperability, and physical lifecycle evidence remain required.

The algorithm-neutral secure-randomness boundary now gives future crypto code a
production-facing interface with explicit not-ready/ready/failed state, bounded
1-64-byte requests, and complete output or no buffer change on every failure.
Its predictable scripted source is isolated under test support. Eight readiness,
failure, exhaustion, retry, transition, and request-boundary groups plus 100
focused repeats pass. No ESP-IDF entropy adapter, strong-DRBG choice,
production key generation, or physical cold-start/brownout/RF-concurrency
evidence is claimed.

The checked monotonic-clock boundary now gives target composition one boot-local
64-bit millisecond source that remains separate from UTC. Equal ticks succeed;
temporary not-ready preserves continuity, while decreasing time or source
failure latches the current guard closed without consuming later samples.
Target code must share one successful value across a cooperative cycle instead
of letting each state machine read hardware independently. Eight groups plus 100
focused repeats pass. No ESP-IDF adapter, task/deep-sleep/brownout behavior,
accuracy/drift, long-run continuity, or physical failure evidence is claimed.

The power-state boundary now normalizes one atomic adapter observation without
assuming battery chemistry or estimating state of charge from voltage. Source
readiness, external power, battery presence, orthogonal charge state, optional
percentage/voltage, and monotonic sample age remain explicit. Composition must
inject nonzero freshness plus distinct low/critical percentage thresholds;
zero/default or incoherent policy fails before reading hardware. Eleven groups
plus 100 focused repeats cover exact bands, charging, external-only operation,
missing readings, reported fault, invalid combinations, future/stale time, and
bounded fake ordering. No target adapter, charger control, hardware threshold,
shutdown policy, endurance, or physical power evidence is claimed.

The local display/input boundary now gives portable-client application logic a
fixed semantic frame with status indicators, notices, and up to four action
slots. Target adapters own pixels, labels, coordinates, buttons, and touch.
Only a successfully presented, strictly increasing boot-local revision becomes
active; stale, disabled, out-of-range, unknown, and source-failed input remains
typed. The critical-confirmation screen has a canonical confirm/cancel shape and
requires a hold before resolving the confirm request. Twelve groups plus 100
focused repeats cover capabilities, frame validation, atomic presentation,
revision/input binding, failure behavior, critical confirmation, system-fault
restrictions, and bounded fakes. No renderer, physical input/readability,
critical-alert delivery, target performance, or supported display is claimed.

The portable-client composition preflight now binds all ten target-facing
endpoints needed by the first self-contained client and aggregates missing,
invalid, insufficient-MTU, insufficient-action, and missing-hold evidence. The
whole-contract audit caught and corrected a real distinction: two 704-byte
`ODS0` replay slots require `DuplicateCheckpointStorage` in addition to the
64-byte multi-domain `PersistentStorage`. The composition reuses pure power and
display validators and queries only radio MTU; it performs no storage, entropy,
clock, GPS, power, display, input, log, send, receive, or service operation.
Eight groups pass in the complete 33-executable matrix. This is structural host
evidence only. No concrete ESP-IDF target, board adapter, partition/pin map,
application task, target build, or physical result exists.

An algorithm-neutral outbound counter prerequisite now has host evidence. The
fixed `OTCN` store commits one nonoverlapping 64-bit range before returning it,
alternates exact readback-verified slots in a separate persistence domain, binds
the high-water mark to a 128-bit domain plus group epoch, and deliberately skips
unused/commit-uncertain ranges after restart. Ten groups pass locally and on
public `main`. A separate seven-group boundary now packs the adapter-supplied
32-bit prefix and rollback-safe counter only after full 128-bit lease/key domain
equality. A fixed 52-byte `OTKD/v1` encoder separately binds group, epoch, full
sender fingerprint, and three output purposes across eight groups. Exact KDF,
epoch-secret handling, AEAD, target protected storage, secure rollback, and
physical power interruption remain unproved.

The immutable one-repeater host path now has a reboot coordinator around its
duplicate state. It requires protected namespace evidence matching the expected
group/epoch, requires the store and fixed `ODS0/v1` record to embed that exact
binding, restores or repairs the checkpoint before operation, and readback-
verifies a new replay checkpoint before allowing a queued frame to transmit.
Wrong-group/epoch and legacy unbound v0 media require service without restore or
overwrite. Failed or uncertain persistence and unreadable media disable
forwarding. Ten store groups, nine coordinator groups, the complete 28-
executable matrix, and 100 focused repeats of both suites pass locally. The
context-bound published matrix passes on public `main` in run `31374678550`.
The RAM
queue is not durable, so the accepted safety tradeoff can lose a frame after
replay save and before transmit. Target protected binding, rollback protection,
physical power-cut/wear, and durable-outbox behavior remain unproved.

The host-tested `OTCB0/v0` benchmark boundary now requires exact board,
toolchain, dependency-lock, sdkconfig, radio, timing, memory/stack/flash,
watchdog, artifact-hash, and eight security/lifecycle gate fields. Eight groups
show that a blocked plan cannot create a template and incomplete, mismatched,
private, or measured-failing evidence cannot pass; the suite passes locally and
on public `main`. The public plan remains `draft_blocked`; this is
reproducibility tooling, not target cryptographic evidence or a library
selection.

A separate protected-packet sizing model makes candidate security overhead
explicit without freezing packet v1. Requirements reconciliation found that the
former 36-byte profile omitted space for the required authenticated 64-bit
destination alias. Under the corrected 44-byte header plus 16-byte-tag profile,
a 163-byte example MTU leaves 103 plaintext bytes; the existing 16-byte position
payload becomes a 76-byte frame with 276,992 us theoretical airtime at the bench
PHY. The signed-group candidate adds a 64-byte source signature, leaving 39
plaintext bytes and making that position 140 bytes/461,312 us. A 64-byte signed
`OGA0` alert or `OGK0` ACK requires two candidate fragments, 312 transmitted
bytes, and 1,025,024 us theoretical source airtime. All ten groups pass locally and on public
`main`. Final fields, nonce/signature construction, crypto, target MTU/airtime,
and fragmentation/reassembly remain unproved; no alert is approved for this
candidate framing yet.

A separate fixed-memory verified-fragment reassembler now bounds four concurrent
messages, 16 fragments, and 103 bytes per fragment. Ten host groups cover the
39+25-byte alert shape, reorder, duplicates, conflicts, capacity, timeout,
clock rollback, and maximum completion without releasing partial plaintext.
The input remains a future crypto-adapter obligation; this is not raw-packet,
AEAD, source-signature, receiver-replay, target-resource, or radio evidence.

Decision 0004 now fixes the initial routing boundary without claiming an
implementation: zero or one authorized repeater validates source/auth/epoch/
permission, suppresses duplicates, and retransmits exact immutable packet bytes
once. It does not rewrite an end-to-end authenticated TTL. Named-source claims
require source authentication beyond common group-key access. Multi-repeater
routing needs a separate outer construction/new packet version and is deferred.

The algorithm-neutral host policy now implements that ordering: authentication,
authorization, context/epoch, local-role/destination, and immutable permission
are checked before replay observation; eligible exact bytes then face duplicate,
queue, rate, and queue-age limits. Nine groups and the full 27-executable matrix
pass locally and on public `main`. Its verified metadata is a future crypto-
adapter obligation, not implemented proof. Protected replay persistence, target
radio/task binding, reboot/power-loss, and field behavior remain open.

- Public hardware evidence is generated through the fail-closed `OTFL0` boundary;
  raw captures and recovery journals remain local, while committed summaries
  omit transport/hardware identifiers, channels, coordinates, and secrets.
- OpenTrail will not decode raw vehicle CAN/J1939 traffic.
- Hardware-specific code will be isolated behind interfaces.
- LoRa will carry compact state/events/messages, not map packages or high-rate telemetry.
- Forwarding will be controlled and measured before any mesh topology is adopted.
- Protocols and stored configuration will be versioned and defensively decoded.
- Loss of GPS, maps, UI, peers, or OpenGauge must degrade independently.
- Offline-map formats/providers remain replaceable and must permit offline use with correct attribution.
- OpenGauge alerts cross a fixed 64-byte `OGA0` semantic boundary with canonical units and explicit assert/clear lifecycle IDs. CRC detects corruption only; the transport must supply authenticated and authorized producer identity before OpenTrail accepts an alert.
- Critical-alert acknowledgements cross a separate fixed 64-byte `OGK0` boundary with explicit accepted/rejected disposition, canonical rejection reason, original lifecycle identities, consumer boot session/sequence, and observed age. CRC detects corruption only; transport authorization, replay persistence, delivery-controller/outbox correlation, and physical delivery remain required.
- The ACK responder produces `OGK0` only from a final ingress decision: accepted and identical duplicate alerts become accepted/none; authenticated unauthorized/stale/conflict/rate decisions become explicit rejection; malformed/untrusted/identity-mismatched/local-clock-invalid input is silent. Sequence advances only after encoding. A separate two-slot `OTAS` allocator commit-last persists consumer/authorization binding and increments a nonzero boot session before returning it; corruption, identity/epoch change, equal-generation conflict, exhaustion, read failure, and uncertain state fail closed. Ten allocation groups plus affected responder/configuration suites each repeat 100 times. Per-session sequence remains RAM-only; target storage, trusted rollback resistance, authenticated response delivery, and OpenGauge rebind remain.
- Two role-reversed OT-017D bench cycles carried exact 64-byte `OGA0` and responder-produced correlated `OGK0` frames over temporary MeshCore channel text. All correlation checks passed, round trips were 1009.6-1014.0 ms, loss/duplicates/new errors were zero, the SenseCAP recorded exact aggregate +4 flood RX/TX, repeat stayed on, and 4/4 endpoint cleanup checks passed. The host supplied authenticated/authorized context; this is physical byte/composition evidence, not authenticated on-device delivery.
- Two later OT-017E cycles retained the physical and cleanup checks while independently admitting each returned ACK through OpenGauge's real authorization/session/replay/correlation ingress and completing its exact reconstructed outbox entry. Both ended with one acknowledgement and zero queued/in-flight state. OpenGauge state was reconstructed after receipt; persistent live target state remains unproved.
- Two OT-017F role-reversed stale-policy cycles returned exact correlated rejections through the same physical path. OpenGauge processed both but recorded zero acknowledgements, `outbox_completed=false`, no retry release, and explicit terminal failure. Radio loss/duplicates/errors were zero, SenseCAP aggregate was exact +4 flood RX/TX, and cleanup passed 4/4. Retryable rejection and persistent failure/restart/revoke cases remain.
- Two OT-017G role-reversed rate-limit cycles returned exact correlated retryable rejections. OpenGauge processed both with zero acknowledgements/completions, exactly one queued retry, zero in flight, retry release, and no terminal failure. Radio loss/duplicates/errors were zero, SenseCAP aggregate was exact +4 flood RX/TX, and cleanup passed 4/4. Persistent backoff and a later retry-to-accept cycle remain.
- OT-017H added two role-reversed four-leg rejection/retry/accepted sequences. Exact backoff and byte-identical preparation passed; both lifecycles ended with one acknowledgement, one remote retry, zero queued/in-flight/terminal failures, exact aggregate +8 SenseCAP flood RX/TX, and 4/4 cleanup. State was reconstructed after both responses rather than kept live during the physical wait.
- OT-017I repeated both four-leg sequences with one real OpenGauge process already holding the in-flight event before the first send and retaining authorization/replay/outbox state through final completion. Both live lifecycles passed with exact aggregate +8 SenseCAP flood RX/TX and 4/4 cleanup. Restart/power-loss durability and on-device state remain unproved.
- OT-017J records the cross-project restart boundary: OpenGauge's canonical `OOC0` checkpoint is now wired into boot-only atomic live-outbox export/import. Host tests reconstruct queued retry readiness, in-flight ACK timeout, maximum lifetime, exact frames/state/attempts against a new monotonic origin and fail closed on prepared, corrupt, mismatched, nonempty, or unrepresentable state. Durable coordinated storage and physical/on-device restart remain unproved.
- OT-017K records that `OOC0` compatibility is no longer trusted caller metadata: OpenGauge derives a nonzero versioned fingerprint from all four outbox timers, maximum attempts, and emergency reserve. Determinism and sensitivity to every field pass in the full matrix plus 100 focused repeats. Coordinated durable storage remains unproved.
- OT-017L records OpenGauge's canonical `OCR0` coordination boundary: one nonzero generation now contains the exact ACK replay/authorization and outbox checkpoints. Four groups, the full 30-executable matrix, and 100 repeats validate the envelope. Serialized live dual import and recoverable durable storage remain unproved.
- OT-017M records preflighted live `OCR0` coordination: OpenGauge exports both state owners into one generation and validates both exact boot imports on private component copies before committing either live state under exclusive ownership. Exact retry readiness and replay duplicate rejection survive the host restart. Five groups, the full 31-executable matrix, and 100 repeats pass. Recoverable durable storage and physical/on-device restart remain unproved.
- OT-017N records OpenGauge's recoverable two-slot `OCR0` host store: increasing generations, full readback/byte/decode verification, newest-unique selection, prior-good preservation under partial/corrupt writes, degraded-I/O visibility, and equal-generation conflict rejection pass eight groups, the full 32-executable matrix, and 100 repeats. Target NVS, physical power-cut/wear, secure integrity/rollback, and on-device restart remain unproved.
- OT-017O records store-owned recovery generation allocation: empty media starts at 1, saves advance/rotate monotonically, unreadable or conflicted baselines fail closed, and 64-bit exhaustion occurs before export/write. Ten store groups, the full 32-executable matrix, and 100 repeats pass. Factory-reset authority, target NVS, physical power-cut/wear, secure integrity/rollback, and on-device restart remain unproved.
- OT-017P records conservative uncertain-commit handling: all write errors retain intended slot/generation, 16 `OCR0` boundary interruptions preserve the prior newest-good slot, and a full write followed by an I/O error reconciles as committed on boot. Twelve store groups, the full 32-executable matrix, and 100 repeats pass. Target-backend semantics and physical power-cut/wear remain unproved.
- OT-017Q records OpenGauge's canonical `OPA0` peer-authorization restart boundary. The fixed 256-byte record preserves active/revoked logical peers, role permissions, channel, opaque key handles, and authorization epochs, refuses pending approvals, and imports atomically only into a clean boot registry. Eight groups, the full 33-executable matrix, and 100 repeats pass. Coordinated `OPA0`/`OCR0` restore, protected target storage, authenticated integrity, rollback resistance, and physical power-loss evidence remain unproved.
- OT-017R records OpenGauge's recoverable `OPS0` peer-authorization store. The fixed 288-byte envelope and two-slot host store allocate monotonic generations, require exact byte/decode readback, preserve the prior good generation across ten interrupted-write boundaries, reconcile a full write followed by I/O error as committed at boot, expose degraded reads, and fail closed on conflict/exhaustion. Ten groups, the full 34-executable matrix, and 100 repeats pass. Coordinated authorization/replay/outbox restore, protected target storage, authenticated integrity, rollback resistance, and physical power-loss evidence remain unproved.
- OT-017S records OpenGauge's atomic `ORS0` system-recovery boundary. The fixed 1280-byte record binds exact `OPA0` peer authorization and exact `OCR0` ACK/outbox state to one generation. Import restores authorization/outbox only in private candidates and constructs a temporary ACK ingress against them, so epoch, policy, pointer, replay, and retry dependencies preflight before any live owner changes. Six groups, the full 35-executable matrix, and 100 repeats pass. Recoverable `ORS0` storage, target binding, authenticated integrity, rollback resistance, physical power-loss, and on-device boot evidence remain unproved.
- OT-017T records OpenGauge's recoverable two-slot `ORS0` host store. Normal saves own monotonic generations, preserve the newest good slot, require exact readback/decode, recover across eleven interrupted-write boundaries, reconcile a full write followed by I/O error at boot, expose degraded reads, and refuse conflict/exhaustion. Eight groups, the full 36-executable matrix, and 100 repeats pass. ESP-IDF protected storage, authenticated integrity, rollback/reset authority, physical power-loss/wear, and on-device boot evidence remain unproved.
- OT-017U records the external trusted-generation contract: OpenGauge restore rejects a newest valid `ORS0` below the supplied trusted minimum before any live owner import, and save allocation advances beyond both last-trusted and valid local generations. Ten focused groups, the unchanged 36-executable matrix, and 100 repeats pass. A hardware-backed trusted source, authenticated integrity, reset/replacement authority, and physical evidence remain unproved.
- OT-017V records the target recovery-adapter plan: exact two-slot semantics, protected opaque-handle resolution, a separate trusted-generation source, boot/save ordering, coordinated reset/replacement, and physical interruption/endurance evidence are explicit. The connected Heltec/SenseCAP MeshCore radios do not run an OpenGauge target, so no ESP-IDF adapter or on-device durability claim exists.
- OT-017W records protected key-handle preflight: direct and stored OpenGauge `ORS0` restore now validate active logical peers through an injected opaque-handle boundary after private authorization restore but before outbox/ACK preflight or live mutation. Revoked peers are skipped; unavailable, wrong-purpose, and backend failures retain typed peer-specific evidence. Eight system groups, eleven store groups, the unchanged 36-executable matrix, and 100 repeats each pass. No concrete protected-key backend or on-device result exists.
- OT-017X records OpenGauge's typed host boot coordinator. It combines provisioning, trusted-generation, read-only two-slot inspection, protected-key `ORS0` restore, and exact trusted-floor reconciliation into first-boot/restored/degraded/safe-mode/service-required outcomes. Transport stays disabled until the result is operational. Nine focused groups, the complete 37-executable matrix, and 100 repeats pass. No OpenGauge target task, protected backend, or physical boot result exists.
- OT-017Y records OpenGauge's verified save coordinator. Normal persistence requires exact local/trusted generation agreement; the next `ORS0` is verified before trust advances; and exact trust readback is required before transport remains allowed. Missing, rollback, conflict, local-ahead, commit-uncertain, and trust-update failures stay typed and route to service or boot reconciliation. Eight focused groups, the complete 38-executable matrix, and 100 repeats pass. Physical storage/trust durability remains unproved.
- OT-017Z records OpenGauge's unreadable-slot fail-close. Known empty/invalid peer media may restore operationally degraded, but unreadable media could conceal a newer committed generation. Any visible restore remains private, trust does not advance, and transport stays disabled under a service-required result. Ten boot groups, the complete 38-executable matrix, and 100 repeats pass. Physical backend diagnosis remains unproved.
- OT-017AA records OpenGauge's known-degraded repair coordinator. Only current operational degraded evidence with matching active/trusted generation and one valid plus one known empty/invalid slot can write. The next `ORS0`, trust update/readback, and final two-valid-slot inspection must all pass. Healthy, unreadable, service, stale, and uncertain cases fail closed. Five groups, the complete 39-executable matrix, and 100 repeats pass. Physical repair durability and unreadable-media service remain unproved.
- OT-017AB records OpenGauge's redacted recovery-status boundary. Boot, save, and repair results map into one fixed-shape record with operator state/reason/action, slot health, observed/trusted generations, key-failure class, and transport/repair flags. Peer IDs and key handles are structurally absent, and unknown or inconsistent inputs fail closed. Seven groups, the complete 40-executable matrix, and 100 repeats pass locally. Target logging/rendering, persistent audit retention, and physical service workflows remain unproved.
- OT-017AC records OpenGauge's versioned recovery diagnostics adapter. One 32-bit event carries the redacted operation, state/reason/action, slot health, protected-key failure class, and transport/attention/repair/redaction flags. Generations and identity-bearing fields are omitted; magic/version/enums and coherence are validated before a ring write. Eight groups, the complete 41-executable matrix, and 100 repeats pass locally. Target log binding, persistent retention/export, and physical service capture remain unproved.
- OpenTrail has its own GitHub Actions validation on `main` pushes and
  pull requests. The commit-pinned Windows 2025/Python 3.13/UCRT64 job builds
  six verifier/planning/operator CLIs and runs all 58 C++ executables plus the Python
  MeshCore lease, privacy-safe field/pilot, and crypto-benchmark evidence
  suites. The matrix includes position scheduling/privacy control,
  experimental packet/priority admission, loss-aware priority-to-delivery
  handoff, checked-time outbound service coordination, fail-visible outbound
  position safety, checked-time position commands, single-owner position UI,
  privacy-safe position UI diagnostics and strict offline position/recovery
  operator decoding and the unified diagnostic entry point,
  portable-client composition, local-interface, power, time, randomness,
  replay, pilot, and benchmark boundaries. This is host/build evidence, not
  physical MeshCore,
  target firmware/bindings, cryptography, or measured-radio evidence.
- Public OpenGauge GitHub Actions separately validates the shared Windows host matrix on every `main` push and pull request. Its current-main warning-free run passes all 41 executables with zero annotations; OpenTrail links that evidence without conflating the two scopes.
- Project software and documentation are published under Apache-2.0; external contributions follow `CONTRIBUTING.md`, and sensitive reports follow `SECURITY.md`.

## Available hardware and current evidence

| Item | Current status | Required evidence |
| --- | --- | --- |
| Two Heltec V4 LoRa-capable boards | Both units are runtime-identified as **Heltec V4 OLED** and run MeshCore USB Companion `v1.16.0-07a3ca9` with matching USA/Canada settings (910.525 MHz, BW 62.5 kHz, SF7, CR5, 10 dBm). Antennas were user-confirmed attached. Raw-RX evidence established channel match, MAC validation, decryption, queue notification, and application retrieval. A temporary private-channel sample delivered 5/5 numbered messages each direction with 0 loss, 0 duplicates, 233.3-247.2 ms latency, 11.25-12.25 dB SNR, and zero receive/core errors. Packet-v0 delivered 3/3 C++-encoded/decoded frames each direction before and again after the five-hour repeater soak, with valid CRC, no loss/duplicates/errors, empty queues, and verified channel/journal cleanup. OT-017D then delivered 2/2 exact `OGA0` alerts and 2/2 correlated `OGK0` ACKs across role-reversed cycles with zero loss/duplicates/errors and verified cleanup. See `tests/hardware/OT-007A-2026-08-08.md`, `tests/hardware/OT-007-2026-08-08.md`, and `tests/hardware/OT-017D-2026-08-09.md`. `OT-DEV-001` has ROM-level ESP32-S3/2 MB PSRAM/16 MB flash evidence; `OT-DEV-002` does not. | OT-004, OT-006, OT-007, and host-mediated OT-017D use this bench evidence. Authenticated on-device alert binding, usable RSSI, fine-grained airtime, field range/mobility, regulatory constraints, and exact SKU/RF/antenna/pinout/power questions remain. |
| Seeed SenseCAP solar node | Runtime-identified as **Seeed SenseCap Solar**, USB `VID 2886:0059`, running MeshCore Repeater `v1.16.0-07a3ca9` at 910.525 MHz/BW 62.5/SF7/CR5/22 dBm with repeat enabled. Both Heltecs received its advert and remotely read its synchronized clock. A temporary private-channel run produced exactly +2 flood RX/+2 flood TX. Explicit one-hop direct routes then succeeded both ways; with repeat off, the same route failed with +1 direct RX/+0 direct TX and no destination message, proving the repeater was required. A non-secret channel lease passed real stopped-session recovery. The 300-minute alternating close-bench run delivered 300/300 (150 each direction), zero loss/duplicates/errors, 229.8-312.1 ms latency, exact +300 repeater flood RX/TX, repeat preserved, empty queues, and verified exact-name channel/journal cleanup. OT-017D added exact aggregate +4 flood RX/+4 flood TX while two role-reversed alert/ACK cycles passed; repeat remained on and errors stayed zero. See `tests/hardware/OT-009-2026-08-08.md`, `tests/hardware/OT-009A-2026-08-09.md`, and `tests/hardware/OT-017D-2026-08-09.md`. | Exact P1/P1 Pro SKU and internals, battery/GPS/antenna details, solar endurance, physical field behavior/range, and regulatory validation remain. |
| Wio Tracker L1 Pro for MeshCore | Owner reports ordered; not received. Vendor MeshCore page identifies SKU `100030144` and Bluetooth Companion shipping firmware, while the package/revision remains unconfirmed | Follow the non-destructive OT-020 arrival plan; exact label, USB/DFU/BLE, firmware, GNSS current/stale behavior, Heltec interoperability, recovery, power, and privacy-safe evidence |
| Two Waveshare ESP32-S3 1.75-inch round AMOLED touch boards | Owner reports two ordered; not received or tested. The product family is advertised as 466x466 touch with ESP32-S3R8, 8 MB PSRAM, 16 MB flash, and standard/case/GPS variants; exact ordered and received variant remains unconfirmed | Preserve shipping firmware/recovery evidence; confirm exact labels/variant, display/touch/storage interfaces, usable memory, power/thermal behavior, and map/peer/alert rendering under OT-018 |
| Two approximately 7-inch touchscreens | Original test intent; no exact hardware identified | Board/display/controller, interface, resolution, memory/storage needs, availability |

Hardware is not added to a tested-compatible list until repeatable evidence exists.
The [2026-08-10 hardware/regulatory reconciliation](../hardware/HARDWARE_REGULATORY_INVENTORY_2026-08-10.md)
separates exact-unit observations from official family specifications. Its US
field gate remains open: exact labels and FCC grants, installed antenna/gain and
cable loss, frozen firmware/radio settings, and grant coverage for the complete
configuration must be verified. A USA preset or an in-band center frequency is
not treated as proof of authorization.

## Assumptions to validate

- The Heltec boards are legal/configurable for the user's operating region and can form the first two-node test bed.
- ESP32 resources are sufficient for selected map/UI behavior after benchmarking.
- Local Wi-Fi SoftAP may support setup and map transfer without Internet, subject to UX/security/storage testing.
- GPS modules and suitable antennas/power arrangements will be selected separately.
- Alerting is supplemental and cannot guarantee delivery, location accuracy, or emergency response.

## Unresolved decisions

### Product and hardware

- Exact commercial Heltec and Seeed SKUs, regulatory authorization, antenna/RF details, and power-source characteristics
- Reference MCU/radio/display/GPS/storage hardware and minimum supported resource tier
- Portable, vehicle, fixed-relay, and touchscreen power/environmental requirements
- Whether a single ESP32 can meet the chosen large-display map workload

### Protocol and security

- Direct/repeater topology, modulation profiles, airtime budget, final
  position/status cadence, and congestion policy. The position scheduler's
  fixed start/stop/coalescing mechanics and semantic local privacy control are
  host-tested, but policy values and rendered physical behavior remain
  unselected pending measurement
- Identity/name/alias/membership boundaries and the OT-013 invitation/promotion/revoke/rekey/recovery policy are defined and host-tested. Exact Node-ID/alias derivation, production administrator quorum, authenticated join-handshake instantiation, encryption, key storage, rollback protection, persistent recovery, rendered UX, and physical lifecycle evidence remain under partial OT-005 and later gates
- Packet-v0 encoding/budget, position payload, host-only acknowledgement/retry/expiry/duplicate/forwarding/priority policies, the external `OGK0` alert-ACK codec, and OT-014 non-secret configuration persistence are bounded and tested. Generic packet-v0 ACK composition, authenticated routing/priority/ACK transport, measured deployed timing, persistent message/duplicate counter integrity and secure rollback, realistic contention, and final queue/cache limits remain
- Duplicate checkpoints have a canonical fixed 672-byte `OTD0` codec with CRC, strict padding/capacity/version checks, duplicate-key rejection, atomic decode, and remaining-lifetime restoration. Seven codec groups, the full 23-executable matrix, and 100 codec/window repeats pass. Atomic durable storage, wear/privacy policy, authenticated integrity, and rollback protection remain
- The fixed 704-byte `ODS0` store now uses context-bound v1: its formerly reserved bytes carry the exact nonzero group-context ID and epoch, and every active inner key must match. Wrong binding and structurally valid legacy unbound v0 media fail without live mutation or overwrite. Original generation/rotation/readback/degraded/conflict/exhaustion behavior remains. Ten store groups, the full 28-executable matrix, and 100 focused store/coordinator repeats pass locally; the exact matrix passes publicly in run `31374678550`. Protected ESP32 namespace binding, physical atomicity/endurance, authorized migration/reset, authenticated integrity, and trusted rollback protection remain
- The v0 update/recovery architecture requires signed hardware-bound bundles,
  complete inactive-slot readback, persisted bounded trials, independent health
  confirmation, automatic rollback, a trusted firmware floor, and documented
  physical/USB recovery. A pure guard passes eight host groups across candidate,
  write, trial-health, deadline, boot-limit/mismatch, clock, and rollback paths.
  A separate canonical 64-byte `OTU0/v0` checkpoint binds hardware, baseline,
  candidate, exact policy, trial count, rollback reason, and caller-owned
  generation across eight more groups. Restore is atomic and intentionally
  clears boot-local health, time, and session evidence. An abstract two-slot
  store now owns normal generation allocation, preserves prior-good state
  across partial/corrupt writes, verifies readback, repairs a known invalid
  peer, and fails closed on unreadable/conflicted state. A caller-supplied
  trusted-generation contract rejects missing/stale generations before live
  restore and allocates beyond the greater local/trusted value. Read-only
  inspection exposes empty, degraded, unreadable, invalid, and conflicted media
  without mutating the guard or slots. All 20 store groups plus 100 focused
  repeats pass. No target partition table, signer,
  updater adapter, authenticated target storage, hardware-backed trusted
  generation source, or physical interruption/recovery evidence exists. A
  typed host boot coordinator now holds guard state private until the observed
  image is validated, any trial/rollback transition is readback-verified, and
  the trusted generation advances with exact readback. Fifteen groups plus 100
  focused repeats pass. A verified normal-save coordinator now requires exact
  local/trusted agreement, verifies the next checkpoint before advancing trust,
  and verifies exact trust readback before reporting committed. Ten groups plus
  100 repeats pass; uncertain/local-ahead state requires reboot reconciliation.
  A lifecycle-transition coordinator now applies health, tick, confirmation,
  and rollback to a private guard copy, publishes reboot-relevant state only
  after that verified save commits, and stops the original live guard on any
  persistence failure. Ten groups plus 100 repeats pass. A fixed redacted
  operator-status boundary validates boot, save, and transition coherence before
  emitting only coarse state/reason/action, generation evidence, and recovery
  flags. Hardware/candidate identity, checkpoint payloads, raw adapter errors,
  and nested results are absent; unknown or contradictory input blocks normal
  operation as service-required. Eight groups plus 100 repeats pass in the
  complete 58-executable matrix. A versioned `OTRD0` adapter now records one
  coherent status through the existing logger as one fixed hexadecimal 32-bit
  word. Generations and identity-bearing detail are omitted; magic, version,
  reserved bits, enums, flags, and state/action/reason coherence fail closed.
  Eight groups plus 100 repeats pass. A bounded production-facing RAM ring now
  retains the newest 32 canonical records, assigns boot-local sequences,
  snapshots oldest-first without partial output, counts rollover/rejection,
  and captures real `OTRD0` events across eight groups plus 100 repeats. A
  separate host-only CLI accepts exactly one canonical uppercase recovery
  record, reruns all v0 word/coherence checks, and emits stable coarse names.
  It reads no file, device, log, or network and has no recovery execution
  authority. Target
  task binding/concurrency, retained audit/export, rendering, scheduling and
  reboot execution, target boot tasks, terminal
  cleanup/reset authority, protected backends, and physical restart evidence
  remain absent.
  A semantic presentation adapter now maps valid `OTRD0` outcomes into the
  existing checked status/system-fault frame. Invalid words fail visibly to a
  generic critical service frame; only nonblocking notices expose
  acknowledgement, which has no confirm/cleanup/reboot authority. Nine groups
  plus 100 repeats pass. Exact renderer, target scheduling/revision ownership,
  and physical operator workflow remain absent.
  A separate position-sharing adapter now presents stopped, active, waiting,
  deferred, and terminal-failure scheduler states through that same checked UI
  boundary. Start only arms the scheduler, stop is immediate, stale frame input
  is rejected, and unrelated actions cannot mutate position sharing. Ten groups
  plus 100 repeats pass. Renderer wording/layout, exact target synchronization,
  direct radio/GPS composition, and physical privacy behavior remain absent.
  An experimental packet-admission sink now revalidates the scheduler's
  canonical current payload, obtains injected ephemeral packet-v0 metadata,
  encodes one exact 38-byte frame, and admits it only as background traffic
  using the actual scheduler attempt time. Ten groups plus 100 repeats cover
  round-trip, expiry, priority, pressure, and failure behavior. Packet v0 is
  unauthenticated and prohibited for real coordinates; identity/counter
  lifecycle, authenticated composition, delivery/radio binding, and physical
  behavior remain absent.
  A single-owner priority-to-delivery handoff now peeks before admission and
  commits only after the delivery controller accepts. Full/rejected delivery
  admission retains the queue entry, and remaining queue lifetime bounds the
  delivery expiry. Ten groups plus 100 repeats cover strict priority, pressure,
  rejection, exact expiry, rollback-safe time handling, and the complete
  position-packet path through a fake radio. Authentication, target concurrency,
  direct-radio binding, and physical delivery remain absent.
  A checked-time outbound service coordinator now reads the guarded monotonic
  clock once and orders optional active-sharing location, scheduling, handoff,
  delivery, and radio service. It performs no GPS or downstream work when time
  is unavailable, and permanent clock faults stop sharing and latch service
  closed. Ten groups plus 100 repeats pass. Target task/concurrency, inbound
  processing, concrete adapters, and physical behavior remain absent.
  The same coordinator now owns target-facing position Start/Stop. Start obtains
  one fresh checked sample internally; not-ready defers without scheduler
  access; rollback/source failure latches closed; Stop is immediate without a
  clock read. Ten groups plus 100 repeats pass. No target task/clock binding,
  rendered retry behavior, reboot policy, or physical input is claimed.
  A cooperative position-sharing UI coordinator now owns revision allocation,
  initial/current presentation, one checked input poll, live Start/Stop, and the
  required post-action refresh. Temporary clock deferral retains the truthful
  Start frame. Post-action display failure and revision exhaustion stop sharing
  and latch input closed. Ten groups plus 100 repeats pass. Exact ESP-IDF
  task/lock serialization, rendering, and physical behavior remain absent.
  The same owner now compares live user-visible position semantics before
  polling input. GPS wait/recovery, sink deferral, and permanent clock faults
  publish a higher revision first; nonvisible counters/timestamps do not.
  Failed observation refresh stops sharing and latches input closed. Ten groups
  plus 100 repeats pass. Exact target atomicity and physical timing remain
  absent.
  A separate `OTPD0/v0` adapter records validated position UI outcomes as one
  fixed 32-bit public event while omitting revisions, timestamps, counters,
  location, content, and identity. Idle polls are suppressed. Ten groups plus
  100 repeats pass; target retention/export and physical service evidence are
  absent.
  A runtime-aware position overlay now validates the coordinator status before
  presentation or Start/Stop application. Latched rollback/source failure and
  incoherent state produce a no-action critical frame; stale healthy Start is
  rejected, while Stop remains safe. Ten groups plus 100 repeats pass. Exact
  renderer/input, reboot, and physical behavior remain absent.

### Maps and interface

- The v0 offline-map gate fixes legal/source metadata, off-device preparation,
  immutable staged activation, prior-good recovery, and mapless fallback. The
  provider/data, package/container, renderer, storage medium, transfer method,
  signature policy, limits, and exact target remain open pending OT-016/OT-018
  experiments
- Touchscreen UI framework and distracted-driving/safe-use constraints
- OpenGauge authenticated on-device transport, peer/key lifecycle, persistent replay/outbox state, failure UX, and direct radio integration; the v0 semantic schema/policy is host-tested and OT-017D/OT-017E supply bounded physical byte and host-component completion evidence
- Field-session repetition count, movement/terrain profiles, acceptance
  thresholds, and the final position/status cadence after measured contention

### Governance

- Final public product naming and professional clearance
- Legal applicant/payee, funding bookkeeping, award authority, and application-
  specific eligibility/terms
- Public privacy policy, location-data retention/deletion, authentication,
  backup, and incident-response rules
- Code of conduct, CI, release/signing process, and supported-hardware evidence policy; Apache-2.0 licensing, contribution guidance, and security reporting are established

## Next decision checkpoint

The hardware-abstraction set and host-tested portable-client composition now
cover all ten enumerated target-facing endpoints. The whole-contract review is
complete and records the separate 64-byte and 704-byte storage obligations.
OT-003F now supplies the first checked-time outbound runtime composition, but
OT-003 remains partial because no exact ESP-IDF radio/GPS/log/storage/
entropy/time/power/display/input adapters, target application build, or
on-device composition evidence exists. Exact adapters, thresholds, and rendered
behavior wait for frozen client hardware. OT-023 remains blocked at the exact
four-unit client and firmware freeze. Use the prepared OT-020 procedure when the
Wio Tracker arrives.
Authenticated on-device transport, protected target state, physical restart and
power-failure injection, GPS evidence, field performance, and direct-radio
airtime remain explicit later gates.
