# OT-212 host deadline correction — 2026-09-13

The host now preserves an earlier valid receipt when its deadline is reached
before another raw read begins. Both pre-read deadline checks return an empty
read to the unchanged capture loop, which completes its full observation horizon.
An empty window still times out. Admission failures, invalid clocks, late returned
bytes, malformed/oversized receipts, trailing data and one-use closure still refuse.

The [OT-211 trial](OT-211-INVITATION-TRIAL-2026-09-12.md) exposed this race after a
matching receipt was observed. It remains a failed physical trial; these software
results do not retroactively accept it. The exact historical crossing is unknown.

## Validation

- 335 tests across 18 suites, no skips; focused deadline cases cover both crossings.
- Actual isolated Python/PowerShell probes pass in ordinary and hostile environments.
- One fresh runtime contains 3563 pinned files; manifest SHA-256
  `9e0eedc9b0c421862253d993f9d6b26450344d34128c73790f53a8eaf87d0e1c`.
- Synthetic package admission passes normal and candidate-independent recovery paths.
- The unchanged firmware is 445248 bytes, SHA-256
  `4526209643bbfb51ccf95d04a992eed41c877dd72cc0b03f415783a63d9036d2`. No firmware build or hardware action was needed.

The [machine-readable evidence](../../tests/benchmarks/crypto/OT-212-DEADLINE-OPERATOR-2026-09-13.json) contains exact source, test and
runtime hashes, suite counts, actions and scope. Exact commands, logs and the
runtime manifest are retained privately in `.private/ot212-deadline`.

## Operator boundary

Use `tools/Invoke-SecurityPolicyDeadlineOperator.ps1` with its exact new manifest.
The successor composes the corrected endpoint with the unchanged receipt boundary,
diagnostics, original-custody and restoration checks. OT-210 sources and frozen
runtime remain unchanged. The firmware, receipt grammar and full 30-second horizon
are unchanged. Synthetic packages are not live custody or physical authorization.

Next: Obtain fresh exact-image authority and original custody for one A-first confirmation through the corrected OT-212 operator. Require a strict pass and independent original restoration/readback/restart before B; no automatic retry.
Product trust provisioning/confirmation, two-node join/rekey/reset, physical entropy
and interruption gates remain open. No V1 score or public website status changed.
Implementation is local/uncommitted; publication requires separate scope.
