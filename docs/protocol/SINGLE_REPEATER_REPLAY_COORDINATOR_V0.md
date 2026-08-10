# Single-Repeater Replay Coordinator v0

Status: algorithm-neutral host recovery boundary, not target storage or
cryptographic evidence, 2026-08-10

## Purpose

`SingleRepeaterReplayCoordinator` composes the immutable single-repeater
forwarder with the existing two-slot duplicate-checkpoint store. It closes one
host-level reboot gap from Decision 0004: a newly accepted replay key must be
saved and readback-verified before the corresponding queued frame can be
released for transmission.

The coordinator deliberately prefers bounded at-most-once forwarding over
replay-driven amplification. It is not a delivery guarantee and it does not
make the volatile forwarding queue durable.

## Boot contract

The owner constructs a fresh duplicate window, forwarder, store, and
coordinator while transport is disabled. Boot becomes operational only when:

1. the expected group-context ID, group epoch, and retention policy are
   nonzero;
2. the target adapter supplies evidence that these exact values select the
   protected storage namespace used by the two checkpoint slots;
3. the live duplicate window is empty;
4. both slots are readable and contain either a unique newest valid checkpoint
   or a genuinely empty first-provisioning state;
5. every restored key matches the expected group epoch; and
6. any known single-slot degradation is repaired with a verified new
   generation before operation.

The store itself also requires its constructor binding and every `ODS0/v1`
record to match the coordinator's expected group and epoch. An empty store is
accepted only with explicit first-provisioning authority. The
coordinator immediately writes generation 1 containing the empty window before
enabling operation. An unreadable slot, invalid-only media, generation
conflict, binding mismatch, legacy unbound v0 record, epoch mismatch, or repair
failure requires service and leaves forwarding disabled. A boot attempt is one-
shot; retry requires a new coordinator instance after the underlying condition
is resolved.

Restore first occurs in a private candidate window. Epoch and checkpoint
validation therefore complete before the live duplicate owner changes.

## Processing and persistence order

For an operational coordinator:

1. the forwarder applies its authentication, authorization, group/epoch,
   permission, monotonic-time, and duplicate checks;
2. an eligible new key is observed before queue/rate admission;
3. queued, queue-full, and rate-limited new observations all report that replay
   state changed;
4. the complete live duplicate window is written to the alternating `ODS0`
   slot and readback-verified; and
5. only after that save succeeds may `next_transmit()` release a queued exact
   frame.

If the save fails or is commit-uncertain, the coordinator disables itself and
will not release even a frame already present in the RAM queue. Congestion
observations are also persisted so repeatedly presenting a valid frame cannot
turn queue/rate pressure into a later amplification opportunity.

## Power-loss consequences

| Interruption point | Conservative result after reboot |
| --- | --- |
| Before a replay key is durably saved | No frame was released; a later valid reception may create a new opportunity |
| During or after an uncertain save | Runtime disables; boot must reconcile the two slots before any forwarding |
| After verified save but before transmit | The volatile queued frame can be lost, while the restored key prevents a second forward |
| After transmit | The already-saved key suppresses the same frame after reboot |

This ordering prevents a reboot from repeating an already authorized forwarding
opportunity. It can sacrifice delivery when power fails between the verified
save and radio transmission. Removing that loss window requires a durable frame
outbox coordinated atomically with replay state; relaxing the save-first order
would weaken the amplification boundary.

## Persistence and protection limits

The current fixed 704-byte `ODS0/v1` record contains replay keys, remaining
lifetimes, a generation, the exact group-context ID and epoch, and CRC-32. Every
inner key must match the outer epoch. CRC detects accidental damage; it is not
authentication or rollback protection. The coordinator still requires external
evidence that the target adapter selected the intended protected namespace.
Structurally valid legacy v0 records are recognized but never imported or
overwritten without a separately authorized reset/migration path.

Every eligible new observation currently causes a full alternating-slot save.
That proves ordering but is not a flash-endurance or latency claim. A target
adapter must measure write amplification, latency, endurance, and power-cut
behavior and may need a protected append journal or coordinated durable outbox
without weakening persist-before-transmit semantics.

## Host evidence

Nine deterministic scenario groups cover:

1. authorized empty-store initialization before operation;
2. service-required refusal of an unprovisioned empty store;
3. persisted duplicate suppression across a fresh-process restart;
4. transmission refusal after a checkpoint save failure;
5. durable queue-congestion observations;
6. repair of one known missing slot before operation;
7. bound checkpoint mismatch and legacy-v0 refusal without live mutation;
8. unreadable-slot refusal; and
9. protected binding, one-shot boot, and clean-live-owner enforcement.

The context-bound store and typed legacy/mismatch extensions pass the complete
28-executable matrix locally and on public `main` in GitHub Actions run
`31374678550`. Both focused store and coordinator suites also pass 100
consecutive local repeats.

## Remaining gates

- exact ESP32 target and protected two-slot namespace binding;
- authenticated integrity and a trusted rollback floor;
- protected target namespace proof for the host-tested context-bound format;
- physical power interruption at every storage mutation boundary;
- measured write latency, wear, and endurance under expected group traffic;
- target radio scheduling and a durable outbox if the product requires recovery
  of saved-but-not-yet-transmitted frames; and
- authenticated packet adapter, direct SX1262 binding, and four/eight-client
  field evidence.
