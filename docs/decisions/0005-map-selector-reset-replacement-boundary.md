# Decision 0005: Map Selector Reset and Replacement Boundary

Status: accepted architecture direction; target authority and implementation
pending, 2026-08-11

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
  permit. The permit deliberately has no consumer until domain/retirement
  records and a target provisioner satisfy the remaining requirements here.
- A future new-device provisioner must establish a fresh domain and cannot
  silently import another device's selector.
- Radio, messaging, alerts, position sharing, OpenGauge integration, and USB
  recovery continue independently while maps remain unavailable.
