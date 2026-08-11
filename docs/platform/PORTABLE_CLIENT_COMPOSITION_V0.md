# Portable Client Composition Preflight v0

Status: host-tested structural boundary, 2026-08-10. This is not an ESP-IDF
target, board support package, boot task, rendered interface, or supported-
hardware declaration.

## Purpose

`PortableClientComposition` defines the minimum hardware-facing shape of the
first self-contained OpenTrail client before one exact board is frozen. A board
target supplies non-owning adapter bindings plus explicit product policy. The
preflight aggregates every structural mismatch into one result and refuses to
describe an incomplete composition as ready.

Structural readiness does not mean a radio is online, GPS has a fix, entropy is
ready, storage is durable, a display is readable, or firmware can be flashed.
Those are separate boot, target-build, physical, and field gates.

## Required bindings

| Binding | Existing contract | Why it is required |
| --- | --- | --- |
| Opaque radio | `RadioTransport` | Provides bounded MTU and cooperative frame transport without protocol coupling |
| GPS | `GpsProvider` | Keeps no-fix operation valid while allowing the pilot hardware to be self-contained |
| Diagnostics | `LogSink` | Gives the application one bounded, privacy-aware diagnostic destination |
| Small protocol/configuration storage | `PersistentStorage` | Supplies two 64-byte slots in separated configuration, secret, protocol, and outbound-counter domains |
| Replay-checkpoint storage | `DuplicateCheckpointStorage` | Supplies two full 704-byte `ODS0` slots; it cannot be substituted by the 64-byte storage surface |
| Secure randomness | `SecureRandomSource` | Preserves explicit not-ready/ready/failed entropy semantics without fallback |
| Monotonic time | `MonotonicCounterSource` | Feeds one checked boot-local clock that application code must sample once per cooperative cycle |
| Power state | `PowerStatusSource` | Supplies one atomic normalized power observation without chemistry guesses |
| Display | `DisplaySink` | Presents semantic frames while the target owns pixels, labels, and rendering |
| Local input | `LocalInputSource` | Returns revision-bound action slots while the target owns buttons, touch, and coordinates |

A concrete target may place both storage interfaces over one physical backend,
but it must preserve their different record sizes, namespaces, atomicity, and
failure semantics. Treating them as the same 64-byte slot contract would leave
replay recovery structurally unbound.

## Explicit policy

The caller supplies:

- a nonzero required radio MTU no larger than the transport ceiling;
- distinct critical and low battery percentages plus nonzero freshness;
- fixed display capabilities;
- a nonzero minimum action-slot count; and
- whether the target must support the canonical held critical confirmation.

Zero/default policy is invalid. A critical-confirmation target requires at
least two action slots and hold support. Button-only and touch-only targets can
both pass when their declared capabilities meet the same semantic contract.

## Non-invasive review

`review()`:

1. records every missing binding;
2. validates radio, power, display, and local-action policy;
3. reads only the radio's advertised MTU;
4. compares that MTU to the explicit requirement; and
5. returns a fixed 32-bit issue mask plus the observed MTU.

It does not read or write storage, request entropy, sample the clock, query GPS
or power, write a log, present a frame, poll input, inspect radio status, send,
receive, or service a driver. Repeating the review is deterministic except for
the adapter-owned MTU capability read.

An entropy source that is temporarily not ready and a GPS provider with no fix
remain structurally valid. Runtime code must surface those states without
turning them into missing hardware or silently weakening the feature.

## Whole-contract review findings

The 2026-08-10 review enumerated every abstract target-facing interface under
`firmware/components`.

- The first draft omitted `DuplicateCheckpointStorage`. The final boundary now
  binds it separately from `PersistentStorage`; the test suite rejects its
  absence.
- Power and display capability rules were previously private implementation
  details. They are now exposed as pure validators and reused by both their
  owning components and composition, preventing duplicate interpretations.
- The remaining target-facing interfaces have compatible non-owning lifetimes
  and do not require preflight I/O.
- OT-003F's [outbound service coordinator](OUTBOUND_SERVICE_COORDINATOR_V0.md)
  now owns one `CheckedMonotonicClock` sample across location, scheduling,
  priority handoff, delivery, and radio service. Full boot, persistence,
  inbound, UI, power, logging, and target-task composition remains.
- UI failure must remain independent of radio servicing; GPS no-fix must not
  stop messaging; entropy not-ready must gate cryptographic work; and storage
  failure must not be hidden by default configuration.

No concrete pin map, task priority, NVS partition, protected-key backend,
display driver, GPS driver, radio driver, or charger threshold was selected by
this review.

## Host evidence

`tests/host/portable_client_composition_tests.cpp` covers eight groups:

1. a complete button target;
2. an equivalent touch target with not-ready runtime services;
3. aggregation of all missing bindings;
4. invalid, oversized, and insufficient radio MTU cases;
5. shared power-policy validation without a power read;
6. shared display-capability validation without display/input I/O;
7. separate local-action, slot-capacity, and hold requirements; and
8. deterministic repeated review with no mutating adapter call.

The suite is part of the complete host matrix. Passing it proves only that a
future board target cannot omit or misdeclare these structural dependencies
without a typed issue. It does not prove that any adapter exists or works on
the Heltec V4, Wio Tracker L1 Pro, SenseCAP repeater, or another board.

## Concrete target gate

For one exact client model, the next target step must:

1. implement all ten bindings with documented SDK/toolchain/partition details;
2. run this structural review before enabling application transport;
3. construct the checked clock, power evaluator, local interface, and higher
   components under explicit ownership;
4. define nonblocking boot and cooperative service ordering;
5. build the actual ESP-IDF target with warnings treated as errors; and
6. collect recovery, power, readability/input, GNSS, radio, and privacy-safe
   physical evidence without converting a host pass into a hardware claim.
