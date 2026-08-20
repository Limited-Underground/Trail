# Cryptographic Benchmark Evidence v0

Status: host-tested evidence boundary; pre-crypto build, candidate-readiness,
and source-lock admission contracts frozen while target candidate execution
remains blocked, 2026-08-20

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
The blocked candidate-readiness validator is
`tools/crypto_benchmark_readiness.py`, and its accepted ledger is
`tests/benchmarks/crypto/OT-094-OT005-CANDIDATE-READINESS-V0.json`.
The candidate source-lock admission validator is
`tools/crypto_candidate_source_lock.py`, and its accepted contract is
`tests/benchmarks/crypto/OT-095-OT005-CANDIDATE-SOURCE-LOCK-ADMISSION-V0.json`.
The bounded mbedTLS/PSA static validator is
`tools/crypto_mbedtls_static_eligibility.py`, with accepted assessment
`tests/benchmarks/crypto/OT-096-OT005-MBEDTLS-STATIC-ELIGIBILITY-V0.json`.

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

## OT-094 candidate-readiness contract

`OTCBR0/v0` freezes the admission requirements between the accepted
candidate-free build and any executable OT-005 plan. The accepted ledger has
readiness ID `OT-094-OT005-CANDIDATE-READINESS-V0`, canonical SHA-256
`705b30693196e2f46d8bda7c17acb1e04d7b9092c4a3817286c14d189001b9d3`,
status `readiness_blocked`, and public result
`CANDIDATE-READINESS-CONTRACT-FROZEN-HOST-ONLY; OTCB0-EXECUTION-BLOCKED`.

The ledger binds the unchanged historical `OTCB0/v0` plan and accepted
`OTCBL0/v0` baseline. It preserves baseline-only target, ESP-IDF, compiler, and
sdkconfig facts without treating them as final candidate configuration or
target support. Its six ordered blockers cover exact received target identity,
final common/candidate configuration, the three comparison-source locks and
mbedTLS API/config eligibility, and direct-radio region/MTU/full-PHY evidence.
Every closure digest remains null.

A legacy plan's caller-declared `ready` string is structural only. Without an
independently accepted and fully resolved readiness artifact it cannot create a
result template or yield a passing evaluation. The accepted-ready trust-anchor
set is empty, so the public plan remains `draft_blocked`, readiness unverified,
and execution unauthorized. A future executable plan must use a new accepted
version and bind the OT-094 contract digest plus actual closure evidence rather
than rewriting the historical plan.

The ledger's fixed three-candidate order is the comparison set from Decision
0003, not a selection. Espressif libsodium and Monocypher remain unimported and
unlocked. The installed ESP-IDF mbedTLS source observation is explicitly not a
project dependency lock or API/config eligibility result. No radio profile,
suite/library, handshake/KDF, or packet-v1 wire is selected.

See [Decision 0038](../decisions/0038-host-only-ot005-candidate-readiness-contract.md)
and the [OT-094 evidence](../../tests/hardware/OT-094-2026-08-20.md).

## OT-095 candidate source-lock admission contract

`OTCSL0/v0` freezes the host-only evidence admission rules that a future
candidate source lock must satisfy. The accepted contract has admission ID
`OT-095-OT005-CANDIDATE-SOURCE-LOCK-ADMISSION-V0`, canonical and policy
SHA-256 `c0bd923782d0977f8b375cbd2fe8cde5ff132a26b8b6a7ea34a62111bd101f1f`,
status `admission_contract_frozen_host_only`, and public result
`SOURCE-LOCK-ADMISSION-CONTRACT-FROZEN-HOST-ONLY; ZERO-SOURCES-ACQUIRED-OR-IMPORTED; OTCBR0-READINESS-BLOCKED`.

The contract preserves the three-candidate comparison order and distinguishes
six evidence layers: acquisition receipt, immutable source tree, project
dependency lock, API/configuration eligibility, candidate import, and benchmark
execution. Admission semantics are defined for the first five; benchmark-
execution admission remains undefined and blocked. None is sufficient alone.
Candidate-specific accepted source-lock,
API/configuration, and import registries are separate and empty. The registries
are excluded from the immutable policy digest, but changing an anchor alone
cannot advance the current zero-source blocked state; a future truthful accepted-
state contract revision must reconcile the candidate, version, license, source
tree, project lock, parent-IDF binding where applicable, final configuration,
import evidence, and independent accepted digests.

OT-095 accepted, acquired, and imported zero sources. The installed ESP-IDF
mbedTLS/PSA observation is not a project lock and does not prove required API,
Ed25519, or final-configuration eligibility. All six `OTCBR0/v0` requirements
remain blocked, `OTCB0/v0` remains `draft_blocked`, and no acquisition, import,
build, benchmark, target support, cryptographic selection, implementation,
physical evidence, authority, or score credit was added.

See [Decision 0039](../decisions/0039-host-only-candidate-source-lock-admission-contract.md)
and the [OT-095 evidence](../../tests/hardware/OT-095-2026-08-20.md).

## OT-096 mbedTLS/PSA static eligibility

`OTCMSE0/v0` records the clean, already-installed ESP-IDF v6.0.2 /
mbedTLS 4.1.0 pinned source with canonical SHA-256
`3034da5a9f21ed663f82dc45ba976f8b5d6ec4ff353c2f96a3d5de4b586c013e`.
Five of eight fixed operations have concrete source/API paths: X25519,
SHA-256, HKDF-SHA-256, and ChaCha20-Poly1305 encrypt/decrypt. Ed25519
sign/verify and Noise XK implementations are absent. Generic APIs/identifiers
do not prove implementation, and defaults/pre-selection sdkconfig do not prove
final configuration.

The result is
`MBEDTLS-STATIC-ELIGIBILITY-FROZEN-HOST-ONLY; FIXED-OT005-OPERATION-SET-INELIGIBLE; OTCBR0-BLOCKER4-REMAINS-OPEN`.
It is not a benchmark failure, global rejection, source lock, import, or
selection. All six blockers and all authority/score closures remain open. See
[Decision 0040](../decisions/0040-host-only-mbedtls-psa-static-eligibility.md)
and [OT-096 evidence](../../tests/hardware/OT-096-2026-08-20.md).

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

Ten deterministic `OTCB0/v0` scenario groups cover the blocked public plan,
exact ready-plan structure, candidate/gate set enforcement, plan-bound template
admission refusal without independently accepted readiness, measured failures,
plan/config mismatch, private-text rejection, and incomplete/noncanonical
measurements. Thirteen `OTCBR0/v0` scenario groups cover immutable plan/baseline
binding, target/configuration/candidate/radio blockers, authority closure,
strict bounded loading, privacy, and no execution capability. Thirteen
`OTCBL0/v0` scenario groups cover source/tool/configuration locks,
exact-type and loader rejection, two-receipt provenance, derived artifact
equality, environment restoration, aggregate-only claims, privacy, and CLI
sanitization. The focused suites, Heltec and V1/V1.5 static admission checks,
both publication-safety layers, and the complete host gate pass.

This remains an evidence-quality boundary. No candidate library has been
imported, benchmarked, or selected; no OT-005 candidate result exists. Entropy,
interoperability, candidate resource deltas, radio, physical join/revoke/reset,
protected storage, and power-interruption evidence remain open.
