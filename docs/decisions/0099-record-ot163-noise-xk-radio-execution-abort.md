# Decision 0099: record the OT-163 Noise XK radio execution abort

- **Status:** Accepted
- **Date:** 2026-08-28
- **Scope:** OT-005 / OT-163 sole OT-162-authorized two-node Noise XK radio-cost attempt

## Context

Decision 0098 and the exact `OT162NXRA0` artifact authorized one non-reusable
two-node attempt against the immutable OT-161 corrected executable/restoration
bundle. Both anonymous nodes passed exact installed-Trail readback and hard
reset before the consumption boundary, and the owner confirmed both Trail
logos.

The authority was consumed exactly once. Both nodes then passed benchmark
application readback and hard reset before the frozen runner was entered. The
runner terminated fail closed as `radio_run_failed` at the allowlisted stage
`restart_ack_a`. No benchmark or radio result passed validation. Both nodes
subsequently passed exact Trail restoration readback and hard reset;
restoration is complete, recovery is not required, no recovery receipt exists,
and the owner confirmed both Trail logos after the abort.

The safe stage identifies only the incomplete restart acknowledgement for
anonymous role A. It does not preserve or establish an endpoint, host, USB,
firmware, physical-radio, or other root cause.

## Decision

1. Record OT-163 as a consumed, exactly restored execution abort with public
   schema `OT163NXAR0` and allowlisted stage `restart_ack_a`.
2. Preserve every OT-160 through OT-162 source, executable, restoration,
   preparation, authority, decision, and evidence byte as immutable history.
3. Admit no benchmark or radio result, transmission or frame count, timing,
   loss/duplicate/corruption observation, candidate/library/suite/handshake/
   KDF/Packet-v1 selection, Phase 2 or Phase 3 completion, readiness, support,
   production, regulatory, or score claim.
4. Treat the OT-162 authority as consumed by this abort. It is non-reusable,
   grants no continuation, and cannot authorize a retry.
5. Record exact restoration for both anonymous nodes and the owner's visual
   confirmation of both Trail logos. No recovery operation or receipt is
   required.
6. Keep private endpoints, ports, identifiers, paths, raw payload or serial
   data, tokens, keys, exceptions, backend text, and private fields outside the
   public record.
7. Require any later hardware attempt to use a separately accepted immutable
   successor bundle and fresh explicit non-reusable authority.

## Alternatives rejected

- Reusing the OT-162 authority after complete restoration: rejected because
  the accepted authority is consumed on success or abort.
- Inferring a device, USB, firmware, or physical-radio root cause from
  `restart_ack_a`: rejected because the safe stage does not prove one.
- Admitting planned transmissions, theoretical airtime, or partial execution
  as an observed result: rejected because no result passed validation.
- Publishing private execution content to diagnose the abort: rejected because
  its immutable public anchors and safe stage are sufficient for this record.

## Consequences

OT-163 closes the one authorized execution boundary safely but advances no
measurement or release state. V1 remains exact 43.75% and displayed 44%; the
historical baseline remains exact 31.75% and displayed 32%. This internal
execution-abort record does not require a public website status update.

## Evidence

- [OT-162 authority decision](0098-accept-ot162-noise-xk-radio-one-attempt-authority.md)
- [OT-162 authority](../../tests/benchmarks/crypto/OT-162-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json)
- [OT-163 execution-abort evidence](../../tests/hardware/OT-163-2026-08-28.md)
- [Sanitized abort receipt](../../tests/benchmarks/crypto/OT-163-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTION-ABORT-RECEIPT-V0.json)
