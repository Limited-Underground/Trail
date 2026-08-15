# Decision 0009: One Phone Companion, Device-Authoritative State

Status: accepted architecture direction with host-tested envelope/session guard;
target and Android evidence pending, 2026-08-14

## Decision

One Android phone may hold the active companion session for one screenless LoRa
client. The device remains authoritative for security, group state, radio work,
queues, message and acknowledgement identity, delivery outcomes, GNSS validity,
position-sharing policy, peer position freshness, and durable history. The
Android application is a renderer and intent client, not the source of those
facts.

The production companion boundary uses the brand-neutral, versioned
[BLE Companion GATT v0](../platform/BLE_COMPANION_GATT_V0.md). It does not reuse
the simulator's unauthenticated `OTS0` helper or serialize the embedded
`UiFrame`. Customer-facing working names remain outside UUIDs, protocol fields,
persistent schemas, cryptographic domains, board identifiers, and device IDs.

An active session requires an encrypted link, authenticated bond, separate
application authorization, one private controller binding, and one strictly
increasing device-assigned boot-local session nonce. A second phone cannot
become active while that lease exists. Session nonces and request IDs never
wrap; exhaustion fails closed. Exact duplicate request IDs are detected so an
action is not applied twice; stale, wrong-controller, wrong-session, server-
direction, and fragmented v0 actions fail closed.

Disconnecting the phone closes only presentation/control access. It does not
stop device radio service, discard queues, change group security, or implicitly
disable/enable location policy. Reconnection begins from a fresh authoritative
snapshot and device-owned cursors.

## Why

This keeps Essential useful as a real LoRa device rather than turning the phone
into the network authority. It also preserves the same lower application,
security, message, alert, and location behavior for the later self-contained
touchscreen target while allowing Android and touchscreen rendering to remain
separate adapters with separate evidence.

One active controller also answers the owner's concern that optional Bluetooth
must not leave an uncontrolled second command path. BLE transport security
alone is not enough: a screenless target still needs an explicit owner-
authorization and revocation workflow before a bond can satisfy this decision.

## Consequences

- Android must render explicit disconnected, reconnecting, stale, rejected,
  locally accepted, queued, transmitted, delivered, and acknowledged states
  without collapsing them.
- Android background execution, permissions, accessibility, local caching,
  notification behavior, signing, and distribution are platform gates, not
  firmware evidence.
- Phone caches are replaceable projections. They cannot override device queue,
  history, security, or location truth.
- Gold and Platinum need no BLE controller for ordinary operation. Their local
  touchscreen adapter can consume shared semantic owners directly; USB remains
  maintenance/recovery unless another explicit decision changes that boundary.
- Maps follow the position/state contract and a licensed offline-map decision;
  they do not block the first BLE status/action slice and never travel over
  LoRa.
- The current standalone four-unit V1 definition and V1 percentage do not
  change from this host-only foundation.

## Remaining gates

- select and validate the screenless device's BLE stack, secure pairing/OOB
  method, physical authorization control, bond storage, revocation, and lost-
  phone recovery;
- bind a target-owned monotonic boot-local session allocator with explicit
  exhaustion, then add bounded result caching/reassembly;
- define and host-test exact snapshot, action, result, message, alert, and
  position payload schemas;
- implement Android scan/selection, security, MTU negotiation, subscribe,
  reconnect/resync, and stale-state behavior;
- bind the same semantic owners to authenticated packet-v1 and the actual radio,
  queue, persistence, and GNSS target; and
- validate two phones with two devices over real LoRa before any compatibility
  or production claim.
