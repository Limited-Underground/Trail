# Decision 0087: record the OT-150 abort and correct the mbedTLS/PSA successor

- Date: 2026-08-27
- Status: Accepted
- Scope: OT-005 / OT-151 mbedTLS/PSA execution abort and host-only correction

## Decision

Accept the sanitized record of the single OT-150-authorized, application-only
attempt and a separate host-only corrected successor. The non-consuming
preflight verified both exact installed Trail applications, reset both nodes,
and received the owner's visual confirmation that both displays returned to the
Trail logo.

The authorized execution reached Node A benchmark readback. Capture then failed
closed as `capture_failed` / `frame_count_incomplete`: 1,468 bytes contained
exact `READY` plus five complete frame-prefixed records, after which the host
waited to the fixed deadline. Node A restored, read back, and reset exactly to
Trail; Node B was never benchmark-written and remained on Trail. The private
terminal state records `restoration_complete=true`, recovery was unnecessary,
and the owner confirmed both Trail logos after the abort.

Host-only source and frozen-ELF inspection account for every observed byte and
identify a deterministic benchmark-fixture error. The primitive-vector check
correctly required an all-zero X25519 peer to return
`PSA_ERROR_INVALID_ARGUMENT`, but it incorrectly also required
`output_length == 0`. The exact pinned TF-PSA implementation deliberately
randomizes the output buffer and reports the full output size after that error.
The frozen ELF proves the invalid status check passed and the nonzero-length
check alone selected the vector-failure branch.

The accepted successor therefore preserves every frozen OT-150 input and
changes only the nonportable output-length assertion in a separate target. It
continues to require `PSA_ERROR_INVALID_ARGUMENT` and continues to zeroize the
failure buffer. A separate successor runner recognizes only a canonical early
terminal failure transcript and reports it immediately with a privacy-safe
failure code; the exact 1,015-frame successful transcript, parser, deadline,
START/READY rules, and all result-admission requirements remain unchanged.

## Consequences

- The OT-150 authority is consumed by this abort, grants no retry or
  continuation, and is not reusable.
- No mbedTLS/PSA benchmark result, timing, primitive result, runtime-resource
  result, candidate selection, Phase 2 completion, radio evidence, support,
  regulatory acceptance, production readiness, end-to-end proof, or score
  credit is admitted.
- The latest admitted candidate benchmark remains OT-146 Monocypher; the latest
  admitted eight-operation benchmark remains OT-122 libsodium.
- No further mbedTLS/PSA hardware attempt is authorized. Any later device access
  requires a newly frozen executable binding and fresh explicit non-reusable
  one-attempt authority.
- V1 remains exact 43.75%, displayed 44%; the historical baseline remains exact
  31.75%, displayed 32%.
- No public website update is required. This is a contained internal benchmark
  correction with no percentage, demonstrated product capability,
  field-test-readiness, support, or release-state change.

## Evidence

- [OT-151 execution and correction evidence](../../tests/hardware/OT-151-2026-08-27.md)
- [OT-150 one-attempt authority](../../tests/benchmarks/crypto/OT-150-OT005-MBEDTLS-PSA-ONE-ATTEMPT-AUTHORITY-V0.json)
- [Sanitized abort receipt](../../tests/benchmarks/crypto/OT-151-OT005-MBEDTLS-PSA-EXECUTION-ABORT-RECEIPT-V0.json)
- Frozen coordinator: `tools/ot150_mbedtls_psa_coordinator.py`
- Frozen runner: `tools/ot150_mbedtls_psa_protocol_runner.py`
- Frozen hardware adapter: `tools/ot150_mbedtls_psa_hardware_adapter.py`
