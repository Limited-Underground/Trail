# Decision 0059: grant one-time Phase 2 benchmark execution authority

- Status: Accepted; execution partially exercised
- Date: 2026-08-23
- Scope: OT-005 benchmark Phase 2 only

## Decision

Accept the strict host-only `OTCBXA1/v1` authority artifact for one execution of
Phase 2 in the immutable OT-116 `OTCBX1/v1` benchmark procedure. The authority
binds the exact OT-116 plan, the OT-119 two-node exact-profile admission, and
the OT-120 atomic retained candidate import/build admission by both raw and
canonical SHA-256. Phase 0 and Phase 1 are complete at counts `3/3/3`.

The owner explicitly resumed this work after being told that Phase 2 would
temporarily flash and access both test devices. That fresh instruction grants
only the bounded actions needed to build and execute the exact two-node target
comparison: computer builds, device access, temporary benchmark-firmware flash
and verified restoration, test-only key/entropy operations, and the measurements
frozen by OT-116.

US915 close-bench transmission at the exact 2 dBm command setpoint is
conditional. Both nodes must pass an affirmative attached-antenna preflight
before any transmission; otherwise radio work fails closed and does not run.

This artifact grants authority but records no execution result. It is consumed
when Phase 2 completes or aborts and creates no continuing authority. Phase 3
independent result admission remains separately required.

## Execution checkpoint

[Decision 0060](0060-bounded-libsodium-local-primitives-checkpoint.md) records
the privacy-safe two-node libsodium local-primitives checkpoint produced under
this authority. Both nodes passed 7/7 operations and were restored exactly, but
the receipt explicitly records `phase_two_complete=false` and `radio_used=false`.
The immutable authority artifact is unchanged; its one bounded Phase 2 session
is only partially exercised, not completed or admitted.

## Immutable parents

- OT-116 `OTCBX1/v1` plan raw/canonical SHA-256 `0280570b74b2b505b5a92e7834b24136caa0956ff1c536d7638bff0a8b105f2a` / `7844d7be1824784a0ff58ba58df25bda85c6059c445f217a4d520215bfff50a8`.
- OT-119 `OTRTPA1/v1` admission raw/canonical SHA-256 `afd3d8b17f80c49560f9fad71e93703ef6d142ee538146fc5829b2a0799d0e36` / `0eff2d934891f36999bdafb2a14ffc755b258c19bccb96a5a8d96db06105a443`.
- OT-120 `OTCIBA1/v1` admission raw/canonical SHA-256 `90af31966553bee58fcf71e4decfee8d2bcadfee58ef026e3f96cffcd6f45ccf` / `0c55f49803d833c075670b17fa8d033bd5a7cd4997e8714ff247161f7fa2057b`.
- OT-121 `OTCBXA1/v1` authority raw/canonical SHA-256 `765aacd8a33862265b46da2d60333759cd96b72a8acae9e70b22c5bda2dbd90f` / `a2e9bbea78282c3a0451654f39c0be49c875217933ef02b7bc384860f32f3105`.

## Authorized actions

- Build the exact retained benchmark candidates.
- Access OT-DEV-001 and OT-DEV-002 for the exact Phase 2 measurement.
- Temporarily flash benchmark-only firmware, verify the write, and restore and
  verify the pre-execution OpenTrail image after completion or abort.
- Perform only the test key and entropy operations required by the frozen
  benchmark gates.
- Run the exact two-node measurements, including conditional US915 2 dBm
  close-bench radio measurements after both antennas pass preflight.

## Withheld authority

No candidate, library, cryptographic suite, handshake/KDF, or packet-v1 wire
format is selected. Secure-LoRa implementation, supported-target or
compatibility declarations, regulatory acceptance, production use, field or
range claims, Phase 3 result admission, and score credit remain unauthorized.
The authority artifact itself performs no build, device access, flash, radio,
key/entropy operation, or benchmark and adds no physical evidence.
