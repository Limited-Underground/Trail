# Decision 0039: Freeze host-only candidate source-lock admission

- Status: Accepted host-only admission contract; source acquisition, import, and readiness remain blocked
- Date: 2026-08-20
- Work item: OT-095
- Scope: Machine-checked evidence boundaries for future OT-005 candidate source locks

## Context

[Decision 0003](0003-crypto-benchmark-gate.md) requires exact candidate locks
before the ESP32-S3 cryptographic comparison can run. [Decision 0038](0038-host-only-ot005-candidate-readiness-contract.md)
correctly keeps `OTCBR0/v0` at `readiness_blocked`, with an empty accepted-ready
trust-anchor set and all six readiness requirements open.

The future-ready validation shape nevertheless treated a nonempty lock-kind
string plus syntactically valid commit and SHA-256 values as source-lock facts.
That is not enough to prove acquisition provenance, immutable source contents,
a project dependency lock, API/configuration eligibility, candidate import, or
benchmark eligibility. The current blocked state could not be bypassed, but a
future accepted readiness record needed stronger evidence admission before any
source is acquired.

## Decision

OpenTrail accepts the machine-readable
[`OTCSL0/v0` admission contract](../../tests/benchmarks/crypto/OT-095-OT005-CANDIDATE-SOURCE-LOCK-ADMISSION-V0.json)
with admission ID `OT-095-OT005-CANDIDATE-SOURCE-LOCK-ADMISSION-V0`, canonical
and policy SHA-256
`c0bd923782d0977f8b375cbd2fe8cde5ff132a26b8b6a7ea34a62111bd101f1f`,
status `admission_contract_frozen_host_only`, and public result:

`SOURCE-LOCK-ADMISSION-CONTRACT-FROZEN-HOST-ONLY; ZERO-SOURCES-ACQUIRED-OR-IMPORTED; OTCBR0-READINESS-BLOCKED`

The contract binds the unchanged historical `OTCB0/v0` plan, accepted
`OTCBL0/v0` baseline, and blocked `OTCBR0/v0` readiness ledger. It preserves
the exact three-candidate order and public version/license facts:

1. Espressif libsodium 1.0.22, ISC;
2. ESP-IDF mbedTLS/PSA 4.1.0, Apache-2.0, with exact parent-IDF binding; and
3. Monocypher 4.0.3, CC0-1.0 OR BSD-2-Clause.

This order is the benchmark set from Decision 0003, not a production selection.

## Evidence layers and trust boundary

The contract separates six evidence layers: acquisition receipt, immutable
source tree, project dependency lock, API/configuration eligibility, candidate
import, and benchmark execution. Admission semantics are defined for the first
five; benchmark-execution admission remains undefined and blocked. No layer is
sufficient by itself.

Future source evidence must bind the exact candidate, version, license,
candidate-specific lock kind, immutable commit and full-tree manifest, project
dependency-lock bytes, license, SBOM, transitive-dependency and patch manifests,
and—where applicable—the exact parent ESP-IDF commit. Logical paths are bounded,
relative, ordered, case-unique, and reject traversal, symlinks, reparse points,
Windows-reserved names, and trailing dot or space forms.

Source-lock, API/configuration, and candidate-import evidence use three separate
candidate-specific accepted-digest registries. All three registries are empty.
They are excluded from the immutable policy digest so a later reviewed revision
can add exact evidence identities without changing the policy identity; however,
the current v0 contract remains a zero-source, blocked-state contract. Mutating
future anchor values alone cannot advance readiness. A future truthful accepted-
state contract revision must reconcile every layer and claim.

The readiness validator now requires candidate source, API/configuration, and
import evidence to reconcile with the exact candidate, plan lock, parent-IDF
binding, final configuration, and independent accepted digest before any future
ready authority can be considered.

## Current result

No source lock is accepted. No source was acquired or imported by OT-095. The
installed ESP-IDF mbedTLS/PSA observation remains exactly that: an observed
comparison source, not a project dependency lock or proof of required API,
Ed25519, or final configuration eligibility.

All six `OTCBR0/v0` requirements remain blocked:
`exact_received_target_profile_unresolved`,
`final_candidate_build_configuration_unresolved`,
`espressif_libsodium_source_lock_absent`,
`esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved`,
`monocypher_source_lock_absent`, and
`direct_radio_mtu_phy_region_unresolved`. `OTCB0/v0` remains
`draft_blocked` and unexecuted.

## Authority, validation, and progress

All acquisition, import, build, benchmark-execution, device, radio, key/entropy,
suite-selection, packet-v1, physical-evidence, readiness-advancement, and score
authority or claims remain false. The validator is pure host-side admission code;
it exposes no download, subprocess, socket, candidate-import, build, hardware,
radio, key, or write capability.

Focused validation passes 14 `OTCSL0/v0`, 16 `OTCBR0/v0`, 10 `OTCB0/v0`, and
13 `OTCBL0/v0` scenario groups. Strict loaders, exact types and ordering,
candidate-specific reconciliation, path and manifest safety, privacy, sanitized
failures, cycles, bounds, and no-capability source scans are covered.
The complete host gate also passes, including both publication-safety layers,
59 loader scenarios, and simulator groups 33/23/15/10 plus 13/13 UI scenarios.

This governance result earns no progress credit. Android remains 60%. V1
Companion remains exact 43.75% and displayed 44%. The historical standalone
baseline remains exact 31.75% and displayed 32%. V1.5 and V2 remain unmeasured.

## Next gate

All six OT-094 readiness requirements remain the gate. External libsodium and
Monocypher acquisition requires separate owner authorization; acquisition does
not authorize repository import, build, execution, or selection. The existing
pinned mbedTLS/PSA tree may receive a separate read-only provenance and API/
configuration audit, but blocker closure requires accepted evidence rather than
an installed-source observation. Only after all requirements close may a new
immutable executable benchmark plan be accepted and the exact comparison be
separately authorized.
