# Decision 0038: Freeze the host-only OT-005 candidate-readiness contract

- Status: Accepted host-only readiness contract; candidate readiness and OT-005 execution blocked
- Date: 2026-08-20
- Work item: OT-094
- Scope: Machine-checked admission requirements before any OT-005 candidate benchmark

## Context

[Decision 0003](0003-crypto-benchmark-gate.md) forbids cryptographic selection
before an exact ESP32-S3 target comparison passes its security, interoperability,
resource, entropy, persistence, recovery, license, and failure gates.
[Decision 0037](0037-pre-crypto-build-baseline.md) and `OTCBL0/v0` froze the
candidate-free build baseline, but deliberately left the historical
[`OTCB0/v0` plan](../../tests/benchmarks/crypto/OT-005-CRYPTO-BENCHMARK-PLAN-V0.json)
at `draft_blocked`.

The original `OTCB0/v0` validator treated a caller-declared `ready` document as
structurally sufficient for template creation. Its synthetic host fixtures were
useful for exercising result shape, but they did not bind the accepted OT-093
baseline, exact received target identity, final candidate configuration,
project-owned dependency locks, or direct-radio region/MTU/PHY evidence. A
self-declared legacy plan therefore could not be allowed to become execution
admission.

## Decision

OpenTrail accepts the machine-readable
[`OTCBR0/v0` candidate-readiness ledger](../../tests/benchmarks/crypto/OT-094-OT005-CANDIDATE-READINESS-V0.json)
with readiness ID `OT-094-OT005-CANDIDATE-READINESS-V0`, canonical SHA-256
`705b30693196e2f46d8bda7c17acb1e04d7b9092c4a3817286c14d189001b9d3`,
status `readiness_blocked`, and public result:

`CANDIDATE-READINESS-CONTRACT-FROZEN-HOST-ONLY; OTCB0-EXECUTION-BLOCKED`

The ledger binds the unchanged historical `OTCB0/v0` plan and its canonical
SHA-256, the accepted `OTCBL0/v0` baseline and its canonical SHA-256, the exact
baseline-only target/toolchain/sdkconfig facts, the ordered comparison-candidate
set, and the complete fields required for a later direct-radio benchmark
profile. It derives no readiness from caller-supplied Booleans.

The six ordered unresolved requirements are:

1. exact received target profile;
2. final candidate build configuration;
3. Espressif libsodium project source lock;
4. ESP-IDF mbedTLS/PSA dependency lock plus API/configuration eligibility;
5. Monocypher project source lock; and
6. direct-radio region, MTU, and full PHY profile.

Every requirement remains blocked with no closure-evidence digest. The baseline
generated sdkconfig remains explicitly
`PRE-SELECTION-BASELINE-NOT-FINAL-OTCB0`. The target's exact received revision
and RF variant remain null and `supported` remains false. Existing historical
MeshCore settings are not an OpenTrail benchmark-radio profile.

## Legacy admission boundary

A structurally `ready` legacy `OTCB0/v0` JSON is not execution admission. It
cannot create a result template or yield a passing evaluation unless an
independently accepted and fully resolved readiness artifact is supplied. The
current accepted-ready trust-anchor set is empty, so the canonical legacy plan
continues to report `draft_blocked`, `readiness_verified=false`, and
`execution_authorized=false`.

OT-094 does not define the later ready artifact. A future executable plan must
use a new, explicitly accepted version and bind this accepted readiness-contract
digest plus the actual closure evidence. Changing the legacy plan in place is
forbidden because OT-093 and this decision preserve its historical identity.

## Candidate and selection boundary

The fixed comparison order remains Espressif libsodium first, the pinned
ESP-IDF mbedTLS/PSA source as a comparison, and Monocypher as a comparison.
This is a benchmark set, not a selected implementation. The ledger records the
installed mbedTLS source observation only as
`installed_comparison_source_observed_not_locked`; it is not a project-owned
dependency lock or proof that the required APIs and sdkconfig are eligible.

All acquisition, import, result-template, benchmark-build, benchmark-execution,
device, radio-transmit, key/entropy, suite-selection, packet-v1, support,
physical-evidence, and score claims remain false. No candidate or secure-LoRa
adapter was downloaded, imported, built, or executed.

## Validation and privacy

The strict validator uses bounded reads, duplicate-key rejection, exact JSON
types and fields, structural depth/node/string/integer limits, cycle rejection,
privacy scanning, sanitized command-line and JSON failures, immutable plan and
baseline bindings, exact candidate order, and cross-field blocker/null/authority
coherence. It exposes no subprocess, socket, download, candidate-import,
hardware, or write capability.

Focused host validation passes 13 `OTCBR0/v0` scenario groups. The preserved
legacy and baseline suites pass 10 `OTCB0/v0` and 13 `OTCBL0/v0` groups. The
Heltec static target and V1/V1.5 scope-admission suites pass 12 and 16 groups,
both publication-safety layers pass, and the complete `tools/Test-Host.ps1`
gate exits `0`.

Public artifacts contain no local path, device identifier, address, key,
secret, PIN, packet content, or private trace.

## Progress and next gate

This is host-only governance evidence. It adds no candidate readiness,
cryptographic performance, target support, implementation, physical evidence,
or progress credit. Android remains 60%. V1 Companion remains exact 43.75% and
displayed 44%. The historical standalone baseline remains exact 31.75% and
displayed 32%. V1.5 and V2 remain unmeasured.

Next, close the six readiness requirements under their separate owner,
physical, source-acquisition, configuration, radio, and regulatory boundaries.
Only then may a new immutable executable benchmark plan be accepted and the
exact candidate benchmark be separately authorized. A later explicit decision
must still select the suite/library, handshake/KDF, and packet-v1 wire before
implementation.
