# Offline Map Selector Trusted-Generation Source v0

Status: backend-neutral enforcement boundary with deterministic host tests,
2026-08-11. No protected target backend, ESP-IDF composition, hardware-backed
counter, encrypted storage, reset authority, or physical durability evidence
exists.

## Purpose

The generation inside an `OTM0/v0` selector checkpoint is ordinary media data.
Its CRC detects accidental corruption but cannot prove that the record is the
newest state ever accepted by the device. A rollback defense therefore needs a
minimum accepted generation that is owned independently from both selector
slots.

This component defines the common-code boundary around that future protected
source. It does not decide whether a target uses a hardware monotonic counter,
a protected service, encrypted storage with an authenticated policy, or another
mechanism. Ordinary selector NVS does not become protected merely by being
passed through this interface.

## Source contract

`MapSelectorTrustedGenerationSource` exposes only two operations:

1. `read()` returns the current 64-bit generation or one typed source error.
   Generation zero is valid and means no selector generation has yet been
   trusted.
2. `compare_and_advance(expected, requested)` must atomically confirm that the
   protected value still equals `expected` and durably advance it to the
   strictly greater `requested` value.

The backend must serialize this operation against every other writer. A stale
expected value must not overwrite a newer value. Success is not accepted by
common code until a second read returns exactly the requested generation.

The source error vocabulary distinguishes uninitialized, temporarily not
ready, I/O failure, invalid state, policy rejection, and generation conflict.
Unknown enum values are contained as `invalid_state`.

## Common fail-closed behavior

`MapSelectorTrustedGeneration` is a boot-local, non-copyable and non-movable
enforcer around the injected source:

- a failed read before any write remains typed and retryable;
- an observed value below the caller's exact expected value is a source
  rollback;
- an observed value above the exact expected value is a generation conflict;
- equal or decreasing advance requests are rejected before backend mutation;
- the backend receives the exact expected and requested values;
- success requires exact post-advance readback; and
- any advance error, post-advance read error, or non-exact readback latches the
  object into `reconciliation_required`.

Once latched, the object performs no more backend reads or writes. There is no
ordinary reset method. Target composition must keep selector/map exposure
disabled and reconcile both selector storage and the protected source through
a fresh boot instance. This is intentionally conservative because a backend
may have committed an advance before reporting an I/O error.

## Integration boundary

The selector store, candidate, transition, first-baseline, and service-reseed
coordinators retain caller-supplied generation values as independently testable
state-machine inputs. Those scalars are not anti-rollback protection. The
separate
[trusted boot coordinator](OFFLINE_MAP_SELECTOR_TRUSTED_BOOT_COORDINATOR_V0.md)
now owns this source for the boot path, derives the scalar internally, and
rechecks or advances it before selector publication. The
[trusted transition coordinator](OFFLINE_MAP_SELECTOR_TRUSTED_TRANSITION_COORDINATOR_V0.md)
does the same for runtime trial, fallback, and cleanup operations. The
[trusted candidate coordinator](OFFLINE_MAP_SELECTOR_TRUSTED_CANDIDATE_COORDINATOR_V0.md)
owns replacement ordering. The
[protected first-baseline coordinator](OFFLINE_MAP_SELECTOR_TRUSTED_BASELINE_COORDINATOR_V0.md)
owns clean initial-install ordering. Service reseed still needs its own
protected-source composition.

The service-reseed authorization permit is also separate. Authorization to
erase and reseed two selector records is not authorization to lower or reset
the protected generation. Protected-source initialization, reset, device
replacement, and recovery require a separately designed and authorized
workflow.

## Evidence and limitations

Ten deterministic scenario groups cover zero/nonzero inspection, retryable
pre-write read failure, unknown error containment, rollback/conflict and
nonincreasing-request refusal, exact compare/advance/readback, every typed
advance failure including applied-then-failed behavior, failed post-write
readback, frozen and advanced-past readback, the no-more-I/O latch, and fresh-
boot reconciliation.

The source suite and its trusted boot/runtime/candidate/baseline compositions
pass under strict C++17 warnings-as-errors in the complete 73-executable
OpenTrail host matrix. This proves only common interface, state-machine,
boot-ordering, and runtime/replacement-ordering behavior. It does not prove
secrecy, authenticity,
monotonicity, flash-replacement resistance, power-loss durability, wear,
factory-reset policy, physical attack resistance, or an ESP32 implementation.
