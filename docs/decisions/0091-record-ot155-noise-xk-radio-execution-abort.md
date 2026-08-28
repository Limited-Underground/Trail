# Decision 0091: record the OT-155 Noise XK radio execution abort

- **Status:** Accepted
- **Date:** 2026-08-27
- **Scope:** OT-005 / OT-155 sole OT-154-authorized two-node Noise XK radio-cost attempt

## Context

Decision 0090 and the exact `OT154NXRA0` artifact authorized one non-reusable
two-node attempt against the immutable OT-153 executable/restoration bundle.
The attempt passed the exact installed-Trail preflight on both anonymous nodes,
then both nodes passed benchmark application readback and hard reset before the
radio runner was invoked.

The execution terminated fail closed as `radio_run_failed`. The private
coordinator evidence proves that the radio path was invoked but that no radio
result passed validation. It also proves that both nodes subsequently passed
exact Trail restoration readback and hard reset. Restoration is complete and no
recovery receipt exists or is required.

The frozen OT-153 error boundary intentionally collapses endpoint-open, runner,
and result-validation exceptions into the same safe failure code. The retained
evidence therefore does not identify the exact failing substage or establish a
physical, firmware, host, USB, or radio root cause.

## Decision

1. Record OT-155 as a consumed, exactly restored execution abort with public
   schema `OT155NXAR0`.
2. Preserve every OT-153 and OT-154 source, executable, restoration, and
   authority byte as immutable history.
3. Admit no radio result, frame or transmission count, timing result, packet
   loss observation, Phase 2 completion, candidate/library/suite/handshake/KDF/
   Packet-v1 selection, readiness, support, regulatory, or score claim.
4. Treat the OT-154 authority as consumed by this abort. It is non-reusable,
   grants no continuation, and cannot authorize a retry.
5. Require a separate host-only diagnostic and correction task before another
   executable bundle or one-attempt authority may be proposed.
6. Keep private endpoints, ports, identifiers, paths, raw serial, backend text,
   and private journal/receipt content outside the public record.

## Alternatives rejected

- Reusing the OT-154 authority after complete restoration: rejected because the
  accepted authority is consumed on success or abort.
- Inferring an exact root cause from `radio_run_failed`: rejected because the
  frozen diagnostic boundary does not preserve an exact safe substage.
- Admitting the planned transmission count or theoretical timing as an observed
  result: rejected because no radio result passed validation.
- Weakening restoration, authority, or privacy gates to simplify a retry:
  rejected because those gates worked as intended.

## Consequences

OT-155 closes the one authorized execution boundary safely, but advances no
measurement or release state. V1 remains exact 43.75% and displayed 44%; the
historical baseline remains exact 31.75% and displayed 32%. This internal
execution-abort record does not require a public website status update.

## Evidence

- [OT-154 authority decision](0090-accept-ot154-noise-xk-radio-one-attempt-authority.md)
- [OT-154 authority](../../tests/benchmarks/crypto/OT-154-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json)
- [OT-155 execution-abort evidence](../../tests/hardware/OT-155-2026-08-27.md)
- [Sanitized abort receipt](../../tests/benchmarks/crypto/OT-155-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTION-ABORT-RECEIPT-V0.json)
