# OpenTrail Progress Log

Progress is grouped by calendar day, newest first. Detailed acceptance criteria
remain in [the engineering backlog](../tasks/BACKLOG.md); this log is the concise
public chronology.

## 2026-08-11

### Protected-generation map-selector first baseline

- Added a first-use composition that inspects protected history before selector
  access and permits only an exact clean `no_selector` owner with zero history.
- Saved canonical stable selector generation 1 against a private guard, then
  advanced protected history from 0 to 1 with exact readback before publishing
  the first active map.
- Kept a retryable initial trust read clean and retryable. Nonzero history blocks
  selector access; selector-save failure never advances trust; and every
  post-save conflict or uncertainty remains ambiguous-mapless for fresh-boot
  reconciliation.
- Added eleven deterministic groups and completed 100/100 focused repeats. All
  fifteen map suites and the complete 73-executable host matrix pass locally,
  including publication-safety checks. Protected reseed composition, a concrete
  protected backend, reset/replacement authority, target/package locking,
  physical durability, and on-device results remain open.

### Protected-generation map-selector candidate replacement

- Added a replacement composition that derives both selector generation values
  from protected history before selector-store access.
- Kept candidate staging and the commit-last selector save private, then
  advanced and exactly read back trust before publishing trial state.
- Required a final exact trust recheck before a rejected candidate may retain
  the active map. Selector-save failure never advances trust; every post-save
  conflict or uncertain advance remains mapless for reconciliation.
- Added eleven deterministic groups and completed 100/100 focused repeats. All
  fourteen map suites and the complete 72-executable host matrix pass locally,
  including publication-safety checks. Baseline/reseed protected composition,
  a concrete protected backend, target/package-slot locking, physical
  durability, and on-device results remain open.

### Protected-generation map-selector runtime transitions

- Added a runtime composition that obtains both the current selector generation
  and rollback floor from the protected-generation source before selector-store
  access.
- Kept trial reads, deadline handling, fallback completion, and prior cleanup on
  a private guard until unchanged trust is rechecked or a saved selector is
  followed by an atomic protected advance and exact readback.
- Contained a currently visible map after trust failure/change and retained
  protected history after invalid-fallback selector clearing, routing that state
  to service reconciliation instead of treating it as clean first use.
- Added eleven deterministic groups and completed 100/100 focused repeats. All
  thirteen map suites and the complete 71-executable host matrix pass locally,
  including publication-safety checks. Candidate/baseline/reseed protected
  composition, a concrete protected backend, target locking, physical
  durability, and on-device results remain open.

### Protected-generation map-selector boot composition

- Added the first map coordinator that derives its rollback floor directly
  from the protected-generation boundary instead of accepting it from an
  ordinary caller.
- Kept restored and boot-updated selector state private until the protected
  value is rechecked or atomically advanced and read back at the exact selector
  generation.
- Made nonzero trusted history with empty selector media service-required,
  retained only fail-visible mapless state for selector rollback, and withheld
  saved selector state after any uncertain protected advance or final conflict.
- Added ten deterministic groups and completed 100/100 focused repeats. All
  twelve map suites and the complete 70-executable host matrix pass locally,
  including publication-safety checks.
  No protected backend, ESP-IDF composition, physical durability, hardware
  counter, reset/replacement authority, or on-device result is claimed.

### Protected map-selector trusted-generation boundary

- Added a backend-neutral source contract whose mutation must atomically match
  the exact current generation before advancing to a strictly greater value.
- Added a boot-local, non-copyable common enforcer that distinguishes source
  rollback from stale conflict, rejects nonincreasing requests without a write,
  and requires exact post-advance readback.
- Treated every advance error as commit-uncertain. Advance failure, failed
  readback, frozen readback, and advanced-past readback latch all later source
  I/O closed until reconciliation through a fresh boot instance.
- Added ten deterministic groups. All eleven map suites pass 100/100 focused
  repeats, and the complete 69-executable host matrix passes locally. No
  protected backend, target composition, reset/replacement authority, hardware
  counter, ESP-IDF implementation, or physical anti-rollback result is claimed.

### Single-use local-service authorization for map reseed

- Replaced the reseed coordinator's five caller-created booleans with a
  non-copyable permit minted only through an injected authorization backend.
- Bound each grant to the exact boot session, selector-reseed scope, local USB
  or independently authenticated local-wireless transport, activation policy,
  baseline package evidence, reviewed trusted floor, five service
  confirmations, and committed local-confirmation revision.
- Enforced a configurable short lifetime under a hard five-minute ceiling,
  exact-expiry rejection, backend handle consumption, move-without-copy permit
  ownership, and burn-before-selector-access behavior. Remote radio, replay,
  mismatch, invalid time, denial, unavailable backend, and failure mint no
  permit.
- Added ten authorization groups and retained twelve reseed groups. All ten map
  suites pass 100/100 focused repeats, and the complete 68-executable host
  matrix passes. No credential verifier, target service UI, protected replay
  store, audit backend, or on-device authentication is claimed.

### NVS-ready map selector key/value boundary

- Fixed one backend-neutral selector binding: `ot_state` partition label,
  `ot_maps` namespace, and exact 64-byte `otm_sel_a` / `otm_sel_b` blobs.
- Required a backend commit after every staged blob write or key erase. Marker
  commit reads the prepared record, changes only byte 59, rewrites the complete
  blob, commits it, and leaves exact readback to the upper store.
- Kept missing keys distinct from I/O, made missing-key erase idempotent, and
  rejected wrong sizes, bindings, offsets, marker values, and already-committed
  blobs without guessing state.
- Added ten deterministic groups including normal two-slot rotation and
  verified selector clear through the real store. The ESP-IDF/NVS plan fixes
  service scope and physical interruption gates without claiming a target
  backend or hardware durability. At this checkpoint nine map suites passed
  100/100 repeats and the complete 67-executable host matrix passed; the newer
  authorization section above supersedes the current matrix count.

### Authorized map selector service reseed

- Added a separate service-only coordinator for dirty or previously used map
  selector state; clean first use and healthy active replacement remain routed
  to their non-destructive coordinators.
- Required five explicit caller acknowledgements, an already-mapless owner,
  exact policy, valid package evidence, and pre-erase generation review.
- Added `reset_and_verify_empty`: both abstract selector records are erased and
  read back, and reported-success erase without exact emptiness fails closed.
  Package bytes and other persistence domains are never touched.
- Advanced the new stable record beyond the greater of observable local and
  reviewed trusted history, rechecked emptiness through `save_if_empty`, and
  allowed exposure only after commit-last exact readback.
- Added twelve reseed groups and raised the selector store to fourteen groups.
  Partial/dishonest erase, exhaustion, write/commit/readback uncertainty, and a
  post-clear selector race remain mapless. All eight map suites pass 100/100
  repeats, and the complete 66-executable host matrix passes.

### Restart-safe first map baseline

- Added a first-use coordinator that accepts only a clean `no_selector`
  mapless guard, exact policy, fully evidenced package, two readable empty
  selector slots, and zero trusted generation history.
- Avoided an unencodable no-prior trial. The initial package becomes private
  stable state and canonical selector record generation 1 is committed/read
  back before map exposure.
- Added `save_if_empty` so a selector appearing between preflight and save is
  never overwritten. Exclusive target ownership is still required; the check
  is not a lock.
- Refused existing, dirty, unreadable, conflicted, previously trusted, and
  changed selector state. This path cannot reset or reseed a used device.
- Added ten deterministic groups covering success/restart, owner/policy/package
  rejection, trusted history, media states, write/commit/readback failure, and
  the inspect/save race. All six affected suites pass 100/100 repeats; the full
  65-executable matrix is the publication gate.

### Safe replacement-map candidate ordering

- Added a typed candidate coordinator for replacing one stable active map with
  externally staged and fully evidenced alternate-slot package bytes.
- Candidate lifecycle changes occur on a private guard; the trial becomes live
  only after commit-last selector save and exact readback.
- Added exact-generation guarded saves to close the preflight-to-save gap for
  boot, runtime-transition, and candidate coordinators. The caller still must
  provide exclusive store ownership; this is not a lock.
- Invalid package evidence leaves the current active map unchanged. Unreadable,
  conflicting, stale, changed-generation, failed-write, uncertain-commit, and
  corrupt-readback states fail mapless with typed service/reconciliation
  outcomes.
- Kept first-ever map installation explicitly out of scope because restart-
  safe trial state requires a real prior-good package rather than invented
  fallback authority.
- Added eleven deterministic candidate groups, including a newer checkpoint
  appearing after preflight but before save. Candidate, transition, boot, and
  store suites each pass 100/100 focused repeats; the full 64-executable matrix
  is the publication gate.

### Verified runtime map transition persistence

- Added read-only exact live-checkpoint verification to the two-slot store; a
  stale or arbitrary in-memory guard cannot become the next durable state.
- Added a transition coordinator that applies trial reads/time, fallback
  completion, and prior cleanup to a private guard copy and publishes durable
  changes only after commit-last save plus exact readback.
- Avoided writes for healthy reads/time that do not change checkpoint state and
  for rejected operations.
- Verified-cleared only the two selector records when fallback evidence was
  invalid; partial clearing, storage uncertainty, rollback, conflicts, policy,
  and generation mismatch remain fail-visible and mapless.
- Passed thirteen store and thirteen transition groups plus 100/100 focused
  repeats. No physical storage, package deletion, renderer, target task, or
  on-device behavior is claimed.

### Persist-before-exposure map selector boot

- Added a typed boot coordinator that restores only into a private map guard
  and releases stable, trial, fallback, or mapless state through explicit
  outcomes.
- Required every resumed-trial boot increment and trial-limit fallback to pass
  a new commit-last store write plus exact readback before the candidate or
  fallback state becomes live.
- Kept failed writes, uncertain commits, bad readback, unreadable media,
  conflicts, rollback-floor rejection, and package mismatch mapless; the
  private candidate is never exposed.
- Preserved map/communications independence and kept physical storage, package
  access, rendering, trusted-floor ownership, and target task binding outside
  the coordinator.
- Passed ten coordinator groups and 100/100 focused repeats under strict C++17
  warnings-as-errors. No physical or on-device boot result is claimed.

### Recoverable map selector store

- Reserved `OTM0` byte 59 as commit marker `0xA5`; CRC covers the committed
  form, so a complete prepared record with zero there is explicitly invalid.
- Added an abstract two-slot store that writes prepared bytes, commits last,
  verifies exact readback, alternates away from prior-good state, and chooses
  only a unique newest committed generation.
- Preserved older state through partial writes, separated known-uncommitted from
  uncertain commit failures, repaired invalid/uncommitted peers, and rejected
  unreadable slots, equal-generation conflict, and generation exhaustion.
- Added optional external trusted-floor comparison without claiming protected
  anti-rollback state. Trial restore exposes its incremented boot count for the
  boot coordinator to persist before candidate use.
- Passed thirteen store groups plus all five map executables 100/100 repeats.
  No physical backend, atomicity/endurance/power-loss result, authentication,
  target filesystem, renderer, display, or on-device behavior is claimed.

### Restart-safe map selector checkpoint

- Added canonical 64-byte `OTM0/v0` for stable, candidate-trial, and fallback-
  required map state with abstract slots/generations, bounded trial policy,
  record generation, canonical reserved bytes, and CRC-32.
- Wired atomic guard export/restore. Every selected package is re-evidenced;
  trial/fallback also require the exact prior package. Restart discards volatile
  reads/time, increments a persisted boot count, and requires full health again.
- Reaching the trial-boot limit enters visible fallback rather than repeatedly
  restarting an unproven candidate. Corrupt, future, incoherent, policy-
  mismatched, active-mismatched, or missing-prior input fails closed.
- Covered ten checkpoint groups and 100/100 focused repeats alongside 100/100
  unchanged activation-guard repeats with strict warnings-as-errors.
- `OTM0` contains no path, geographic content, identity, credential, key, URL,
  or free text. CRC is not authentication/anti-rollback; durable two-slot
  storage, trusted generation, filesystem integration, and hardware remain.

### Fail-safe offline map activation policy

- Added a fixed-memory C++ guard for mapless/active/staged/trial/fallback state
  without selecting a filesystem, selector record, renderer, or target.
- Boot refuses missing, unreadable, ambiguous, or invalid selections. Staging
  leaves the current map untouched, and trial begins only after an adapter
  confirms the exact staged slot/generation selector was committed.
- Retained the prior package until a bounded complete-read threshold passes.
  Trial read failure, deadline, clock regression, and media removal require an
  exact verified prior restore or enter visible mapless state without guessing.
- Covered ten deterministic groups plus 100/100 focused repeats with strict
  C++17 warnings-as-errors. The interface contains no paths, geographic data,
  identity, secrets, free text, radio, message, alert, or USB authority.
- This is lifecycle-policy evidence, not authentication, selector durability,
  storage/renderer integration, received-display, or on-device evidence.

### Offline map package manifest and verifier

- Added strict `OTMP0/v0` metadata for source/licence/offline rights/visible
  attribution, experimental container/encoding/scheme, Web Mercator coverage,
  exact bytes/tile count/SHA-256, and reader/firmware/storage/scratch needs.
- Limited v0 experiments to MBTiles/JPEG/TMS, PMTiles/JPEG/XYZ, and indexed-
  raster JPEG-or-RGB565/XYZ. Unsupported formats, extra fields, bad bounds,
  incompatible pairs, and insufficient storage fail before package reads.
- Added a read-only host verifier that streams the supplied package digest and
  rejects truncation and same-length mutation. It performs no download, write,
  transfer, mount, signing, activation, deletion, rollback, or rendering.
- Passed seven focused groups covering all accepted candidates, strict shape,
  bounds/zooms, coherence, rights/attribution and blocked public OSM endpoints,
  exact-byte verification, mutation/truncation, and rejected-path redaction.
- Hardened the Windows PowerShell host harness so the four intentional negative
  diagnostic-CLI smoke checks capture native stderr and exit status instead of
  being prematurely terminated by the script-wide stop preference.
- The tests use synthetic bytes. No real map data, provider, package approval,
  received display, or target result is claimed.

### Offline-map architecture gate

- Rechecked current OSMF policy and made the public OpenStreetMap tile servers
  an explicit non-source for offline packs. Any package requires a provider or
  self-hosted pipeline with documented offline/redistribution permission plus
  visible attribution.
- Compared MBTiles 1.3, PMTiles v3, and a pre-rendered indexed-raster reference
  without selecting one before target evidence. The first display spike will
  compare incrementally decoded JPEG with RGB565 and may reject every option.
- Fixed an off-device, immutable update boundary: complete staging and
  verification precede a small recoverable activation; normal reads are read-
  only; prior-good data is retained; interruption or removal falls back to a
  mapless UI without stopping messages, alerts, or privacy controls.
- Added the owner-reported pair of incoming Waveshare 1.75-inch round AMOLED
  boards as unreceived candidates only. Exact variant, shipping firmware,
  storage, display/touch behavior, memory, performance, power, and usability
  remain unverified OT-018 evidence gates.
- This increment is current-source research and a measured-test plan. It adds
  no map data, provider selection, package, target build, renderer, or hardware
  compatibility claim.

### One offline diagnostic entry point

- Added `opentrail_diagnostic_cli` as one operator-facing command for the
  existing strict `OTPD0` position-UI and `OTRD0` update-recovery records.
- Kept each original parser authoritative. Exact supported prefixes dispatch to
  their complete validators; malformed records and unknown prefixes fail with
  fixed errors that do not echo rejected content.
- Preserved the narrow authority boundary: one command-line record only, with
  no file, log, device, radio, identity, location, network, retention, export,
  position-control, update, cleanup, reboot, or recovery access.
- Passed both canonical record smoke checks, malformed supported input,
  unsupported input, all 58 C++ executables, and every Python/publication-
  safety gate in the complete local host run. The exact published increment
  passes in GitHub Actions run `31502841481`.

### Strict offline update-recovery diagnostic decoder

- Added a checked host parser for the exact uppercase
  `OTRD0=XXXXXXXX` logger record. Wrong length/prefix/case/hex, unsupported
  versions, reserved bits, unknown categories, altered flags, and incoherent
  recovery combinations fail before a decoded event is returned.
- Added stable names for every v0 operation, operator state, reason, action,
  and parser error plus a one-shot CLI with deterministic `key=value` output.
  Rejected input is never echoed.
- Kept the decoder local and offline: one command-line argument is the entire
  input, with no file, device, log, network, persistence, retention, deletion,
  or export access.
- Covered ten operator-decoding groups, canonical and invalid CLI smoke tests,
  100/100 focused repeats, the complete 58-executable host matrix, and every
  Python/publication-safety check. Target log binding, accessible rendering,
  execution authority, and physical recovery evidence remain separate gates.

### Strict offline position UI diagnostic decoder

- Added a checked host parser for the exact uppercase
  `OTPD0=XXXXXXXX` logger record. It rejects wrong length/prefix/case/hex,
  unsupported versions, reserved bits, unknown categories, and incoherent
  field combinations before returning any decoded event.
- Added stable operator names for every v0 event, outcome, notice, reason, and
  parser error plus a one-shot CLI that prints deterministic `key=value`
  output. Invalid input produces only a fixed error category and is never
  echoed.
- Kept the decoder local and offline: it reads one command-line argument and
  performs no network, file, log, device, identity, location, retention, or
  export work.
- Covered ten operator-decoding groups, canonical and invalid CLI smoke tests,
  100/100 focused repeats, the complete 58-executable host matrix, and every
  Python/publication-safety check. Target log binding and physical service
  capture remain separate gates.

### Privacy-safe position UI diagnostic event

- Added the versioned 32-bit `OTPD0/v0` adapter for one validated
  position-sharing UI coordinator result. It normalizes presentation, observed
  refresh, action, input rejection, and failure into coarse outcome, displayed
  position notice, reason, and safety flags.
- Routed the fixed `OTPD0=XXXXXXXX` message through the existing bounded logger
  with info/warn/error severity. Runtime filtering is an accepted non-write,
  sink rejection remains visible, and normal idle polls are deliberately
  suppressed to prevent bounded-log churn.
- Kept revisions, timestamps, scheduler/runtime counters, coordinates,
  payloads, messages, peer/device identity, addresses, credentials, and free
  text outside the diagnostic payload. The caller-supplied logger timestamp is
  record metadata, not encoded event content.
- Distinguished successful critical-frame publication from UI service failure,
  and distinguished display containment, unavailable presentation, revision
  exhaustion, clock deferral, outbound fault, stale input, and invalid input
  without exporting source detail.
- Covered ten groups, 100/100 focused repeats, the complete 58-executable host
  matrix, and every Python/publication-safety check. Target log binding,
  retention/export/clear policy, persistence, and physical
  service evidence remain open.

### Observed position-state refresh before input

- Extended the single-owner position UI service to remember the last
  successfully presented semantic frame and compare it with current live
  outbound/scheduler state before polling local input.
- A user-visible transition caused outside the UI path—GPS wait or recovery,
  sink pressure/failure, or a permanent outbound clock fault—now publishes a
  higher revision first. An action queued against the superseded revision is
  left unread during refresh and is rejected as stale on the next service call.
- Compared only screen, attention, notice, summary, and all canonical action
  bindings. Service timestamps, attempt deadlines, and counters do not create
  display churn when the visible meaning is unchanged.
- Failed observed-state publication invokes clock-independent Stop and latches
  further UI input closed. Revision exhaustion does the same before either
  refresh or input can proceed.
- Covered ten dedicated groups, 100/100 focused repeats, the complete
  58-executable host matrix, and every Python/publication-safety check. Exact
  ESP-IDF task/lock serialization, renderer behavior, physical display/input,
  and real concurrent service timing remain open.

### Single-owner position-sharing UI coordination

- Added one cooperative owner for position frame revisions, current live
  presentation, one checked input poll, coordinator-owned Start/Stop, and the
  required result-frame refresh. Target callers no longer assemble copied
  scheduler/runtime state or choose revisions/timestamps for this path.
- Kept retry and mutation behavior distinct. Temporary clock-not-ready changes
  no state and retains the current truthful Start frame/revision; every applied
  or permanent-rejection path publishes a higher-revision active, stopped, or
  critical no-action frame.
- Failed closed around uncertain UI state. Initial display failure can retry the
  same unused revision, while post-action display failure immediately stops
  sharing and latches further input closed. Revision exhaustion stops and
  latches before another input is polled or action applied.
- Covered ten groups, 100/100 focused repeats, the complete 58-executable host
  matrix, and every Python/publication-safety check. Exact ESP-IDF task/lock,
  renderer/retry UX, reboot policy, diagnostics, physical input, and concurrency
  remain open.

### Checked-time outbound position commands

- Moved target-facing Start/Stop authority into the outbound coordinator. Start
  now obtains one checked sample when the action is applied; callers cannot
  provide stale or invented `now_ms`. A successful Start only arms scheduling
  and performs no GPS read, payload submission, delivery, or radio service.
- Kept temporary and permanent clock behavior distinct. Not-ready defers with
  no scheduler access and can be retried with a new sample; source failure or
  rollback stops sharing and latches the boot composition closed; a later Start
  consumes no clock sample. Stop remains immediate and clock-independent.
- Routed the semantic UI action adapter through that live command boundary and
  added bounded command counters. The safety presentation now validates clock
  evidence against service plus command operations while copied status is no
  longer action authority.
- Covered ten command groups plus 100/100 focused repeats, repeated the updated
  safety suite 100/100 times, and passed the complete 58-executable host matrix
  plus every Python/publication-safety check. Target task synchronization,
  concrete clock binding, rendered retry UX, reboot behavior, and physical
  input remain open.

### Fail-visible outbound position safety

- Added a target-facing position presentation overload that requires both
  scheduler and outbound-runtime status. A coherent latched clock rollback
  or source failure now overrides the scheduler's ordinary stopped state with a
  critical `position_sharing_failed` frame containing no actions.
- Validated the coarse runtime status before use. Unknown clock states,
  contradictory fault evidence, impossible counters, and faulted-but-active
  scheduler combinations fail closed to the same safe frame instead of
  offering Start.
- Rechecked action authority through the live coordinator: a Start resolved
  from an older healthy frame is rejected after the fault without scheduler
  mutation; a newly presented fault revision invalidates old input; Stop
  remains safe/idempotent.
- Covered ten groups using real coordinator source-failure and rollback paths,
  100/100 focused repeats, the complete 58-executable host matrix, and every
  Python/publication-safety check. Exact renderer, target synchronization,
  reboot recovery, and physical input remain open.

### Checked-time outbound service coordination

- Added one fixed-memory cooperative coordinator that reads the guarded
  boot-local clock once and uses that exact value for active-sharing location,
  position scheduling, priority handoff, delivery, and opaque-radio service in
  a fixed order.
- Preserved privacy and fail-closed behavior: stopped position sharing does not
  read GPS; temporary clock not-ready invokes no downstream component;
  rollback or source failure stops sharing and latches the coordinator closed
  without consuming later clock samples.
- Kept subsystem failure independent. Missing GPS or invalid position policy
  does not block existing queued traffic, handoff rejection does not block
  already accepted delivery, and full delivery retains priority work until a
  later checked cycle frees capacity.
- Covered ten groups including same-cycle exact position packet delivery to a
  fake-radio peer, 100/100 focused repeats, the complete 58-executable host
  matrix, and every Python/publication-safety check. This is cooperative host
  ordering, not ESP-IDF task/concurrency, inbound processing, authentication,
  target adapters, or physical-radio evidence.

### Loss-aware priority-to-delivery handoff

- Added a fixed-memory, single-owner handoff that peeks the strict-priority/FIFO
  head and removes it only after `DeliveryController` accepts the exact frame.
  Delivery capacity or typed rejection leaves the original priority entry
  available instead of losing it during transfer.
- Preserved the original deadline across layers: remaining priority-queue
  lifetime can shorten but never extend the delivery class expiry. Expiry at
  the queue boundary still wins and remains visible through the queue event;
  impossible commit mismatch latches the coordinator closed.
- Covered ten groups for ordering, exact peek/commit, full-queue deferral,
  duplicate/MTU rejection, expiry, unsafe time spans, and the complete
  scheduler-to-packet-to-priority-to-delivery fake-radio path. The focused
  executable passes 100/100 repeats and the complete 58-executable host matrix
  plus every Python/publication-safety check passes. This is host-only,
  unauthenticated packet-v0 composition, not real-coordinate or physical-radio
  evidence.

### Experimental position packet and priority admission

- Added a fixed-memory sink that revalidates only canonical current-position
  payloads, obtains injected ephemeral packet-v0 metadata, encodes the exact
  38-byte frame, and admits it as `MessageClass::position` background traffic.
- Passed the scheduler's actual service timestamp into its sink so queue
  creation and expiry start from real attempted work, not an unrelated clock
  sample. Metadata is consumed before admission and never reused after pressure.
- Kept failure semantics typed: metadata or queue not-ready/rate pressure is
  retryable; reserved/full capacity maps to full; malformed/noncurrent payload,
  invalid metadata, duplicate IDs, invalid policy, and queue failure fail closed.
- Covered ten groups including scheduler composition, decode round-trip,
  priority ordering, exact expiry, pressure, and failure paths; the focused
  executable passes 100/100 repeats and the complete 58-executable host matrix
  plus every Python/publication-safety check passes. This is explicitly
  unauthenticated packet-v0 host evidence, not real-coordinate or radio use.

### Local position-sharing privacy control

- Added fixed semantic notices for stopped, active, waiting-for-fix, deferred,
  and failed position sharing, plus explicit start/stop actions through the
  existing checked local-interface boundary.
- Kept authority narrow: start only arms the scheduler and performs no sink
  submission; stop disables it immediately. Unrelated or unknown UI actions
  cannot mutate scheduler state, and repeated start/stop remains idempotent.
- Preserved fail-visible behavior. Missing fixes and recoverable sink/encoding
  conditions remain warning states with stop available; invalid policy,
  monotonic rollback, and time exhaustion become critical action-free faults.
- Covered ten groups through button and touch capability shapes, 100 focused
  repeats, the complete 58-executable host matrix, and every Python/publication-
  safety check. Exact renderer/text, target synchronization, direct radio/GPS
  composition, and physical privacy UX remain open.

### Start/stop position broadcast scheduling

- Added a fixed-memory scheduler around the existing canonical 16-byte position
  payload. It starts only by explicit command, stops immediately, and treats
  repeated start while active as idempotent rather than a forced send.
- Scheduled the next cadence from the actual accepted time and the next retry
  from actual deferred work. Delayed service submits only the newest snapshot
  once, preventing a stale catch-up queue.
- Allowed only current validated fixes into the injected sink. Unavailable,
  stale, invalid, and malformed-current snapshots are suppressed or rejected
  before sink access; not-ready/full/failure outcomes remain typed.
- Covered ten groups, 100 focused repeats, the complete 58-executable host
  matrix, and every Python/publication-safety check. Exact cadence, rendered UX,
  authenticated packet/priority composition, direct radio/GPS binding, field
  behavior, and regulatory acceptance remain open.

### Fail-visible update recovery presentation

- Connected decoded `OTRD0/v0` outcomes to the existing fixed semantic UI
  frame instead of introducing a parallel renderer or free-form recovery text.
- Mapped nonblocking trial, rejected-transition, and cleanup states to warning
  notices with acknowledgement only. Rollback, safe mode, service, and reboot
  reconciliation become critical system-fault frames with no reboot, cleanup,
  confirmation, or service execution action.
- Made corrupt/unsupported/incoherent diagnostic words fail visibly to a generic
  critical service-required frame when a valid revision exists; revision zero
  cannot create a presentable frame.
- Covered nine groups through the real checked local-interface boundary, 100
  focused repeats, the complete 58-executable host matrix, and every Python/
  publication-safety check. Exact renderer, target task/revision ownership,
  physical recovery execution, and operator workflow remain open.

### Bounded runtime diagnostic ring

- Added a production-facing fixed-capacity `LogSink` that retains the newest 32
  canonical records in RAM, assigns boot-local sequences, and snapshots the
  complete retained set oldest-first without partial caller output.
- Made pressure visible: normal rollover overwrites exactly the oldest entry
  and increments its counter, while malformed direct records are rejected and
  reach the existing logger's sink-drop accounting. Clear erases records but
  preserves boot-local order and lifetime counters.
- Kept the boundary narrow: it allocates no dynamic storage, accepts redacted
  records only as `[REDACTED]`, and is neither a serialized/persistent format
  nor an internally synchronized target service.
- Covered eight groups including actual `OTRD0` capture, 100 focused repeats,
  the complete 58-executable host matrix, and every Python/publication-safety
  check. Exact target composition, measured RAM/timing, persistent audit/export,
  and physical failure capture remain open.

### Versioned redacted update-recovery diagnostic event

- Added canonical `OTRD0/v0`: one 32-bit recovery outcome logged through the
  existing bounded logger as exactly `OTRD0=XXXXXXXX` under the fixed
  `update-recovery` component.
- Omitted observed/trusted generations and all identity, policy, checkpoint,
  key, address, and raw adapter detail. The mandatory redaction bit, fixed magic
  and version, zero reserved bits, enum ranges, flags, and state/action/reason
  coherence all fail closed on encode and decode.
- Kept logger authority intact: info/warn/error severity follows operator state,
  runtime filtering is an accepted non-write, and full-sink rejection remains
  visible rather than becoming false success.
- Covered eight scenario groups, 100 focused repeats, the complete 58-executable
  matrix, and all Python/publication-safety checks. Target sink binding,
  persistent retention/export, display rendering, and physical failure capture
  remain open.

### Redacted update-recovery operator status

- Added one fixed, pointer-free operator record for boot, normal save, and
  trial-time transition outcomes: coarse state, reason, action, two generation
  values, and bounded success/attention/reboot/confirmation/cleanup flags.
- Structurally excluded hardware and candidate identity, checkpoint payloads,
  raw guard/trusted errors, and nested persistence results. The record is
  trivially copyable and bounded to 32 bytes.
- Validated source state, reason, flags, generations, operation, guard outcome,
  lifecycle publication, and nested persistence coherence. Unknown, default,
  incomplete, or contradictory input fails closed to blocked service with no
  inherited continue, reboot, confirmation, or cleanup claim.
- Covered eight scenario groups, 100 focused repeats, the complete 43-executable
  host matrix, and all Python/publication-safety checks. Target logging,
  rendering, retained audit, reboot/cleanup execution, and physical service UX
  remain open.

### Durable trial-time update transitions

- Added a lifecycle-transition coordinator for health reporting, checked ticks,
  confirmation, and explicit rollback. Every operation runs against a private
  guard copy first.
- Boot-local health/time changes publish without flash writes. Confirmation or
  rollback intent becomes live only after the next checkpoint and exact trusted
  generation are verified; failed persistence leaves the attempted state
  unpublished, stops the original guard, and requires the typed recovery path.
- Covered committed confirmation/rollback, both deadline routes, rejected and
  volatile behavior, uncertain writes, generation mismatch, and post-write
  trust failures across ten groups, 100 focused repeats, and the complete 43-
  executable matrix.
- Kept the target claim bounded: staging/install, boot and rollback execution,
  terminal cleanup/reset, scheduling/watchdog behavior, protected backends, and
  physical interruption evidence remain open.

### Verified normal update persistence

- Added read-only two-slot checkpoint inspection so normal persistence can
  compare local state with independent trust without restoring a guard or
  changing storage. Empty, known-degraded, unreadable, invalid-only, and
  equal-generation conflict results remain distinct.
- Added a typed save coordinator that requires a running guard and exact
  local/trusted generation agreement, verifies the next checkpoint before
  advancing trust, and requires exact trust readback before reporting committed.
  Rollback enters safe mode; local-ahead, uncertain-write, and post-write trust
  failures require reboot reconciliation and no same-boot retry.
- Covered ten save-coordinator groups and the expanded 20-group checkpoint store
  in the complete 41-executable matrix and across 100 focused repeats each.
- Kept the claim bounded: this generic coordinator persists an already-mutated
  guard. A target-facing wrapper must still own lifecycle mutation, persistence
  failure shutdown, scheduling, reboot, protected storage/trust, and physical
  interruption evidence.

### Typed update-recovery boot ordering

- Added a boot coordinator that starts/restores only a private guard and exposes
  it to the application only after exact image validation and all required
  persistence steps succeed.
- Persisted each new trial attempt and boot-mismatch/attempt-limit rollback
  decision before release; exact rollback completion is persisted before the
  recovered baseline can operate.
- Advanced and exactly read back the injected trusted generation only after a
  fully verified checkpoint write. Uncertain checkpoint or trust state requires
  reboot reconciliation and leaves the live guard untouched.
- Covered 15 typed baseline/trial/rollback/terminal/failure groups in the
  complete 40-executable matrix and across 100 focused repeats. No ESP-IDF boot
  task, protected backend, authorized cleanup/reset, or physical result is
  claimed.

### Trusted update-generation boundary

- Added an explicit trusted-floor restore path that rejects missing or stale-
  but-valid `OTU0` media before any live boot-guard mutation.
- Added save allocation beyond the greater of the newest local checkpoint and
  caller-supplied last-trusted generation, including fail-closed 64-bit
  exhaustion before export or write.
- Covered absent, rollback, exact-boundary, newer, local-ahead, trust-ahead,
  and exhausted cases. All 16 checkpoint-store groups pass in the complete
  host matrix and across 100 focused repeats.
- Kept the security claim bounded: OpenTrail consumes but does not yet provide
  a hardware-backed trusted source or authenticated target checkpoint store.

## 2026-08-10

### Canonical update-state checkpoint

- Added the fixed 64-byte `OTU0/v0` record for hardware/version/policy binding,
  candidate state, trial-boot count, rollback reason, and a future store-owned
  generation.
- Wired atomic export/restore into the update boot guard; trial restoration
  preserves the attempt count but clears session, clock, and accumulated health
  so a restarted image must prove health again.
- Added an abstract two-slot store that owns generation allocation, preserves
  the previous valid record across partial/corrupt writes, verifies exact
  readback, repairs known invalid redundancy, and refuses unreadable or
  equal-generation-conflicted media.
- Covered deterministic round trip, restart, rollback completion, exact policy
  mismatch, corruption/canonical/version failures, invalid state, and output
  preservation across eight codec/guard groups plus 100 repeats, then ten store
  groups plus 100 repeats; the complete host matrix passes.
- Kept authenticated two-slot target storage, trusted generation persistence,
  ESP-IDF/bootloader adapters, power interruption, wear, and physical recovery
  explicitly open.

### Fail-safe update and recovery boundary

- Defined signed, hardware-bound release bundles and a USB-first transport that
  cannot weaken artifact verification.
- Required inactive-slot write/readback, persisted bounded trial boot,
  independent health confirmation, automatic rollback, and a trusted version
  floor.
- Added a pure lifecycle guard with eight passing host groups for candidate and
  write evidence, stable health, exact deadline, boot/clock failures, bounded
  trials, and exact rollback completion.
- Kept exact target partitions, signer/key custody, implementation, protected
  rollback storage, and physical interruption/recovery evidence explicitly
  open.

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
