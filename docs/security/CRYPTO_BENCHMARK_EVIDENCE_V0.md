# Cryptographic Benchmark Evidence v0

Status: host-tested evidence boundary; pre-crypto build baseline frozen and
target candidate plan blocked, 2026-08-20

## Purpose

`OTCB0/v0` turns the cryptographic selection gate into reproducible,
machine-checked evidence. It does not benchmark cryptography on the host, select
a library, implement Noise, or make packet v0 secure. It ensures that a future
ESP32-S3 comparison cannot omit the exact target, dependency locks, repetitions,
resource costs, radio costs, or failure gates and still be reported as passing.

The candidate-plan/result tool is `tools/crypto_benchmark.py`. The public starting plan is
`tests/benchmarks/crypto/OT-005-CRYPTO-BENCHMARK-PLAN-V0.json`.
The pre-selection build-lock validator is
`tools/crypto_benchmark_baseline.py`.
The separate accepted pre-selection build lock is
`tests/benchmarks/crypto/OT-093-OT005-BUILD-BASELINE-V0.json`.

## Plan contract

A plan has one of two states:

- `draft_blocked`: structurally valid, names at least one blocker, and may leave
  the exact target/lock fields empty; or
- `ready`: no blockers and every board, toolchain, dependency, sdkconfig, and
  radio field is exact.

The ordered candidate set is deliberately fixed to the dated review:

1. Espressif libsodium as the primary target benchmark;
2. the pinned ESP-IDF mbedTLS/PSA build as a comparison; and
3. Monocypher as a small-footprint comparison.

Each candidate needs an exact version, 40-character source commit, dependency-
lock SHA-256, and SPDX license expression before the plan can become `ready`.
The plan also fixes exactly 100 cold and 100 warm repetitions and the complete
eight-gate acceptance set. A canonical, formatting-independent JSON SHA-256
binds results to the reviewed plan.

The current public plan remains honestly `draft_blocked`. Its blocker array is
preserved as a historical plan snapshot naming exact client board/revision,
ESP-IDF/toolchain/sdkconfig, candidate commits/locks, and direct-radio MTU/PHY.
OT-093 does not rewrite that snapshot or make it `ready`. It freezes one
pre-selection build environment, while final candidate-ready target/toolchain/
sdkconfig applicability, candidate dependency locks, and the direct-radio
profile remain unresolved. The validator will not generate a result template
from the blocked plan.

## OT-093 pre-selection build lock

`OTCBL0/v0` freezes the deterministic build immediately before any OT-005
candidate import. Its accepted result is
`BUILD-BASELINE-FROZEN; OTCB0-EXECUTION-BLOCKED`; the canonical aggregate
SHA-256 is
`16ffe83af7e3c1f00b5d123eae30e3ac4a0ea2dea0cb08bcc60b990d3e881733`.

Two independent, initially absent, cache-disabled build roots use stable project
version `ot093-precrypto-v0` and exact source-index, raw-working-byte,
configuration, ESP-IDF, `idf.py`, compiler, CMake, Ninja, Python, and isolated-
Python-cache locks. Each run must exit zero with zero warnings. Individual
helper receipts are reconciliation-pending and cannot claim a frozen baseline.
Independent aggregation hashes and reconciles both raw receipts against their
on-disk artifacts. The aggregate validator validates those embedded receipt
identities, derives the same normalized receipt SHA-256
`265ee99c47784100c8a00dd021c3f10a29ca71cc97361639f7f85e6ea13d10df`,
and requires exact ordered name/size/SHA equality for seven public artifacts.

The accepted application is 471,456 bytes with 4,705,888 bytes of headroom in
the smallest application slot. Exact artifact tuples, source/toolchain digests,
and validation results are recorded in the
[OT-093 evidence](../../tests/hardware/OT-093-2026-08-20.md).

This build lock imports and executes no OT-005 candidate or secure-LoRa adapter.
Existing ESP-IDF/NimBLE cryptographic objects are not a selection. The ordinary
Heltec `standard` build remains a compile-only receipt and cannot publish an
OT-093 frozen-baseline result.

## Result contract

One result evaluates one candidate under one exact ready plan. It records:

- the benchmark ID, canonical plan SHA-256, candidate, and completion time;
- cold/warm repetition counts;
- min/median/p95/max microseconds for Ed25519 sign/verify, X25519, SHA-256,
  HKDF-SHA256, IETF ChaCha20-Poly1305 encrypt/decrypt, and the XK handshake;
- build success and warning count;
- linked-flash delta, static and peak-dynamic RAM, maximum stack used, and
  watchdog resets;
- handshake bytes, fragments, measured airtime, and retry coverage;
- binary, sdkconfig, and SBOM SHA-256 values plus private raw-evidence
  retention; and
- every vector/interoperability/entropy/wipe/counter/lifecycle/license gate.

Timing statistics must be positive and nondecreasing. The result sdkconfig must
match the plan. Unknown/missing fields, noncanonical operations, plan-hash
mismatch, and private machine/device text are invalid rather than measured
failures.

For a structurally valid result, the deterministic verdict is `fail` if the
plan is not ready, either repetition count is below 100, the build fails, any
compiler warning or watchdog reset occurs, raw evidence was not retained, or
any required gate is false. Only a complete result without those failures is
`pass`.

A candidate-level `pass` is benchmark evidence, not automatic production
selection. Library selection still requires review of all candidates, target
behavior, recovery constraints, and the decision gate.

## Public-evidence boundary

The artifact has no fields for serial number, MAC address, transport port,
pairing PIN, password, private key, secret value, or local path. The validator
also rejects Windows/user paths, COM-port text, MAC-like values, and obvious
credential assignments found inside free text. Raw traces stay private; public
artifacts contain only aggregate measurements and hashes.

Result-template creation requires a ready plan and uses exclusive file creation,
so it refuses overwrite. The generated template has blank hashes/timestamp,
zero measurements/repetitions, `build.passed=false`, and every gate false; it is
intentionally invalid/incomplete and cannot be mistaken for a passing result.

## Host evidence and remaining gates

Eight deterministic `OTCB0/v0` scenario groups cover the blocked public plan,
exact ready-plan structure, candidate/gate set enforcement, plan-bound template
creation and overwrite refusal, a complete pass, measured failures, plan/config
mismatch, private-text rejection, and incomplete/noncanonical measurements.
Twelve `OTCBL0/v0` scenario groups cover source/tool/configuration locks,
exact-type and loader rejection, two-receipt provenance, derived artifact
equality, environment restoration, aggregate-only claims, privacy, and CLI
sanitization. Both focused suites and the complete host gate pass.

This remains an evidence-quality boundary. No candidate library has been
imported, benchmarked, or selected; no OT-005 candidate result exists. Entropy,
interoperability, candidate resource deltas, radio, physical join/revoke/reset,
protected storage, and power-interruption evidence remain open.
