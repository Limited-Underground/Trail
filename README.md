# OpenTrail

[![Host validation](https://github.com/nbjelanovic/OpenTrail/actions/workflows/host-validation.yml/badge.svg)](https://github.com/nbjelanovic/OpenTrail/actions/workflows/host-validation.yml)

OpenTrail is a proposed free/open-source, ESP32-based off-road communication, location-awareness, and safety platform designed to keep a group useful when cellular service is unavailable.

## Current snapshot — 2026-08-11

### Funding and future web path

- **Public name remains open:** OpenTrail is the engineering/repository name;
  funding material uses a replaceable `[PUBLIC PROJECT NAME]` field. ECLU is not
  assumed in protocols, storage, device identity, URLs, or hardware.
- **Funding paperwork is ready for tailoring:** the brand-neutral
  [funding packet](docs/funding/README.md) includes a fact sheet, application
  answer bank, hardware-support letter, preliminary budget, submission
  checklist, and dated opportunity register. No application has been submitted
  and the legal applicant/payee remains to be confirmed.
- **All funding outreach is paused:** no cash grant, donation account, payment
  account, hardware request, sponsorship, service-credit request, submission,
  acceptance, or shipment is authorized. The packet remains available only as
  preparation material until the owner explicitly clears the hold.
- **Web operations are private:** hosting accounts, provider choices, DNS and
  email configuration, deployment state, costs, recovery procedures, and
  future infrastructure plans are intentionally maintained outside this public
  repository. Base radio operation remains independent of any web service.

### Hardware and integration

- **Phase:** architecture plus deterministic host components and bounded bench
  proofs; no production firmware or supported-hardware declaration yet.
- **Connected bench setup:** two Heltec V4 OLED USB Companion nodes and one Seeed
  SenseCAP Solar repeater, all on matching USA/Canada radio settings.
- **Hardware/regulatory status:** official family specifications narrow the
  likely hardware matches, but exact labels, FCC grants, installed antennas,
  and approved operating modes remain open. The
  [dated inventory and fail-closed field gate](hardware/HARDWARE_REGULATORY_INVENTORY_2026-08-10.md)
  keeps bench evidence separate from authorization claims.
- **Latest radio result:** the three-node path completed a 300/300 five-hour soak
  and later role-reversed alert/ACK lifecycles with zero observed loss,
  duplicates, or new radio errors and verified cleanup.
- **Latest bounded burst:** 30/30 alternating two-second deliveries passed with
  zero loss/duplicates/new errors, exact +30 repeater flood RX/TX, and verified
  cleanup. The [dated interpretation](tests/hardware/OT-022-2026-08-10.md) and
  [privacy-safe JSON](tests/hardware/OT-022-2026-08-10.json) are public; the raw
  capture remains local.

### Field-test scale

- **Planned progression:** four clients without a repeater, then four plus one
  repeater, then eight plus one repeater. The base client remains independently
  useful; the repeater is optional.
- **First pilot is defined but not hardware-ready:** the dated
  [four-person standalone plan](docs/testing/FOUR_PERSON_PILOT_V0.md) specifies
  four identical self-contained units, no phone/server/repeater/laptop during
  the session, three one-hour environment classes, 300 message origins, 900
  peer-delivery opportunities, privacy-safe logging, and provisional pass/fail
  gates. It remains blocked until one exact client model and firmware satisfy
  the battery/enclosure/GNSS/display/input/USB-recovery freeze.
- **Pilot verdicts are deterministic:** the strict
  [`OTPR0/v0` evaluator](docs/testing/FOUR_PERSON_PILOT_RESULT_V0.md)
  distinguishes pass, measured failure, ineligible setup, and invalid/private
  evidence. It cannot turn the currently blocked hardware plan into a pass.
- **New planning evidence:** a deterministic [group-load model](docs/testing/GROUP_LOAD_MODEL_V0.md)
  separates source and forwarding airtime. Under one fixed comparison profile,
  theoretical scheduled demand is 1.2727%, 2.5455%, and 5.0911% respectively.
  These are not delivery, range, collision, or regulatory results.

### Software and safety

- **Position sharing now has an explicit start/stop scheduler:** the
  [bounded scheduler](docs/protocol/POSITION_BROADCAST_SCHEDULER_V0.md) submits
  only current validated fixes as the existing 16-byte payload. It starts only
  by command, stops immediately, coalesces delayed service instead of building
  a stale catch-up queue, and retries missing fixes or sink pressure at a
  separate injected interval. Ten groups plus 100 repeats pass. No cadence,
  authenticated packet, radio binding, physical GPS behavior, or regulatory
  result is selected or claimed.
- **Position sharing has a local privacy-control contract:** the
  [semantic control adapter](docs/platform/POSITION_SHARING_CONTROL_V0.md)
  presents stopped, active, waiting-for-fix, deferred, and failed states through
  the checked local-interface boundary. Start only arms the scheduler; stop is
  immediate; stale screen input cannot change the state; and terminal scheduler
  faults expose no misleading execution action. Ten groups plus 100 focused
  repeats pass on both button and touch capability shapes. Exact wording,
  renderer, target task/revision ownership, and physical behavior remain open.
- **Cryptography remains gated, not claimed:** the dated
  [candidate review](docs/security/CRYPTO_CANDIDATE_REVIEW_2026-08-10.md)
  makes Espressif's libsodium component the first target benchmark, with pinned
  ESP-IDF mbedTLS/PSA and Monocypher comparisons. Noise XK is only a leading
  invitation prototype when a signed invitation pins the inviter key. Packet v0
  remains unauthenticated, and no irreversible security setting is authorized
  on the current bench radios.
- **Benchmark evidence cannot skip the hard parts:** the host-tested
  [`OTCB0/v0` boundary](docs/security/CRYPTO_BENCHMARK_EVIDENCE_V0.md) fixes the
  exact target/toolchain/dependency/radio fields, 100 cold plus 100 warm runs,
  eight operation timing sets, resources, hashes, and eight security/lifecycle
  gates. Its public plan remains blocked until the real target is frozen; no
  crypto library has been selected or benchmarked on-device.
- **Updates must fail back to a recoverable client:** the
  [v0 update/recovery architecture](docs/update/UPDATE_RECOVERY_ARCHITECTURE_V0.md)
  requires signed hardware-bound bundles, verified inactive-slot writes,
  bounded health-confirmed trials, automatic rollback, trusted version floors,
  and physical/USB recovery. Its pure lifecycle guard passes eight host groups.
  A canonical [64-byte reboot checkpoint](docs/update/UPDATE_STATE_CHECKPOINT_V0.md)
  adds exact policy binding, generation, corruption detection, and atomic
  trial/rollback restoration across eight more groups while forcing every new
  boot to re-prove health. Its [two-slot host store](docs/update/UPDATE_CHECKPOINT_STORE_V0.md)
  owns generation allocation, preserves prior-good state across partial/corrupt
  writes, verifies readback, repairs known degradation, and fails closed on an
  unreadable or conflicted slot. Its explicit [trusted-generation
  contract](docs/update/UPDATE_TRUSTED_GENERATION_FLOOR_V0.md) rejects missing
  or stale checkpoints before live restore and saves beyond the greater local/
  trusted generation. All 20 store groups plus 100 focused repeats pass. No
  target updater, authenticated store, hardware-backed trusted source, or
  physical interruption/recovery result exists. A new [typed boot
  coordinator](docs/update/UPDATE_RECOVERY_BOOT_COORDINATOR_V0.md) works on a
  private guard, persists trial/rollback transitions, advances and exactly
  reads back trust, and releases live application state only after the complete
  sequence succeeds. Fifteen groups plus 100 focused repeats pass; this remains
  host ordering evidence, not an on-device boot result. A [verified save
  coordinator](docs/update/UPDATE_RECOVERY_SAVE_COORDINATOR_V0.md) requires
  exact local/trusted agreement, verifies checkpoint-before-trust ordering, and
  returns a typed reboot-reconciliation result for uncertain or local-ahead
  state. Ten groups plus 100 repeats pass. A [lifecycle-transition
  coordinator](docs/update/UPDATE_RECOVERY_TRANSITION_COORDINATOR_V0.md) applies
  health, time, confirmation, and rollback operations to a private guard copy;
  durable state becomes live only after the verified save succeeds, while any
  persistence failure stops the original guard. Ten groups plus 100 repeats
  pass. A fixed [redacted recovery status](docs/update/UPDATE_RECOVERY_STATUS_V0.md)
  then converts boot, save, and transition results into one coarse operator
  state/reason/action record. It structurally omits hardware/candidate identity,
  raw checkpoints, adapter errors, and nested results; inconsistent input fails
  closed to blocked service. Eight groups plus 100 repeats pass. Target
  rendering, scheduling/reboot, and physical persistence evidence remain. A
  versioned [`OTRD0` diagnostic adapter](docs/diagnostics/UPDATE_RECOVERY_DIAGNOSTIC_EVENT_V0.md)
  records that status through the existing logger as one fixed 32-bit word and
  deliberately omits generations. Magic, version, reserved bits, enums, flags,
  and state/action coherence fail closed; filtering and sink rejection stay
  distinct. A production-facing [bounded RAM ring](docs/diagnostics/RING_LOG_SINK_V0.md)
  now retains the newest 32 canonical records, snapshots them oldest-first,
  counts rollover/rejection, preserves boot-local ordering across clears, and
  captures real `OTRD0` events. Both focused suites pass 100/100 repeats. A
  [semantic recovery presentation](docs/update/UPDATE_RECOVERY_PRESENTATION_V0.md)
  now maps valid outcomes to the existing status/system-fault UI contract;
  corrupt diagnostics fail visibly to a generic service frame. Acknowledgement
  is offered only for nonblocking notices and never confirms, cleans up, or
  reboots. Its nine groups plus 100 repeats pass. Exact target composition,
  renderer, concurrency, persistent audit/export, and physical service capture
  remain.
- **Security overhead is now visible before wire freeze:** a bounded
  [protected-packet budget](docs/protocol/PROTECTED_PACKET_BUDGET_V0.md) charges
  every candidate frame a corrected 44-byte authenticated header and 16-byte
  tag; a signed-group candidate adds 64 bytes, leaving 39 plaintext bytes at a
  163-byte example MTU. Its 16-byte position costs 140 bytes/461.312 ms at the
  bench PHY. Ten groups pass locally and on public `main`; this is sizing
  evidence—not packet v1, cryptography, or a radio measurement.
- **The current 64-byte alert/ACK does not fit one signed candidate frame:** the
  [feasibility result](docs/protocol/CRITICAL_ALERT_PROTECTED_BUDGET_V0.md)
  requires two fragments, 312 transmitted bytes, and 1.025024 seconds of
  theoretical source airtime for each alert or ACK at the example MTU/PHY.
  Fragmentation remains blocked pending authentication, replay, reassembly,
  ACK/retry, resource, target, and regulatory evidence.
- **Verified fragments now have a bounded assembly gate:** a
  [fixed-memory reassembler](docs/protocol/PROTECTED_REASSEMBLY_V0.md) accepts
  only future crypto-adapter output, supports four concurrent messages and up
  to 16x103 bytes, handles reorder/exact duplicates, and drops conflicts or
  exact-timeout sessions. Ten groups pass; raw packet parsing, cryptography,
  packet-v1, target memory, and radio behavior remain absent.
- **First-release forwarding stays bounded:** [Decision 0004](docs/decisions/0004-immutable-first-release-forwarding.md)
  permits zero or one authorized repeater, which validates and rebroadcasts the
  exact immutable protected bytes once. It does not rewrite a TTL. Named sender
  claims require source authentication beyond common group-key possession;
  multi-repeater routing remains a separate future protocol decision. The
  [host policy](docs/protocol/SINGLE_REPEATER_FORWARDING_V0.md) proves exact-byte
  queueing, fail-before-replay authentication/authorization checks, one-
  repeater configuration, congestion bounds, and expiry across nine local
  and public-main groups; cryptography and target forwarding remain absent.
- **Repeater replay now has a reboot-safe host boundary:** the
  [save-before-transmit coordinator](docs/protocol/SINGLE_REPEATER_REPLAY_COORDINATOR_V0.md)
  restores or repairs the two-slot duplicate checkpoint before operation and
  durably records each newly observed eligible key before releasing its queued
  exact frame. Nine restart/failure groups and the full 28-executable matrix
  pass locally and on public `main` in run `31372816356`. A failed or uncertain
  save disables forwarding. This deliberately prevents reboot amplification at
  the cost of possibly losing a queued frame after save and before transmit;
  protected target storage, power-cut/wear, and a durable frame outbox remain
  unproved.
- **Replay checkpoints are now group-bound on disk:** fixed-size
  [`ODS0/v1`](docs/persistence/DUPLICATE_CHECKPOINT_STORE_V1.md) records embed
  the nonzero group-context ID and epoch and require every inner replay key to
  match. Wrong-group/epoch and legacy unbound v0 media fail closed without
  restore or overwrite; the coordinator reports service required instead of
  guessing ownership. Ten store groups and the unchanged 28-executable matrix
  pass locally and on public `main` in run `31374678550`. Protected target
  storage, authenticated integrity, rollback, authorized reset/migration,
  power-cut, and endurance evidence remain open.
- **Nonce-reuse prerequisite now has host evidence:** a fixed-memory
  [outbound counter lease store](docs/security/OUTBOUND_COUNTER_LEASE_V0.md)
  commits a nonoverlapping 64-bit counter range before returning any counter,
  isolates it in its own two-slot domain, and wastes unused counters after a
  restart rather than reusing them. Ten interruption/domain/exhaustion groups
  pass locally and on public `main`; this is not packet-v1, AEAD, or
  target-storage evidence.
- **Nonce packing now rejects lease/key cross-wiring:** the
  [host-tested 96-bit composition boundary](docs/security/AEAD_NONCE_COMPOSITION_V0.md)
  requires matching full 128-bit lease/key domains and a nonzero counter before
  packing the adapter-supplied four-byte prefix plus big-endian counter. Seven
  groups pass; this is not KDF, AEAD, packet-v1, or target evidence.
- **Future KDF input is canonical and purpose-separated:** the
  [`OTKD/v1` context](docs/security/TRAFFIC_KEY_CONTEXT_V0.md) binds group,
  epoch, full sender fingerprint, and distinct AEAD-key/nonce-prefix/counter-
  domain purposes in 52 bytes. Eight groups pass; no secret or KDF output is
  handled, and exact-target cryptographic selection remains open.
- **Secure randomness now fails closed at an explicit boundary:** the
  [v0 source contract](docs/security/SECURE_RANDOM_SOURCE_V0.md) exposes typed
  not-ready, ready, and failed states; bounds each request to 1-64 bytes; and
  requires complete output or no buffer change. A deterministic scripted fake
  remains under test support only. Eight readiness, failure, retry, exhaustion,
  and request-boundary groups plus 100 focused repeats pass locally. No ESP-IDF
  entropy adapter, strong-DRBG choice, production key generation, or physical
  cold-start/brownout/RF-concurrency evidence is claimed.
- **Monotonic time now has one checked target boundary:** the
  [v0 clock contract](docs/platform/MONOTONIC_CLOCK_V0.md) separates boot-local
  elapsed milliseconds from UTC, accepts equal ticks, preserves continuity
  across temporary not-ready reads, and latches rollback or source failure
  closed for the current boot composition. Eight groups plus 100 focused
  repeats pass in the complete local matrix. No ESP-IDF timer adapter,
  target-task composition, deep-sleep/brownout behavior, or physical timing
  evidence is claimed.
- **Power state is explicit instead of guessed:** the
  [v0 power contract](docs/platform/POWER_STATE_V0.md) keeps source readiness,
  external power, battery presence, charge state, optional percentage/voltage,
  and sample age separate. Injected policy classifies normal, low, critical,
  external-only, indeterminate, fault, stale, and invalid states without
  estimating percentage from voltage. Eleven groups plus 100 focused repeats
  pass in the complete 31-executable matrix. No ESP-IDF/board adapter, charger
  control, hardware threshold, shutdown behavior, battery-life result, or
  physical power evidence is claimed.
- **Small OLED/buttons and touch displays share one semantic boundary:** the
  [v0 local-interface contract](docs/platform/LOCAL_INTERFACE_V0.md) presents
  fixed status/action frames rather than pixels and resolves normalized input
  only against the exact successfully displayed revision. A critical-alert
  confirmation requires its dedicated screen and a hold gesture; stale touches,
  invalid slots, display failures, and inappropriate gestures fail visibly.
  Twelve groups plus 100 focused repeats pass in the complete 33-executable
  matrix. No renderer, target adapter, physical readability/input, critical-
  alert delivery, display performance, or supported hardware is claimed.
- **Incomplete portable targets now fail structural review:** the
  [portable-client composition preflight](docs/platform/PORTABLE_CLIENT_COMPOSITION_V0.md)
  binds radio, GPS, diagnostics, both 64-byte protocol/configuration storage
  and separate 704-byte replay storage, entropy, monotonic time, power,
  display, and local input. It aggregates missing or incoherent bindings while
  querying only advertised radio MTU; it performs no mutable adapter I/O.
  Eight groups pass in the complete 33-executable matrix. This is a target
  shape and whole-contract review—not an ESP-IDF application, concrete board
  adapter, target build, physical UI, or supported-hardware result.
- **Latest shared software result:** OpenGauge now records its redacted recovery
  status as one versioned 32-bit diagnostics event. Coarse outcome, slot health,
  protected-key error category, and flags round-trip without generations, peer
  IDs, key handles, addresses, credentials, or raw checkpoint data. Invalid
  combinations fail closed. Its complete 41-executable matrix and 100 focused
  diagnostics repeats pass locally.

### Validation and operations

- **OpenTrail validation:** [GitHub Actions](https://github.com/nbjelanovic/OpenTrail/actions/workflows/host-validation.yml)
  builds three verifier/planning CLIs and runs all 48 C++ test executables plus
  the Python MeshCore lease, privacy-safe field-log/pilot, and crypto-benchmark
  suites on every `main` push and pull request. The current-main matrix,
  including position scheduling/privacy control, portable-client composition,
  local-interface, power-state, clock, randomness,
  pilot-result/template,
  crypto-benchmark, protected-packet-budget, and immutable-repeater suites,
  passes on `main`.
- **Shared integration validation:** the OpenGauge host matrix runs in
  [GitHub Actions](https://github.com/nbjelanovic/OpenGauge/actions/workflows/host-validation.yml)
  on every `main` push and pull request. Its current-main warning-free run
  passes all 41 executables with zero annotations.

### Hardware on order and remaining gates

- **Next hardware:** a Wio Tracker L1 Pro is reported ordered but has not arrived
  or been tested. Two round AMOLED candidates are also reported ordered for the
  separate OpenGauge display evaluation.
- **Still unproved:** authenticated on-device OpenTrail transport, protected key
  storage, direct SX1262 binding, GPS hardware, field range, production UI, and
  regulatory acceptance.

Progress is organized by date in the [public progress log](docs/PROGRESS_LOG.md).

## Project status

Architecture and bounded proof-of-concept phase. Two Heltec V4 OLED companions and a Seeed SenseCAP Solar repeater have close-range transport, packet-v0, flood-relay, software-forced one-hop, and a 300/300 five-hour bench-soak result with verified cleanup. Two strengthened role-reversed physical alert/ACK cycles also delivered 2/2 exact `OGA0` alerts and 2/2 correlated `OGK0` responses with zero loss, duplicates, or errors and exact +4 SenseCAP flood RX/TX. Each returned ACK then passed the real OpenGauge authorization/replay/correlation ingress and completed its exact reconstructed outbox entry. The OpenGauge critical-alert v0 semantic codec, OpenTrail trust/freshness/duplicate/rate ingress policy, canonical duplicate-checkpoint codec and two-slot host store, mirrored `OGK0` acknowledgement codec, final-ingress-to-ACK responder, and commit-last responder boot-session allocator have deterministic host evidence. A Wio Tracker L1 Pro for MeshCore is reported ordered and has a non-destructive arrival plan, but no evidence yet. These are bounded bench, host, and planning results—not a production/security protocol, authenticated physical alert link, direct-radio binding, field-range result, or supported-hardware declaration. No production firmware, selected display, or tested-compatible hardware list exists yet.

## Latest verified checkpoint — 2026-08-09

- **Three connected radios used:** two Heltec V4 OLED USB companions plus one Seeed SenseCAP Solar repeater.
- **Physical alert/ACK:** 2/2 exact alerts and 2/2 exact correlated acknowledgements in role-reversed cycles; 0 loss, 0 duplicates, 0 new radio errors, and 1005.3–1014.2 ms host-observed round trips.
- **Repeater participation:** exact aggregate +4 flood RX and +4 flood TX; repeat remained enabled.
- **Application completion:** both returned acknowledgements passed OpenGauge peer authorization, session binding, replay/correlation checks, and completed a reconstructed in-flight outbox entry.
- **Negative delivery policy:** two additional role-reversed stale-policy cycles returned exact correlated rejections; both produced zero delivery acknowledgements, `outbox_completed=false`, and an explicit OpenGauge terminal failure rather than false success.
- **Retryable delivery policy:** two role-reversed rate-limit cycles also produced zero acknowledgements/completions, released exactly one queued retry, and avoided terminal failure.
- **Retry-to-accept:** two four-leg role-reversed sequences then enforced the exact backoff, retransmitted byte-identical alerts, and completed only after a second physical accepted ACK; final state was one acknowledgement and zero queued/in-flight.
- **Live lifecycle:** the latest two sequences started one real OpenGauge host process before the first alert and kept its authorization, replay, and outbox state alive through all four physical legs and final completion.
- **Restart recovery:** OpenGauge's [canonical 640-byte outbox checkpoint](https://github.com/nbjelanovic/OpenGauge/blob/main/docs/integration/CRITICAL_ALERT_OUTBOX_CHECKPOINT_V0.md) is now wired into the live outbox. Boot-only atomic import/export preserves exact frames, attempts, queued retry readiness, in-flight ACK timeout, and maximum lifetime across a new monotonic-clock session. Compatibility is derived from every timer, attempt limit, and emergency reserve rather than caller input; prepared sends, corruption, and policy mismatch fail closed. Durable coordinated storage is the next gate.
- **Coordinated generation:** OpenGauge now has host-tested [live `OCR0` export/import](https://github.com/nbjelanovic/OpenGauge/blob/main/docs/integration/CRITICAL_ALERT_RECOVERY_CHECKPOINT_V0.md). Both exact boot imports preflight on private copies before live state changes, restoring retry readiness and ACK replay state from one generation. Recoverable two-slot storage remains the next gate.
- **Recoverable storage:** OpenGauge now has a host-tested [two-slot `OCR0` store](https://github.com/nbjelanovic/OpenGauge/blob/main/docs/integration/CRITICAL_ALERT_RECOVERY_STORE_V0.md) with full readback verification, newest-unique-generation recovery, prior-good preservation after partial/corrupt writes, and visible degraded I/O. Target NVS and physical power-cut/wear evidence remain.
- **Generation safety:** the recovery store now allocates generations itself—1 on empty media, then monotonic slot rotation—with conflict/I/O refusal and pre-write exhaustion handling. Callers no longer supply normal save generations.
- **Interrupted-write recovery:** write failures are conservatively marked commit-uncertain. Sixteen `OCR0` overwrite-boundary interruptions preserved the prior good generation, and a fully written record followed by an I/O error was reconciled correctly at boot.
- **Peer authorization restart:** OpenGauge now has a canonical 256-byte [`OPA0` checkpoint](https://github.com/nbjelanovic/OpenGauge/blob/main/docs/security/PEER_AUTHORIZATION_CHECKPOINT_V0.md) for active/revoked logical peers, role permissions, channel, opaque key handles, and authorization epochs. Pending approvals are never persisted and boot import is atomic. The full 33-executable matrix and 100 focused repeats pass; coordinated `OPA0`/`OCR0` target recovery remains.
- **Recoverable peer storage:** OpenGauge now wraps `OPA0` in a 288-byte [`OPS0` two-slot host store](https://github.com/nbjelanovic/OpenGauge/blob/main/docs/security/PEER_AUTHORIZATION_CHECKPOINT_STORE_V0.md). It allocates generations, verifies exact readback, preserves the prior good generation across ten interrupted-write boundaries, and reconciles a full write followed by an I/O error at boot. The full 34-executable matrix and 100 focused repeats pass; protected ESP32 storage and cross-component atomic restore remain.
- **Atomic system recovery:** OpenGauge's 1280-byte [`ORS0` checkpoint](https://github.com/nbjelanovic/OpenGauge/blob/main/docs/integration/CRITICAL_ALERT_SYSTEM_RECOVERY_V0.md) binds exact `OPA0` authorization to exact `OCR0` ACK/outbox state in one generation. It preflights a temporary ACK ingress against private restored registry and outbox candidates before any live owner changes. The full 35-executable matrix and 100 focused repeats pass; recoverable `ORS0` storage and target boot evidence remain.
- **Recoverable system store:** exact `ORS0` generations now have a [two-slot host store](https://github.com/nbjelanovic/OpenGauge/blob/main/docs/integration/CRITICAL_ALERT_SYSTEM_RECOVERY_STORE_V0.md) with store-owned generations, exact readback/decode, prior-good preservation across eleven interrupted-write boundaries, visible degradation, conflict/exhaustion refusal, and full-write error boot reconciliation. The full 36-executable matrix and 100 focused repeats pass; target durability remains unproved.
- **Rollback boundary:** the same store now rejects a selected valid `ORS0` below a caller-supplied trusted minimum before importing authorization, ACK, or outbox state. New saves can advance beyond both the trusted value and local slots. Ten focused groups, the unchanged 36-executable matrix, and 100 repeats pass; a hardware-backed trust source remains a target-design gate.
- **Protected-key preflight:** direct and stored `ORS0` restore can now validate each active logical peer's opaque key handle before any live import. Revoked peers are skipped; unavailable, wrong-purpose, and backend failures are typed and peer-specific. Eight system and eleven store groups, the unchanged 36-executable matrix, and 100 repeats each pass; no concrete key store is claimed.
- **Typed boot recovery:** OpenGauge's [boot coordinator](https://github.com/nbjelanovic/OpenGauge/blob/main/docs/integration/CRITICAL_ALERT_SYSTEM_RECOVERY_BOOT_V0.md) requires independently empty trust/provisioning and two empty slots for first boot, maps rollback/conflict to safe mode and missing key/storage/trust to service, preserves visible degraded recovery, and reads back trusted-floor catch-up before transport enablement. Ten groups, the full 38-executable matrix, and 100 repeats pass; no target boot task is claimed.
- **Unreadable-slot fail-close:** known empty or checksum-invalid peer media may remain operationally degraded, but an unreadable peer slot can conceal a newer committed generation. OpenGauge now retains any visible restore only privately, requires service, avoids trust advancement, and keeps transport disabled. Ten boot groups, the full 38-executable matrix, and 100 repeats pass.
- **Known-degraded repair:** OpenGauge's [repair coordinator](https://github.com/nbjelanovic/OpenGauge/blob/main/docs/integration/CRITICAL_ALERT_SYSTEM_RECOVERY_REPAIR_V0.md) accepts only exact operational degraded evidence for one valid plus one known empty/invalid slot, commits the next generation, advances/readbacks trust, and verifies both slots valid. Healthy, unreadable, service, stale, and uncertain cases cannot report repaired. Five groups, the full 39-executable matrix, and 100 repeats pass.
- **Redacted operator status:** OpenGauge's [status boundary](https://github.com/nbjelanovic/OpenGauge/blob/main/docs/integration/CRITICAL_ALERT_SYSTEM_RECOVERY_STATUS_V0.md) maps boot, save, and repair outcomes to fixed state/reason/action, slot-health, generation, and transport/repair fields while omitting peer IDs, key handles, addresses, credentials, and raw checkpoint data. Unknown or incoherent input fails closed. Seven groups, the full 40-executable matrix, and 100 repeats pass locally; no target log/display or physical service workflow is claimed.
- **Versioned diagnostic event:** OpenGauge's [diagnostics adapter](https://github.com/nbjelanovic/OpenGauge/blob/main/docs/diagnostics/RECOVERY_STATUS_DIAGNOSTIC_EVENT_V0.md) writes one atomic 32-bit event containing only the redacted coarse outcome, slot health, key-failure class, and flags. Magic/version/coherence checks fail closed, and generations plus identity-bearing fields are omitted. Eight groups, the full 41-executable matrix, and 100 repeats pass locally; no target log backend or persistent audit export is claimed.
- **Verified save ordering:** OpenGauge's [save coordinator](https://github.com/nbjelanovic/OpenGauge/blob/main/docs/integration/CRITICAL_ALERT_SYSTEM_RECOVERY_SAVE_V0.md) refuses missing local state, local-behind rollback, and local-ahead unreconciled trust before normal persistence. It verifies the next `ORS0`, then advances and exactly reads back trust; uncertain commit or trust update requires typed reboot reconciliation. Eight groups, the full 38-executable matrix, and 100 repeats pass; no physical durability is claimed.
- **Cleanup:** 4/4 temporary endpoint cleanup checks passed and no lease journal remained.

The physical checkpoint kept real state live on the host and still used host-supplied trust. The new restart checkpoint proves deterministic OpenGauge outbox reconstruction in a fresh host object, not yet coordinated durable on-device state. It does not claim protected physical keys, a direct SX1262 binding, or field/range validation. See [the live-state physical evidence](tests/hardware/OT-017I-2026-08-09.md).

## Intended capabilities

- Compact LoRa messaging, location/status broadcasts, priority alerts, and controlled relaying
- GPS-backed group awareness with graceful operation when GPS or peers disappear
- Portable, vehicle-mounted, repeater, and approximately 7–10 inch touchscreen roles
- Offline maps transferred locally from a phone or computer using a licensed, replaceable package format
- Quick actions such as SOS, medical, recovery, disabled vehicle, fuel/tools, wildlife, and group-defined alerts
- A versioned external interface for normalized critical alerts from OpenGauge or other telemetry producers

These are product goals, not verified capabilities.

## Repository layout

| Path | Purpose |
| --- | --- |
| `docs/` | Architecture, assumptions, decisions, and specifications |
| `firmware/components/` | Hardware-independent and reusable firmware components |
| `firmware/targets/` | Deployable applications for a defined board/role |
| `hardware/` | Board inventories, wiring, power, enclosure, and compatibility evidence |
| `tests/` | Host, integration, protocol, and hardware test assets |
| `tools/` | Development, packaging, provisioning, and diagnostic tools |
| `prototypes/` | Time-bounded experiments that are not production architecture |
| `tasks/` | Prioritized engineering backlog and acceptance criteria |

## Design boundary

OpenTrail owns trail networking, group/location behavior, messaging, maps, and alert presentation/relay. It does not decode raw CAN/J1939. OpenGauge integration occurs only through a documented, normalized, versioned alert interface.

## Start here

Read [the dated progress log](docs/PROGRESS_LOG.md), [the portable-client composition contract](docs/platform/PORTABLE_CLIENT_COMPOSITION_V0.md), [the update/recovery architecture](docs/update/UPDATE_RECOVERY_ARCHITECTURE_V0.md), [the funding packet](docs/funding/README.md), [the four-person standalone pilot](docs/testing/FOUR_PERSON_PILOT_V0.md), [its result evaluator](docs/testing/FOUR_PERSON_PILOT_RESULT_V0.md), [the cryptographic candidate review](docs/security/CRYPTO_CANDIDATE_REVIEW_2026-08-10.md), [the group-load model](docs/testing/GROUP_LOAD_MODEL_V0.md), [the privacy-safe field-log contract](docs/testing/FIELD_TEST_LOG_V0.md), [the latest live-state physical evidence](tests/hardware/OT-017I-2026-08-09.md), [the OpenGauge target recovery plan](https://github.com/nbjelanovic/OpenGauge/blob/main/docs/integration/TARGET_SYSTEM_RECOVERY_ADAPTER_PLAN.md), [the architecture](docs/ARCHITECTURE.md), [project status and assumptions](docs/PROJECT_STATUS.md), [the hardware inventory](hardware/INVENTORY.md), [the Wio Tracker arrival plan](hardware/WIO_TRACKER_L1_PRO_BRINGUP.md), [the critical-alert v0 contract](docs/integration/OPENGAUGE_CRITICAL_ALERT_V0.md), and [the backlog](tasks/BACKLOG.md). Detailed protocol, persistence, security, funding, and evidence documents remain organized under `docs/` and `tests/`. The completed OT-017-series checkpoints listed in the backlog are bounded evidence, not production acceptance. Hardware identity, cryptography, authenticated on-device alert transport, secret storage, and regulatory questions continue in parallel.

## License and contributions

OpenTrail is free/open-source software licensed under the
[Apache License 2.0](LICENSE). Contributions are welcome through GitHub issues
and pull requests; read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting
code or hardware evidence and use [SECURITY.md](SECURITY.md) for sensitive
security reports.
