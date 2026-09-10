# OT-163 corrected mbedTLS two-device comparison

## Accepted result

The corrected mbedTLS/PSA comparison passed on both physical Heltec WiFi LoRa32
V4.2 / ESP32-S3 / 16 MB devices. Each produced 1,015 validated frames, two gate
records, and 100 cold-class plus 100 warm-class samples for each of five operations.
That is 2,000 timing samples across both devices. These sample classes do not
establish physical cold-power or entropy acceptance.

Both distinct original applications were restored, read back and reset. A fresh
backend then independently verified both full 589,824-byte application/erased-tail
spans and their bootloader, partition and OTA regions before guarded resets.
NVS remained outside every permitted write span. The journal is restored and
no recovery invocation is needed. The one-use grant is consumed.

The [sanitized physical outcome](../../tests/hardware/OT-163-MBEDTLS-COMPARISON-OUTCOME-1-2026-09-10.json)
owns exact timestamps, source/artifact hashes, preflight descriptors, timing
distributions, resource measurements and private canonical-capture digests.
Retained canonical transcripts reparse to exactly the accepted per-node results.
They contain validated benchmark frames, not arbitrary raw serial startup output.

## Setup and measurements

The devices remained together on the bench and were handled sequentially: test
and restore A before writing B. No radio transmission was used. The application
was `ot151-mbedtls-psa-v0`, 245,584 bytes, SHA-256
`458e8bf93be1f2d318a2c6010b6d09a93815edabead4c9149fb584a9f6db5f66`,
at offset `0x10000`. It is the unchanged corrected target from
[Decision 0087](../decisions/0087-record-ot150-abort-and-correct-mbedtls-psa-successor.md).
The [retained-build preparation](OT-163-COMPARISON-PREPARATION-2026-09-10.md)
records reproducible artifact reuse; no firmware rebuild was needed this turn.

| Operation | A warm median, microseconds | B warm median, microseconds |
| --- | ---: | ---: |
| X25519 | 219,239 | 218,925 |
| ChaCha20-Poly1305 encrypt | 163 | 163 |
| ChaCha20-Poly1305 decrypt | 165 | 165 |
| SHA-256 | 45 | 45 |
| HKDF-SHA256 | 452 | 452 |

Both nodes reported 2,468 bytes of peak dynamic RAM and 2,384 bytes of maximum
stack use from an 8,192-byte allocation. The accepted terminal frame reports
zero watchdog resets. These are exact benchmark/runtime measurements, not a
new matched linked-flash/static-RAM delta or a product performance promise.

## Execution and validation

The additive [device backend](../../tools/mbedtls_comparison_hardware.py)
connects the [validated session](../../tools/mbedtls_comparison_execution.py)
to pinned esptool 5.3.1 and pyserial 3.5 under Python 3.14.6. It binds current
ROM/passive identity, 16 MB geometry, distinct originals, protected regions,
application-only writes, exact readbacks and guarded reset. Recovery admission
does not boot an unverified image. Failed or uncertain serial close retains a
lease that blocks ROM operations; bounded writes and drain polling replace an
unbounded serial flush.

The affected host matrix passed 68 tests: 14 concrete backend/authority tests,
15 session tests, seven corrected failure-transcript tests, 14 protocol tests,
and 18 private caller tests. The actual session/runner/backend composition was
exercised with synthetic transports, including both full captures and restoration.
Independent source review resolved its capture-success assertion gap. Caller
tests include altered sources, stale/malformed proofs, changed role identity or
route, consumed namespaces, invalid receipts and uncertain-close containment.

Fresh physical preflight passed before the exact grant was issued. The execution
rechecked identities and originals, consumed that grant once, retained validated
canonical captures with digest/readback checks, and independently postchecked
both roles. The final retained-result audit revalidated the captured bytes against
the strict parser and compared the outcome, receipt and restored journal.

## Remaining gate

This closes the missing two-node mbedTLS timing/runtime comparison. The candidate
remains comparison-only: the admitted configuration covers five operations and
does not supply the required complete eight-operation/Noise surface. Libsodium
remains recommended; no production library, suite or wire format is selected.

Next reconcile corrected matched-control accounting and historical raw-trace
custody, alongside the existing independent Noise/entropy/counter/security proof
gates. The corrected binary does not inherit the older control pair's matched
admission merely because the measured size totals agree. Phase 2/3 admission and
authenticated phone-to-phone product delivery remain open.

Phone applications, bonds and registry were not changed. Their post-test UI/Ready
state was not independently accepted by this comparison. Cold-power disassembly
stays owner-deferred. Completion weights and values are unchanged, as is public
website capability status. No commit, publication or deployment occurred.
