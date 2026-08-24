# Cryptographic Benchmark Evidence v0

Status: host-tested evidence boundary; historical plans preserved, phases 0
and 1 accepted at source/API/import counts `3/3/3`; exact-target measurement
blocked only by absent fresh execution authority, 2026-08-22

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
The same source-lock validator also owns the current license-aware contract
`tests/benchmarks/crypto/OT-097-OT005-LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1.json`.
The OT-116 successor validator is `tools/crypto_benchmark_execution_plan.py`; its accepted append-only review and immutable phased contract are `tests/benchmarks/crypto/OT-116-OT005-SUCCESSOR-READINESS-DECISION-V1.json` and `tests/benchmarks/crypto/OT-116-OT005-EXECUTABLE-BENCHMARK-PLAN-V1.json`.

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

## OT-097 license-aware source-lock admission v1

`OTCSL0/v1` has admission ID `OT-097-OT005-LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1`, canonical/policy SHA-256 `51639e1b9342dc9e501fb0682d044c0f7c05e691e1a26f463358a753f28a123a`, and result `LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1-FROZEN-HOST-ONLY; ZERO-SOURCES-ACQUIRED-OR-IMPORTED; OTCBR0-READINESS-BLOCKED`.

It preserves the v0 evidence layers while making four license fields mandatory for future acceptance: upstream SPDX expression, explicit project choice, complete inventory, and a separate inventory digest. These facts cannot substitute for one another. OTCSL0/v0 remains valid historical evidence but is permanently non-admitting.

The candidate entries preserve their distinct upstream expressions and current project choices. Every inventory remains incomplete, every inventory digest is null, and every source-lock state is `not_accepted`. OT-097 acquires/imports zero sources and grants no legal clearance or compatibility determination. All six blockers and all authority/readiness/benchmark/selection/support/physical/score closures remain open. See [Decision 0041](../decisions/0041-license-aware-source-lock-admission-v1.md) and [OT-097 evidence](../../tests/hardware/OT-097-2026-08-20.md).

## OT-098 external candidate acquisition and static inspection

`OTCAI0/v0` binds exact official origins, refs, commits, trees, manifests,
license files, and unresolved signature state for acquired libsodium 1.0.22 and
Monocypher 4.0.3 sources. Its raw SHA-256 is
`b7be03e305c6253e10f69f624132a736cce5aea3f559760cde4f948ae79abad6`.
Static source paths cover 7/8 operations for libsodium and 5/8 for Monocypher;
neither contains Noise XK composition. Monocypher also lacks SHA-256 and
HKDF-SHA-256.

Signature trust remains unresolved. Monocypher's project license choice remains
null, inventories are incomplete, and neither candidate has an OpenTrail
project lock, final configuration, import, build, benchmark, or selection.
All six blockers remain open. See [Decision 0042](../decisions/0042-external-candidate-acquisition-static-inspection.md)
and [OT-098 evidence](../../tests/hardware/OT-098-2026-08-20.md).

## OT-099 libsodium managed-import evidence

`OTLMI0/v0`, raw SHA-256 `8285fa7308bfc83a5d55503a7a3e1fa4c21895a42b095197b3ec75f634411ec9`,
binds the exact registry lock, 733-entry source manifest, ISC license inventory,
candidate-scoped SPDX record, managed dependencies, and empty patch set. The
evidence is complete, but source-lock admission remains pending because the
controlling OTCSL0/v1 accepted registries remain empty.

An isolated generic ESP32-S3 computer build passed: the probe compiled, the
candidate archive built and entered the link graph, and the ELF linked. Probe
symbols were not retained. This is not exact-target, final-configuration,
crypto-execution, benchmark, or selection evidence. All six blockers and all
device/radio/key/packet-v1/score boundaries remain open. See [Decision 0043](../decisions/0043-libsodium-managed-import-evidence.md)
and [OT-099 evidence](../../tests/hardware/OT-099-2026-08-20.md).

## OT-100 libsodium source-lock admission

`OTCSLA0/v0`, raw SHA-256 `df595f2d07ba1b5d0a9bdf70237b1f0ea5a01fe8cb5a63ffb3575fe484faede0`,
binds the unchanged OT-097 policy and OT-099 evidence and independently accepts
only the exact Espressif libsodium 1.0.22 source anchor. The mbedTLS/PSA and
Monocypher source registries and every API/configuration and import registry
remain empty.

OT-094 and OT-097 remain historical six-blocker artifacts. At OT-100, the
then-current set was five after closing only
`espressif_libsodium_source_lock_absent`; readiness
remains blocked. This adds no target/final-configuration proof, device/radio/key
or crypto execution, benchmark, selection, packet-v1, legal/compatibility,
physical, authority, or score evidence. See [Decision 0044](../decisions/0044-libsodium-source-lock-admission-delta.md)
and [OT-100 evidence](../../tests/hardware/OT-100-2026-08-20.md).

## OT-102 Monocypher source-lock admission

Exact `OTCSLE0/v1` Monocypher 4.0.3 source evidence has SHA-256
`fe037820304103f7ca2253665076e4dc41740598ca9742ba8d45f6ec64ebc06f`.
It binds all 161 retained upstream Git blobs at commit
`ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f` and tree
`eccc366491fc98c4149401d580ce41081a7854b1`, plus canonical acquisition,
full-tree, complete license, SPDX, transitive-dependency, zero-patch, and
project-lock evidence. The owner selected the upstream `BSD-2-Clause` branch;
that is not legal clearance or a compatibility determination.

Strict host-only `OTMSLA0/v0`, raw SHA-256
`6dbeeac0266f9e6dd90265cdd71a721acfd36b4308dcb87180bd9d7c24c77e52`,
accepts exactly one Monocypher source lock. It preserves the historical six-
blocker and prior five-blocker states, closes only
`monocypher_source_lock_absent`, and records the then-current four requirements.
The total accepted source count is two including libsodium. Every
API/configuration and candidate-import registry remains empty, and readiness
stays blocked.

This adds no firmware import or build, crypto execution, device/radio/key
action, benchmark, selection, packet-v1, legal/compatibility, physical,
authority, or score evidence. See [Decision 0045](../decisions/0045-monocypher-source-lock-admission-delta.md)
and [OT-102 evidence](../../tests/hardware/OT-102-2026-08-20.md).

## OT-103 exact received target-profile admission

Strict host-only `OTRTPE0/v0`, raw SHA-256
`517809caf31250d126cc3619f9d05386a92811a594dca0087d9acbf1b671147e`,
binds `heltec-v4-bench-candidate` / `OT-DEV-001` to the exact received Heltec
Automation `WiFi LoRa 32 V4`, PCB/RF-variant model `HTIT-WB32LAF`, received
revision `V4.2`, documented-high-band profile, ESP32-S3 / ESP32-S3R2 revision
v0.2, 40 MHz crystal, 16 MiB flash, and 2 MiB PSRAM. The retained public
record contains normalized observations from five owner-provided photos; it
retains no raw photo, local path, EXIF/location data, or private device identity.

The official V4.2 datasheet was inspected at the recorded URL and digest but
was not retained. Its Table 1.5 records `868-928 MHz` and `28 +/- 1 dBm` for
`HTIT-WB32LAF`; Table 3.5.1 separately records `863-928 MHz` and
`28 +/- 1 dBm`. Those source values remain distinct rather than being
normalized or reconciled. The official family-level `SX1262` statement is not
an electrical verification of the received unit, and the package checkbox
state, installed antenna, legal region, and direct-radio profile remain
unclaimed.

Strict host-only `OTRTPA0/v0`, raw SHA-256
`98cce120cadc1bddf5851f1480ae181488e17277ba0a2c8c8c38a70a062be105`,
closes only `exact_received_target_profile_unresolved`. OT-094 and OT-097
remain historical six-blocker artifacts; OT-102 retains the prior four-blocker
state; three current requirements remain: final candidate build configuration,
ESP-IDF mbedTLS/PSA dependency-lock plus API/configuration eligibility, and the
direct-radio MTU/PHY/region profile. Source/API-configuration/candidate-import
anchor counts remain `2/0/0`, and readiness stays blocked.

This is exact identity evidence, not support, compatibility, regulatory or
legal-region acceptance, radio-profile proof, firmware import/build, crypto or
device/radio/key action, benchmark, selection, packet-v1 authority, continuing
authority, or score evidence. See [Decision 0046](../decisions/0046-exact-received-target-profile-admission-delta.md)
and [OT-103 evidence](../../tests/hardware/OT-103-2026-08-20.md).

## OT-105 pinned ESP-IDF mbedTLS/PSA source-lock admission

Strict `OTCSLE0/v1`, raw SHA-256 `ae12ad7da6702ac85092e9cb8ad793b749871153fadee8b1a276e5a46b036e49`, and append-only `OTMPSLA0/v0`, raw SHA-256 `26b6acdc9928eb9510a0baed53c609a4f9a23288155636c6462747745f28ac85`, accept the metadata-only dependency lock for installed pinned ESP-IDF v6.0.2 / mbedTLS 4.1.0. Project-lock SHA-256 is `12f8699d8d286a484e054df186fb0e8c97b75263d23caf4bd77ed48082e9c7ab`.

All 3,551 source and 198 glue files are covered exactly once by full-tree, glue, 3,749-file license, SPDX, seven bundled partitions, patch, and project-lock records. Bundled partitions are not runtime/link evidence. Zero patches means only zero OpenTrail-applied patches after the exact pinned Espressif gitlink; Espressif/upstream divergence is unassessed.

OT-103 remains historical `2/0/0`. OT-105 records current `3/0/0`, closes no blocker, and leaves three requirements open. OT-096 remains 5/8, so the composite dependency-lock plus API/configuration requirement is unresolved. No acquisition/copy/import/build/API eligibility, device/radio/key action, benchmark, selection, legal/compatibility/support, physical evidence, or score is added. See [Decision 0047](../decisions/0047-host-only-mbedtls-psa-source-lock-admission-delta.md) and [OT-105 evidence](../../tests/hardware/OT-105-2026-08-21.md).

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

This remains an evidence-quality boundary. OT-116 now records all six historical
OT-094 closure requirements satisfied and freezes the exact phased procedure,
but it imports, builds, benchmarks, or selects no candidate and creates no
OT-005 result. OT-117 subsequently admits complete eight-of-eight host-only libsodium API/
configuration evidence, advancing accepted source/API/import counts to
`3/2/0`. OT-118 then admits strict five-of-eight Monocypher comparison
evidence, populating all three API/configuration registries and advancing
counts to `3/3/0`. OT-119 then accepts the second-node exact profile and
completes phase 0. OT-120 atomically accepts every retained candidate
import/build anchor and completes phase 1 at `3/3/3`. OT-121 and OT-122 then
admit bounded two-node libsodium local-primitives, Noise XK, and runtime-resource
checkpoints with exact Trail restoration while Phase 2 remains incomplete.
OT-123 prepares the five-of-eight Monocypher comparison. OT-124 records its
subsequent attempt as aborted after benchmark readback and a bounded capture
timeout, with every touched node exactly restored and no Monocypher result
admitted; that abort consumes Decision 0059. OT-125 separately grants one
corrected, non-reusable application-only retry, whose hardware execution remains
pending both USB endpoints and visible reset of both displays. Entropy,
interoperability, candidate resource deltas, exact benchmark radio cost,
remaining candidate measurements, physical join/revoke/reset, protected
storage, and power-interruption evidence remain open.
