# OT-163 Security evaluation composition and matched resources

## Accepted boundary

The additive `heltec_v4_security_eval` target composes guarded ESP32 entropy,
real Noise XK, authenticated peer-pin checking, NVS counter reservation and
bidirectional local authenticated records. Two fresh ESP-IDF 6.0.2 builds pass.
This is a same-chip two-role evaluation. It does not start BLE advertising or
LoRa, accept invitations, or implement product phone-to-phone messaging.
No device was flashed or physically tested in this batch.

See the [exact build evidence](../../tests/benchmarks/crypto/OT-163-SECURITY-EVALUATION-BUILD-2026-09-10.json)
and [target boundary and operating constraints](../../firmware/targets/heltec_v4_security_eval/README.md).

## Source and host evidence

- The actual target Noise adapter passes 26 independent real-primitive vector
  groups, 19 composed session groups and five direct size-refusal groups.
  Its additive correction bounds size_t arithmetic before conversion or output
  mutation; frozen benchmark predecessors remain unchanged.
- The actual entropy runtime passes 15 lifecycle, refusal and cleanup scenarios.
  Admission closes before draining in-flight requests. A drain timeout retains
  the source for a cleanup retry. SDK controller calls themselves do not have a
  proven wall-clock bound.
- The actual NVS backend passes 15 groups, including isolated role namespaces,
  sticky I/O failures, exact record sizes, commit/readback failures and durable
  counter advancement. This is host fault injection, not physical power-cut proof.
- Sessions verify the authenticated remote static key before split and reserve
  counters before sealing. Changed traffic-key domains with retained counters
  refuse; fresh-key restart is deliberately not a production persistence design.
- Independent scalar/session proof requires the pinned admitted local libsodium
  source and producer environment. It is an explicit local gate, not a silently
  skipped generic CI test. Entropy and NVS suites run in the normal host matrix.

The complete Windows host matrix passed, including the historical loader
compatibility checks and simulator UI 13/13. Documentation checks, 13 documentation
tests, 16 V1 scope groups and frozen evidence bindings also pass.

## Matched resource results

Each delta subtracts its own harness-preserving control. Coverage and target
configuration differ between candidates; these are not interchangeable product
firmware sizes or a library-selection decision.

| Candidate | Flash delta (bytes) | Static RAM delta (bytes) | Exact evidence |
| --- | ---: | ---: | --- |
| Libsodium | 125,160 | 1,232 | [Matched result](../../tests/benchmarks/crypto/OT-163-LIBSODIUM-MATCHED-RESOURCE-2026-09-10.json) |
| Corrected Monocypher | 19,660 | 0 | [Matched result](../../tests/benchmarks/crypto/OT-163-MONOCYPHER-CORRECTED-MATCHED-RESOURCE-2026-09-10.json) |
| Corrected mbedTLS | 99,632 | 1,152 | [Prior matched result](../../tests/benchmarks/crypto/OT-163-MBEDTLS-CORRECTED-MATCHED-RESOURCE-2026-09-10.json) |

Libsodium candidate/control pairs reproduce and all 73 allocated harness sections
match across four builds. These new resource images are distinct from the old
physical OT122 image; its timing is not transferred to them. Corrected Monocypher
reuses its exact accepted candidate pair and fresh controls, with all 70 allocated
harness and eight protocol sections matching. Its narrow additive validator pins
the frozen predecessor and corrected configuration. Focused accounting suites
pass 12 existing groups and eight corrected-Monocypher groups respectively.

## Remaining coherent gate

Bind authenticated invitation/prologue policy and receive replay/lifecycle rules
to the evaluation composition, then prepare a solicited, challenge-bound capture
and scoped NVS recovery path. The current categorical boot result is not such a
receipt. New physical execution requires fresh artifact/port/layout preflight and
current scoped authority; previous one-use grants remain consumed. Physical
entropy, interrupted NVS, restart/key retirement and two-node lifecycle remain
unaccepted. Cold-power disassembly remains deferred.

Historical libsodium/Monocypher raw-capture custody remains open. Preserve the
[prior admission assessment](../security/CRYPTO_ADMISSION_BATCH_2026-09-10.md)
and exact receipts; do not reconstruct raw transcripts from aggregates. Complete
applicable Phase 3 admission and explicitly select the library, suite, handshake,
KDF and wire instantiation before product integration. Accepted evidence did not
change public website capability or weighted V1 completion.
