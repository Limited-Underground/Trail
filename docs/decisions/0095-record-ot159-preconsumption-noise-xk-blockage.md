# Decision 0095: record the OT-159 pre-consumption Noise XK blockage

- **Status:** Accepted
- **Date:** 2026-08-28
- **Scope:** OT-005 / OT-159 pre-consumption validation of the sole OT-158-authorized reset-aware Noise XK radio attempt

## Context

Two matching anonymous nodes were present with antennas connected. The exact
OT-158 authority, OT-157 preparation, benchmark application, and Trail
restoration application validated before the official bound preflight. Direct
application readback independently proved the exact Trail application on both
nodes, and both hard resets completed.

The official OT-157-bound preflight nevertheless failed on both of two attempts before
the coordinator created its exclusive one-use journal. No benchmark write,
radio operation, execution receipt, or recovery receipt occurred. The OT-157
journal, execution-receipt, and recovery-receipt paths were absent before and
after the blockage.

Host-only source diagnosis identifies a deterministic composition defect. The
inherited OT-153 adapter readback path calls `coordinator._sha256(readback)`
with bytes. OT-157 replaces that adapter's coordinator module, but its public
`_sha256` name is a file-hash helper that accepts a `Path` and calls
`path.read_bytes()`. The successor therefore shadows the inherited byte-hash
helper exactly where application readback is verified. This diagnosis is about
the accepted host composition; it does not identify a device, USB, firmware,
or physical-radio defect.

## Decision

1. Record OT-159 as a pre-consumption blockage with public schema `OT159NXB0`.
2. Preserve the exact OT-157 executable/restoration bundle and OT-158 authority
   as immutable history. The authority remains unused and unconsumed, but the
   accepted composition is non-executable and cannot be silently repurposed for
   a corrected successor.
3. Admit only the two direct exact-Trail application readbacks and two completed
   resets. Admit no benchmark write, radio operation, recovery operation,
   execution result, transmission count, timing result, or packet observation.
4. Require OT-160 as a minimal host-only successor correction that separates
   the file-hash and byte-hash helpers while preserving every accepted
   authority, artifact, privacy, preflight, journal, restoration, and radio
   contract boundary.
5. Require later exact successor-bundle and fresh-authority gates before any
   corrected hardware execution; OT-160 itself grants no hardware authority.

## Alternatives rejected

- Treating direct readback as a successful official preflight: rejected because
  the exact accepted composition did not pass its bound preflight.
- Marking the OT-158 authority consumed: rejected because the exclusive journal
  was never created and no first benchmark write occurred.
- Reusing OT-158 against modified host code: rejected because the authority
  binds the immutable OT-157 bytes.
- Attributing the blockage to either node or the radio: rejected because the
  deterministic host composition fails before benchmark write or radio entry.

## Consequences

OT-159 advances no benchmark, radio, Phase 2, selection, readiness, support,
release, regulatory, or score state. V1 remains exact 43.75% and displayed 44%;
the historical baseline remains exact 31.75% and displayed 32%. This contained
internal pre-consumption blockage requires no public website status update.

## Evidence

- [OT-158 authority decision](0094-accept-ot158-noise-xk-radio-one-attempt-authority.md)
- [OT-158 authority](../../tests/benchmarks/crypto/OT-158-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json)
- [OT-159 evidence](../../tests/hardware/OT-159-2026-08-28.md)
- [Sanitized blockage record](../../tests/benchmarks/crypto/OT-159-OT005-LIBSODIUM-NOISE-XK-RADIO-PRECONSUMPTION-BLOCKAGE-V0.json)
