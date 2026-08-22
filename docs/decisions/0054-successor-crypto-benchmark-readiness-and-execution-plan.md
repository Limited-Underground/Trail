# Decision 0054: Freeze the successor crypto-benchmark readiness review and phased execution plan

- Status: Accepted host-only contract; OT-094 requirements closed, benchmark measurement blocked
- Date: 2026-08-22
- Work item: OT-116
- Scope: Successor readiness semantics and an immutable, fail-closed OT-005 execution procedure

## Context

OT-114 closes the last of the six requirements recorded by the historical
`OTCBR0/v0` ledger. That fact does not make the benchmark measurement-ready.
Accepted candidate evidence remains `3/1/0` for source, API/configuration, and
import admissions. The only admitted API/configuration evidence is the
five-of-eight mbedTLS/PSA comparison, which is structurally nonselectable.
Libsodium and Monocypher still lack candidate-specific API/configuration
admission, every candidate lacks an accepted import anchor, and the second
measurement node lacks exact-profile admission.

The historical `OTCB0/v0` plan also assumes one common sdkconfig. OT-107 and
OT-108 supersede that assumption with exact per-candidate configurations and
permit partial operation coverage only for a nonselectable comparison.
Historical artifacts remain unchanged.

## Decision

Accept the append-only `OTCBR1/v0` successor review and the immutable
`OTCBX1/v1` phased execution contract produced by OT-116.

`OTCBR1/v0` records both facts without conflating them:

- all six OT-094 closure requirements have independently accepted evidence;
- benchmark measurement remains blocked pending candidate-specific preflight
  admissions and separate execution authority.

`OTCBX1/v1` freezes a fail-closed procedure in three phases:

1. admit candidate-specific API/configuration evidence;
2. admit retained candidate import/build evidence;
3. only after those gates and fresh separate authority, perform exact-target
   cold/warm measurement and radio-cost collection.

The candidate order remains Espressif libsodium primary, pinned ESP-IDF
mbedTLS/PSA comparison, then Monocypher comparison. A passing primary result
requires complete eight-operation coverage. The current mbedTLS/PSA evidence
permits measurement of only its five admitted operations and can never be
converted into selection evidence. Monocypher remains unmeasurable until its
own API/configuration evidence is admitted.

The plan binds the exact received target, toolchain, per-candidate sdkconfig and
source anchors, the accepted US915 close-bench radio profile, 163-byte protocol
test length, 255-byte direct ceiling, fixed operation and gate order, at least
100 cold and 100 warm repetitions, deterministic results, recovery/restart,
privacy, and raw-evidence custody.

## Authority and claims

OT-116 is host-only. It imports or compiles no candidate, builds or executes no
benchmark, accesses or flashes no device, transmits no radio packet, and
performs no key or entropy operation. It selects no candidate, suite,
handshake/KDF, secure-LoRa composition, or Packet V1 wire format. It adds no
support, compatibility, legal, regulatory, range, physical, continuing-
authority, or score claim.

The accepted plan describes future actions but grants no authority to perform
them. A later action must satisfy the plan's preflight gates and receive fresh
authority for its actual scope.

## Consequences

- The six-requirement OT-094 history is preserved and now has an append-only
  successor review.
- The historical `OTCB0/v0` plan remains `draft_blocked` and unmodified.
- The exact benchmark procedure is frozen, but measurement is not admitted.
- V1 remains exact 43.75% / displayed 44%; the historical standalone baseline
  remains exact 31.75% / displayed 32%; Android remains 60%; V1.5 and V2 remain
  unmeasured.

## Next gate

Produce and independently admit complete libsodium and appropriate Monocypher
candidate-specific API/configuration evidence, admit the second measurement node's exact profile, then produce accepted retained
import/build evidence for each included executable candidate. Only after those
preflights and fresh execution authority may exact-target measurement begin.
Any cryptographic selection remains a later explicit decision.
