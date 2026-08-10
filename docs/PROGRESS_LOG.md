# OpenTrail Progress Log

Progress is grouped by calendar day, newest first. Detailed acceptance criteria
remain in [the engineering backlog](../tasks/BACKLOG.md); this log is the concise
public chronology.

## 2026-08-10

### Verified protected-fragment reassembly

- Added a fixed-memory reassembler that accepts only future crypto-adapter-
  produced verified fragment metadata/plaintext and never parses raw radio
  packets or manufactures authentication.
- Bounded state to four concurrent messages, 16 fragments, 103 bytes per
  fragment, and 1,648 bytes per complete message with no dynamic allocation.
- Covered the real 39+25-byte alert shape out of order, exact duplicates,
  changed-byte/count conflicts, invalid context, full capacity, exact timeout,
  clock rollback, and maximum-size completion across ten groups.
- Released no partial plaintext and kept packet-v1, signature scope, AEAD,
  receiver replay persistence, retry behavior, target resources, and physical
  evidence open.

### Critical-alert protected-radio feasibility

- Applied the corrected signed-group profile to the real 64-byte `OGA0` alert
  and `OGK0` ACK instead of assuming either would fit one radio frame.
- Each requires two candidate fragments, 312 transmitted bytes, and 1,025,024
  us theoretical source airtime at the 163-byte example MTU and bench PHY.
- Alert plus ACK is four source fragments; one exact-byte repeater copy raises
  aggregate theoretical transmission airtime to 4,100,096 us before retries,
  contention, or scheduling.
- Added a tenth deterministic budget group and kept fragmentation blocked until
  signature scope, nonce/replay/reassembly, ACK/retry, bounded-resource, target,
  and regulatory gates close.

### Protected-header destination reconciliation

- Stopped before encoding the 36-byte candidate header because it could not
  carry both the forwarding policy's authenticated 64-bit destination alias and
  fragment metadata.
- Corrected the sizing profile to 44 header bytes with explicit destination and
  no mutable TTL; base overhead is now 60 bytes and the signed-group candidate
  leaves 39 plaintext bytes at the 163-byte example MTU.
- Updated all then-existing nine budget groups and current architecture/status/backlog claims;
  the 16-byte signed position is now 140 bytes/461,312 us at the bench PHY.
- Kept final offsets, flags/types, fragment/reassembly rules, signature coverage,
  AEAD, destination privacy, and packet-v1 approval open.

### Canonical traffic-key derivation context

- Added the fixed 52-byte `OTKD/v1` public context for a future audited KDF.
- Bound nonzero group ID, epoch, full authoritative sender fingerprint, and
  distinct AEAD-key, nonce-prefix, and counter-domain purposes in network byte
  order; short aliases and display names are excluded.
- Failed closed with zero output for zero group/epoch/fingerprint or unknown
  purpose across eight deterministic scenario groups.
- Kept epoch secrets, KDF selection, secret outputs, wiping, independent
  vectors, AEAD, and target evidence behind the exact-device benchmark gate.

### Lease-bound AEAD nonce composition

- Added an algorithm-neutral 96-bit nonce composer that requires the durable
  counter lease and traffic key to carry the same nonzero 128-bit domain.
- Fixed the candidate nonce bytes as a crypto-adapter-supplied four-byte prefix
  followed by the nonzero 64-bit counter in network byte order.
- Failed closed with zero output for missing domains, domain mismatch, or
  counter zero; seven deterministic scenario groups cover canonical and
  boundary cases.
- Kept KDF/key/prefix derivation, AEAD, packet-v1, target storage, and library
  selection behind OT-005's exact-target benchmark gate.

### Hardware and US regulatory reconciliation

- Reconciled the two runtime-identified Heltec V4 OLED companions and the Seeed
  SenseCAP Solar repeater against official manufacturer family documentation
  without claiming exact SKUs from runtime strings.
- Recorded family-level MCU/radio/display/GNSS/power/antenna characteristics
  separately from facts observed on the connected units.
- Added a fail-closed US field gate requiring physical model/revision and FCC ID,
  equipment-grant/exhibit review, installed antenna/gain/cable evidence, and a
  frozen firmware/radio configuration before any authorization claim.
- Kept OT-003A partial: a USA preset, 902-928 MHz center frequency, or lower
  transmit power does not independently prove Part 15 authorization for the
  62.5 kHz MeshCore mode.

### Portable-client composition and whole-contract review

- Added a hardware-independent composition preflight for the first self-
  contained portable client. It binds radio, GPS, diagnostics, two distinct
  storage surfaces, entropy, monotonic time, power, display, and local input.
- Audited every abstract target-facing interface. The review caught the separate
  704-byte replay-checkpoint storage obligation instead of incorrectly treating
  the existing 64-byte multi-domain storage surface as sufficient.
- Exposed power-policy and display-capability validation as shared pure checks,
  then reused them in composition so preflight performs no power/display/input
  I/O and cannot drift from component rules.
- Aggregated all missing and incompatible bindings, validated required/observed
  radio MTU plus UI action/hold capability, and kept GPS no-fix and entropy not-
  ready as valid structural states.
- Passed eight composition groups and the complete 33-executable host matrix.
  No ESP-IDF target, concrete adapter, board build, pin/partition map, rendered
  UI, or physical hardware behavior is claimed.

### Local display and input foundation

- Added a production-facing fixed semantic-frame boundary for small OLED/button
  clients, touch displays, and later adapters without exposing pixels, touch
  coordinates, GPIO identities, or private peer/message content to application
  state logic.
- Bound every normalized action-slot event to the exact successfully presented
  boot-local revision; stale, disabled, out-of-range, unknown, not-ready, and
  failed input is explicit and cannot resolve an action.
- Required a canonical critical-confirmation screen and hold gesture before the
  local confirm request resolves; this remains separate from radio delivery.
- Passed twelve capability/frame/failure/revision/input/critical/system-fault/
  bounded-fake groups, the complete 32-executable host matrix, and 100 focused
  repeats.
- Kept renderers, localization, accessibility, readability, target adapters,
  distracted-driving policy, resource/power measurements, and physical display/
  input evidence explicitly open.

### Power-state foundation

- Added a production-facing atomic power observation and evaluator that keeps
  source readiness, external power, battery presence, charge state, optional
  percentage/voltage, and monotonic sample age distinct.
- Required composition to inject low/critical percentage and staleness policy;
  the common component does not infer percentage from voltage or choose a
  hardware threshold.
- Passed eleven normal/low/critical/charging/external-only/missing/fault/time/
  validation/FIFO groups, the complete 31-executable host matrix, and 100
  focused repeats.
- Kept board adapter, charger/shutdown behavior, hardware thresholds, endurance,
  rendered UX, and physical power-failure evidence explicitly open.

### Monotonic clock foundation

- Added a production-facing checked boot-local millisecond boundary with typed
  not-ready, source-failure, rollback, and latched-fault outcomes plus fixed
  saturating status counters.
- Defined equal ticks as valid, temporary not-ready as recoverable, and source
  failure or decreasing time as closed for the current boot composition without
  consuming later samples after the latch.
- Passed eight lifecycle/failure/boundary groups, the complete 30-executable
  host matrix, and 100 focused repeats.
- Kept ESP-IDF timer/task binding, deep-sleep/brownout behavior, accuracy/drift,
  long-run continuity, and physical failure injection explicitly open.

### Secure randomness foundation

- Added an algorithm-neutral production-facing randomness interface with typed
  not-ready, ready, and failed entropy state, bounded 1-64-byte requests, and
  an atomic full-output-or-no-change rule with no weaker fallback.
- Isolated a predictable 512-byte scripted source under test support only; it
  exposes failure injection and exact attempt/success/consumption accounting.
- Passed eight readiness, failure, exhaustion, retry, transition, validation,
  and boundary groups, the complete 29-executable host matrix, and 100 focused
  repeats.
- Kept ESP-IDF entropy/DRBG binding, production key generation, cold-start,
  reboot, brownout, radio/ADC concurrency, and physical evidence explicitly open.

### Funding foundation

- Added a brand-neutral funding packet with a factual project sheet, reusable
  application answers, a vendor-neutral hardware request, preliminary
  milestone budget, qualification/submission checklist, and dated opportunity
  register.
- Kept the public product name replaceable and identified the legal applicant,
  payee, final bill of materials, quotes, and opportunity eligibility as
  required pre-submission decisions. No application or external request was
  sent.
- Preserved local-first operation: the optional server may add recovery and
  convenience but cannot become a requirement for base radio communication.
- Placed every cash, hardware, discount, loan, sponsorship, and service-credit
  request on owner-directed hold. The packet remains preparation material only;
  no opportunity research for outreach, contact, submission, account activity,
  acceptance, shipment, or announcement is authorized.

### Field-test capacity foundation

- Added a deterministic group-load model and runnable CLI for the planned four
  standalone, four-plus-repeater, and eight-plus-repeater field phases.
- Separated logical messages, source attempts, repeater copies, and exact LoRa
  airtime. Under one fixed one-hour profile, scheduled demand is 1.2727%,
  2.5455%, and 5.0911%; the model explicitly makes no RF, collision, range, or
  regulatory claim.
- Added a privacy-safe live-session evidence contract covering loss, duplicates,
  retries, latency distribution, counters, queues, resets, GPS state, power, and
  route/environment context.
- Passed the expanded matrix locally and in public GitHub Actions: 24 strict C++
  executables, three CLI builds, and the four-group Python MeshCore lease suite.

### Four-person pilot definition

- Defined the first live-test boundary as four identical self-contained clients
  with no repeater, server, internet, phone, laptop, or vehicle connection
  required during the session.
- Added a machine-validated one-hour plan with at least three broad environment
  classes, 300 message origins, 900 peer-delivery opportunities, privacy-safe
  evidence requirements, and provisional pass/fail thresholds.
- Kept the plan explicitly `draft_blocked`: it cannot claim readiness until one
  exact client model and firmware are frozen across four units with battery,
  enclosure, GNSS, display, local input, and USB recovery.
- Added two fail-closed host groups for canonical plan validation, traffic math,
  and the blocked-to-ready hardware transition. The complete current-main host
  matrix passes publicly in GitHub Actions.
- Added the strict `OTPR0/v0` result contract and evaluator. It derives expected
  traffic from the plan and distinguishes an eligible pass, eligible measured
  failure, ineligible setup, and malformed/privacy-unsafe record.
- Added six result groups covering exact-match pass, blocked-plan refusal,
  threshold failure, setup mismatch, privacy rejection, canonical shape, and
  impossible delivery counts. They pass publicly in GitHub Actions.
- Added fail-closed result-template generation. It refuses a blocked plan,
  unknown scenario, existing output, or abandoned temporary output; copies only
  frozen public configuration; and leaves evidence claims false. Three added
  groups bring the publicly passing result suite to nine.

### Cryptographic decision gate

- Completed a current primary-source review of the official Espressif libsodium
  component, pinned ESP-IDF mbedTLS/PSA direction, Monocypher, Noise, and
  Noise-C. Made libsodium the first target benchmark, retained mbedTLS/PSA and
  Monocypher comparisons, and limited Noise-C to reference/vector use.
- Selected Noise XK only as the leading invitation prototype when a signed
  invitation pins a separate X25519 Noise key to the Ed25519 device identity;
  refused silent downgrade or plaintext group-key transfer.
- Recorded entropy, sender-specific traffic keys, rollback-safe nonce counters,
  resource/interoperability benchmarks, protected storage, and physical
  lifecycle evidence as hard gates before packet v1 or a security claim.
- Prohibited irreversible Secure Boot/flash-encryption/eFuse experiments on the
  current bench radios; production enablement needs a sacrificial device and a
  separately proven recovery path.
- Added the algorithm-neutral `OTCN` outbound counter lease store. It commits
  nonoverlapping ranges before use, keeps its two slots separate from existing
  protocol/configuration/secret state, and skips unused ranges after restart.
- Added ten host groups covering allocator lifecycle, exact record/domain
  isolation, restart, mismatch, corruption/conflict/version, exhaustion, reads,
  malformed state, and every persistence mutation boundary. They pass locally
  and in public GitHub Actions run `31368305188`; target protected storage,
  physical power loss, nonce packing, and AEAD remain.
- Added the strict `OTCB0/v0` cryptographic benchmark plan/result boundary and a
  public `draft_blocked` ESP32-S3 plan. A ready plan requires exact target,
  toolchain, sdkconfig, candidate commits/locks/licenses, and radio profile.
- Added eight evidence groups covering ready/block transitions, exact candidate
  and gate sets, canonical plan hashing, no-overwrite result templates,
  complete pass/measured fail, config mismatch, privacy rejection, and invalid
  measurements. They pass locally and in public GitHub Actions run
  `31369215213`. No target benchmark or library selection is claimed.
- Added a fixed-memory protected-packet budget model that charges every fragment
  explicit authenticated-header/tag overhead and theoretical LoRa airtime.
- Added eight groups covering the 163/255-byte MTU examples, a 16-byte position,
  a larger layered header, a 300-byte three-fragment message, empty payload,
  exact fragment boundary, fragment-limit refusal, and invalid PHY/capacity.
  They pass locally and in public GitHub Actions run `31369948699`; this initial
  36-byte header estimate was superseded later the same day by the 44-byte
  destination-inclusive reconciliation above.
- Recorded Decision 0004 after comparing OSCORE's inner/outer split, IPsec
  mutable-field handling, COSE countersignatures, and the RFC Editor final-review
  Group OSCORE design. The first release permits one authorized repeater to
  validate and forward exact immutable bytes once; no protected TTL is rewritten.
- Separated group AEAD access from individual source authentication. A 64-byte
  initial Ed25519 signed-group estimate raised the 163-byte-MTU overhead to 116
  bytes and left 47 plaintext bytes. The destination-inclusive reconciliation
  above supersedes those numbers with 124-byte overhead and 39-byte capacity.
  A ninth budget group passes locally; target signature cost,
  exact wire format, and target evidence remain. The expanded budget passes in
  public GitHub Actions run `31371354045`.
- Added the algorithm-neutral immutable single-repeater forwarder. It requires
  adapter-supplied source authentication/authorization, verified context/epoch,
  and immutable forwarding permission before replay observation, then queues
  the exact protected bytes without a TTL rewrite.
- Added nine groups covering exact bytes, fail-before-replay auth/context/
  permission, exactly one authorized repeater, duplicates/reflections, self/
  local destination, queue/rate congestion, non-rescuable replays, exact expiry,
  enqueue/process clock regression, and invalid input. The full 27-executable
  matrix passes locally and in public GitHub Actions run `31371354045`;
  cryptography, target binding, restart persistence, and field evidence remain.
- Added the single-repeater replay coordinator. It requires protected storage-
  namespace evidence matching the expected group/epoch, restores and repairs
  the two-slot duplicate checkpoint before operation, and readback-verifies a
  new checkpoint before a queued exact frame can be released.
- Failed or uncertain saves and unreadable media disable forwarding. Queue/rate
  congestion observations are also persisted, preventing reboot from rescuing
  a consumed forwarding opportunity into amplification.
- Added nine restart/failure groups covering authorized first provisioning,
  missing checkpoint service, restart suppression, failed-save transmit block,
  congestion persistence, known-degraded repair, epoch mismatch, unreadable
  media, binding, clean owner, and one-shot boot. The complete 28-executable
  matrix passes locally and in public GitHub Actions run `31372816356`.
- Accepted the explicit availability tradeoff: because only replay state is
  durable, power loss after verified save but before radio transmission can
  lose the volatile frame. A coordinated durable outbox, protected target
  storage, rollback defense, power cuts, and endurance remain later gates.
- Evolved the same fixed 704-byte checkpoint envelope to context-bound
  `ODS0/v1`. The old eight reserved header bytes now carry the 64-bit group-
  context ID and the old four reserved tail bytes carry the group epoch; every
  inner replay key must match that epoch.
- Added typed refusal for zero bindings, valid records from another group or
  epoch, and structurally valid legacy unbound v0 media. Neither restore nor
  save mutates/overwrites those records, and the repeater coordinator maps
  bound-media and legacy cases to service before operation.
- Expanded the store suite from nine to ten groups while retaining nine
  coordinator groups. The complete 28-executable matrix and 100 consecutive
  focused repeats of each suite pass locally; the exact published matrix passes
  in GitHub Actions run `31374678550`. No target storage, authenticated
  integrity, anti-rollback, physical migration, power-cut, or endurance claim
  is made.

### Privacy-safe hardware evidence

- Added the `OTFL0` converter/validator so raw local captures can be published as
  aggregate role-labeled evidence without transport ports, serials, addresses,
  channels, coordinates, keys, PINs, or secrets.
- Added four deterministic groups covering redaction, count/latency/topology
  coherence, prohibited key/value detection, and atomic overwrite refusal.
- Ran a one-minute alternating two-second three-radio burst: 30/30 delivered,
  zero loss/duplicates/new errors, median 268.4 ms, p95 275.6 ms, maximum
  431.2 ms, exact +30 repeater flood RX/TX, repeat preserved, empty final client
  queues, 2/2 cleanup, and no remaining lease journal.
- Published only the validated aggregate JSON and dated interpretation; the raw
  capture remains in ignored local build state.
- Confirmed the privacy-safe logger expansion in public GitHub Actions before
  adding the separately scoped pilot-plan validation groups.

## 2026-08-09

### Connected hardware

- Used two Heltec V4 OLED USB Companion nodes and one Seeed SenseCAP Solar
  repeater with matching USA/Canada radio settings.
- Completed a five-hour alternating three-radio bench soak: 300/300 deliveries,
  zero loss/duplicates/errors, exact +300 repeater flood RX/TX, repeat preserved,
  and verified cleanup.
- Completed a post-soak packet-v0 regression and role-reversed physical
  OpenGauge alert/ACK cycles through the same three-radio setup.
- Recorded a non-destructive arrival plan for the ordered Wio Tracker L1 Pro;
  it has not arrived or been tested.

### OpenTrail and OpenGauge integration

- Exercised accepted, terminal stale-rejection, retryable rate-limit,
  retry-to-accept, and live-state alert/ACK lifecycles with zero observed radio
  loss, duplicates, or new errors.
- Added OpenGauge restart checkpoints for ACK replay and outbox state, combined
  them in `OCR0`, and added a recoverable two-slot host store with store-owned
  generations and uncertain-commit reconciliation.
- Added OpenGauge's canonical `OPA0` peer-authorization checkpoint as the next
  prerequisite for restoring authorization epochs after reboot. It has a
  passing 33-executable host matrix and 100 focused repeats.
- Added OpenGauge's `OPS0` two-slot host store around `OPA0`. Automatic
  generations, exact readback, ten interrupted-write boundaries, full-write
  error reconciliation, the complete 34-executable matrix, and 100 focused
  repeats pass.
- Added OpenGauge's `ORS0` system checkpoint, binding peer authorization and
  ACK/outbox recovery to one generation. Dependency-correct private candidates
  preflight all three owners before live import; the complete 35-executable
  matrix and 100 focused repeats pass.
- Added a recoverable two-slot host store for exact `ORS0` generations.
  Store-owned allocation, exact readback, eleven interrupted-write boundaries,
  full-write error reconciliation, the complete 36-executable matrix, and 100
  focused repeats pass.
- Added a caller-owned trusted-generation boundary: below-floor restore is
  rejected before live import, and new saves advance beyond trusted/local state.
  Ten focused groups, the unchanged 36-executable matrix, and 100 repeats pass.
- Documented OpenGauge's target recovery-adapter boundary: two-slot persistence,
  protected opaque-handle resolution, a separate trusted-generation source,
  boot/save ordering, coordinated reset, and physical evidence requirements.
  No on-device implementation is claimed.
- Added OpenGauge protected key-handle preflight to direct and stored `ORS0`
  restore. Active handles validate before any live import, revoked peers are
  skipped, and failures remain typed. Eight system and eleven store groups plus
  100 repeats each pass.
- Added OpenGauge's typed system-recovery boot coordinator. It distinguishes
  genuine first boot, restored, degraded, safe-mode, and service-required
  outcomes, and enables transport only after protected-key restore plus exact
  trusted-floor reconciliation. Nine groups, the 37-executable matrix, and 100
  repeats pass.
- Added OpenGauge's verified recovery-save ordering. Local and trusted
  generations must exactly agree before a normal save; checkpoint verification
  precedes trust advancement and exact readback. Missing/behind/ahead/uncertain
  states remain typed. Eight groups, the 38-executable matrix, and 100 repeats
  pass.
- Hardened OpenGauge boot recovery for an unreadable peer slot that could hide a
  newer committed generation. Unlike known empty/invalid media, unreadable state
  now remains service-only with no trust advancement or transport enablement.
  Ten boot groups, the 38-executable matrix, and 100 repeats pass.
- Added OpenGauge known-degraded repair for exactly one valid plus one known
  empty/invalid slot. It revalidates boot evidence, commits the next generation,
  advances/readbacks trust, and proves both slots valid; unreadable/stale/
  uncertain cases fail closed. Five groups, the 39-executable matrix, and 100
  repeats pass.
- Added OpenGauge's redacted recovery-status boundary for boot, save, and
  repair results. It retains operator state/reason/action, slot health,
  generations, protected-key failure class, and transport/repair flags while
  structurally omitting peer IDs and key handles. Unknown/incoherent results
  fail closed. Seven groups, the 40-executable matrix, and 100 focused repeats
  pass locally.
- Added OpenGauge's versioned diagnostics adapter for that status. One atomic
  32-bit ring event carries only the coarse outcome, slot health, protected-key
  failure class, and flags; generations and identity-bearing fields are omitted,
  and malformed/incoherent words fail closed. Eight groups, the 41-executable
  matrix, and 100 focused repeats pass locally.

### Project operations

- Published Apache-2.0 licensing, contribution guidance, and security reporting.
- Reorganized the repository home page so current hardware, results, limitations,
  and this dated log are visible before the deeper architecture material.
- Added OpenTrail's own public GitHub Actions workflow. The commit-pinned Windows
  2025/Python 3.13/UCRT64 job builds both verifier CLIs and passes all 23 C++
  test executables plus the four-group Python MeshCore lease suite. Its
  current-main
  warning-free run returned zero annotations.
- Linked the public OpenGauge GitHub Actions workflow that validates the shared
  Windows host matrix on every `main` push and pull request. Its current-main
  warning-free run passes all 41 executables with zero annotations.

## 2026-08-08

### Foundation

- Bootstrapped OpenTrail as its own repository with architecture, project status,
  hardware inventory, and an evidence-based backlog.
- Identified and configured both Heltec V4 OLED companions over USB without
  persisting private pairing identifiers.
- Added deterministic host foundations for the radio abstraction, packet v0,
  delivery behavior, diagnostics, GPS state, position encoding, group lifecycle,
  and recoverable non-secret configuration.

### Three-radio repeater evidence

- Runtime-identified the Seeed SenseCAP Solar node as a MeshCore repeater.
- Proved bidirectional close-bench relay and then a software-forced one-hop route;
  disabling repeat made the same route fail, providing the negative control.
- Preserved the temporary-channel cleanup evidence and recorded the remaining
  field-range, exact-SKU, power, antenna, and regulatory gates.
