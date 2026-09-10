# OT-163 parallel crypto admission batch

## Accepted result

This batch completes corrected mbedTLS matched resource accounting, independent
host Noise XK interoperability and an additive entropy lifecycle guard. It also
reconciles the remaining historical capture-custody gap. The
[bound assessment](../../tests/benchmarks/crypto/OT-163-ADMISSION-BATCH-2026-09-10.json)
owns exact source/evidence hashes and the eight-gate disposition. Previous
[corpus evidence](../../tests/benchmarks/crypto/OT-163-CRYPTO-CORPUS-RECONCILIATION-2026-09-10.json)
remains immutable; this is a successor assessment, not complete Phase 3 admission.

## Matched resources

The [corrected result](../../tests/benchmarks/crypto/OT-163-MBEDTLS-CORRECTED-MATCHED-RESOURCE-2026-09-10.json)
reuses the exact accepted corrected candidate A/B and two fresh, cache-disabled,
warning-free no-candidate builds. Each side reproduces its complete artifact tuple.
The unchanged signed-delta validator and its 12 regression tests pass.

| Measurement | Candidate | Matched control | Added bytes |
| --- | ---: | ---: | ---: |
| Linked flash | 245,466 | 145,834 | 99,632 |
| Static RAM | 50,187 | 49,035 | 1,152 |

Both controls use `ot151-mbedtls-psa-v0`, the matched configuration and shared
harness. An explicit macro-prefix mapping aligns embedded shared `__FILE__`
strings with the retained candidate's logical source path. Actual current build
paths remain in private provenance. The earlier controls with 48 extra path-string
bytes are unadmitted exploratory outputs. No historical candidate source changed.
These figures do not rank the differently configured libsodium/Monocypher targets;
each still needs its own harness-preserving matched control.

## Independent Noise proof and correction

The [reproducible proof](../../tests/benchmarks/crypto/independent_noise_xk/README.md)
uses pinned noiseprotocol 0.3.1 with cryptography/OpenSSL as an independent
producer. Two fixed empty-payload XK cases, with empty/nonempty prologues,
compare all three messages, final handshake hashes and both directional split
keys. The C path compiles the actual adapter against 15 unchanged upstream scalar
primitive sources; every one of the 731 admitted component files is verified.

The frozen adapter and the additive corrected adapter each pass 26 groups,
including wrong responder identity, altered/truncated/extended messages and
wrong ordering. Ten harness/admission tests also cover source/vector tampering,
shadow headers, byte order and the exact single-line correction. Independent
review verified 17 object hashes and 102 dependency paths.

The real libsodium header exposed a nonnull API-contract violation in the old
adapter's zero-length HKDF input. The
[additive successor](../../tests/benchmarks/crypto/independent_noise_xk/nonnull_adapter/noise_xk_libsodium.c)
changes only that input pointer to a nonnull empty string, preserving length zero.
The successor adapter and probe compile with warnings treated as errors. The
frozen baseline remains unchanged and retains its warning in evidence.

This proves the stated deterministic scalar interoperability surface. It does not
prove full Noise conformance, `sodium_init`/CPU dispatch, production randomness,
target initialization or invitation/Packet V1 integration. The successor has not
been built into or flashed to either Heltec.

## Entropy lifecycle

The [source assessment](../../tests/benchmarks/crypto/OT-163-ENTROPY-LIFECYCLE-ASSESSMENT-2026-09-10.json)
traces the actual Heltec adapter and pinned ESP-IDF 6.0.2. The application marks
randomness ready before NimBLE starts, and containment does not revoke that state.
Audited normal consumers follow stack initialization; this is a lifecycle contract
gap, not evidence that weak random material was used on hardware.

The new [serialized guard](../../firmware/components/security/include/opentrail/serialized_secure_random.hpp)
requires a real readiness probe, rejects new fills immediately on revoke and
reports whether an in-flight fill has drained. Source loss or partial output
closes admission; temporary output is wiped and no partial caller result escapes.
The owner must keep the physical source enabled until revoke reports quiescence.
An observed source loss requires explicit qualified activation before recovery.

Actual-adapter host tests cover premature/stale readiness, bounds, in-flight
revocation, loss during a fill, unchanged failure output and recovery. They use
an explicitly deterministic `esp_fill_random` stub. The public test runner is
included in the regular Windows host matrix. Target wiring, exact no-modem-sleep
configuration, failure/containment ordering, target builds and physical entropy
remain open. Do not enable the internal SAR entropy source concurrently with
RF/ADC as a shortcut. Cold-power enclosure disassembly remains owner-deferred.

## Custody and remaining acceptance

The [custody assessment](../../tests/benchmarks/crypto/OT-163-CAPTURE-CUSTODY-ASSESSMENT-2026-09-10.json)
checks 73 related private files without an extension filter; no file matches the
accepted historical libsodium capture hashes. The exact Monocypher receipt and
journal match their public projection, but their result digest binds a parsed
result, not raw serial bytes. This scoped search is not a global absence claim.
The corrected mbedTLS canonical validated streams remain retained and audited.

The existing secure-LoRa lifecycle suite passes 40 host-model scenario groups.
It is reusable policy evidence, not actual target invitation, persistent-counter,
reset/rekey or phone-to-phone acceptance. Full eight-gate admission remains open.

The next consolidated work is candidate-specific resource controls and an
evaluation-target composition of the corrected Noise adapter, guarded entropy,
durable counters and existing invitation/refusal policy. Preserve current factory
reset semantics and the accepted V1 physical-attacker boundary. Any successor
capture or physical target trial needs fresh exact preflight and scoped authority;
all preceding grants remain consumed.

No device access, firmware flash, APK change, Git publication or website operation
occurred in this batch. Completion values and weights remain unchanged. Accepted
evidence did not change public website capability status.
