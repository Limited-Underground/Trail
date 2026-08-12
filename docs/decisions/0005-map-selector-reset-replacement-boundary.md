# Decision 0005: Map Selector Reset and Replacement Boundary

Status: accepted architecture direction; common preparation and stable
activation implemented, target authority and physical composition pending,
2026-08-11

## Decision

OpenTrail will keep ordinary factory reset, authorized selector service reseed,
same-device protected-source recovery, and whole-device commissioning as four
separate operations.

1. Ordinary factory reset must preserve both map-selector records and protected
   map-generation history.
2. Selector service reseed may replace only selector records and only while the
   protected source is intact. Its replacement generation must advance beyond
   protected and observable selector history through the existing exact-bound,
   single-use authorization path.
3. Loss, erasure, or replacement of protected history on the same physical
   device cannot be treated as first use and cannot self-authorize recovery in
   common firmware. It requires a future independent external recovery
   decision, otherwise the map remains unavailable.
4. A whole-device replacement must be independently established as a new blank
   device and commissioned into a fresh trust domain. Retained selector state
   cannot be imported through this route.

The common reset/replacement component is only a fixed-shape classifier. It
will not expose erase, protected-reset, generation-lowering, credential, or
state-import authority.

## Why

The map selector and protected generation are intentionally separate. The
selector has recoverable commit-last records and CRC, while the protected
source supplies the rollback floor. Automatically accepting a blank protected
source on a used device would remove the independent fact needed to distinguish
fresh state from a rollback or storage replacement.

Treating every event as "factory reset" would also broaden one operator action
across unrelated persistence domains. Keeping the routes separate makes the
irreversible scope explicit and prevents an ordinary reset from becoming a
rollback shortcut.

## Consequences

- Factory reset can proceed under identity/configuration policy without map-
  domain erasure.
- Temporary protected-source failure blocks map recovery but does not destroy
  selector evidence.
- Same-device protected-source replacement has no automatic common-firmware
  recovery path.
- A future external recovery design must define authority, audit, physical
  presence, domain binding, target locks, and power-loss behavior before it can
  mutate protected state.
- A host-tested authorization handoff now derives the two domain scopes,
  requires a consumed exact local-USB grant, and mints a move-only preparation
  permit. Only the bounded domain provisioner can consume it, and it burns the
  exact binding/boot/time authority before any I/O.
- The canonical `OTMD/v0` record now defines current/retired domain and selector-
  floor lifecycle data without changing `OTM0/v0`. Its separate abstract two-
  slot store now enforces exact lifecycle/generation successors, commit-last
  readback, prior-good preservation, and fail-closed media selection without an
  erase/reset API. The provisioner commits a pending record before verified
  selector clear or protected-source establishment, supports exact pending
  recovery under a new permit. The stable activation coordinator then commits
  a generation-1 or retired-floor-plus-one selector, atomically advances the
  exact protected domain, marks the domain active, rechecks every durable fact,
  and only then exposes the baseline map. It resumes each committed intermediate
  state without lowering or rebinding protected history. Stable active-domain
  boot now rereads domain, selector, and protected state without mutation before
  exposure. Candidate entry now advances selector, protected, then accepted
  domain generation before trial exposure. Trial boot now recognizes only the
  committed relationship or the exact single-generation interruption gaps,
  persists the boot count or boot-limit fallback privately, advances protected
  then accepted domain history as needed, and publishes only after final
  three-owner agreement. Promotion, fallback completion, cleanup, protected
  target/credential/continuity backends, and physical evidence remain required
  before complete on-device domain operation.
- A future new-device provisioner must establish a fresh domain and cannot
  silently import another device's selector.
- Radio, messaging, alerts, position sharing, OpenGauge integration, and USB
  recovery continue independently while maps remain unavailable.
