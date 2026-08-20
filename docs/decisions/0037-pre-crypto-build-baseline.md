# Decision 0037: Freeze the pre-crypto OT-005 build baseline

- Status: Accepted deterministic build-only baseline; OT-005 execution blocked
- Date: 2026-08-20
- Work item: OT-093
- Scope: Reproducible ESP32-S3 pre-selection build evidence for the later OT-005
  cryptographic benchmark

## Context

[Decision 0003](0003-crypto-benchmark-gate.md) requires exact-target evidence
before OpenTrail may select production cryptography or packet v1. The public
[`OTCB0/v0` plan](../../tests/benchmarks/crypto/OT-005-CRYPTO-BENCHMARK-PLAN-V0.json)
remains `draft_blocked`: it has not frozen final candidate dependency locks or a
direct-radio MTU/PHY profile, and the accepted Heltec bench target still lacks
an exact received-board revision and RF-variant identity.

A candidate comparison also needs a trustworthy zero-candidate build baseline.
Without that lock, later size, warning, or artifact differences could be caused
by source, toolchain, configuration, path, cache, or version drift instead of
the candidate under test.

## Decision

OpenTrail accepts the machine-readable [`OTCBL0/v0` build lock](../../tests/benchmarks/crypto/OT-093-OT005-BUILD-BASELINE-V0.json)
with baseline ID `OT-093-OT005-BUILD-BASELINE-V0`, canonical SHA-256
`16ffe83af7e3c1f00b5d123eae30e3ac4a0ea2dea0cb08bcc60b990d3e881733`,
and public result:

`BUILD-BASELINE-FROZEN; OTCB0-EXECUTION-BLOCKED`

The accepted baseline is the same pre-selection firmware built twice from two
independent, initially absent build directories. Both builds used the stable
project version `ot093-precrypto-v0`, disabled the shared compiler cache,
normalized reproducible build paths, exited zero, produced zero compiler
warnings, and yielded identical ordered public artifact tuples. Each individual
helper receipt remained only `BUILD-RUN-CAPTURED` and reconciliation-pending.
Independent aggregation hashes and reconciles both raw receipts against their
on-disk artifacts. The aggregate validator then validates the embedded receipt
identities and derives normalized-receipt and exact artifact-tuple equality;
only that validated aggregate may freeze the baseline.

The lock binds the source commit, clean firmware scope, Git-index stage-zero
manifest, raw working-tree byte manifest, `core.autocrlf` state, target
contract, sdkconfig defaults, generated sdkconfig, reproducible defaults, and
LF-normalized build-helper text. It also binds the exact ESP-IDF commit and
`idf.py`, compiler, CMake, Ninja, and Python executable identities. Fresh
per-profile Python caches and a disabled user site prevent checkout-local or
user-site Python bytecode from silently changing the invoked build path.

The build helper's ordinary `standard` profile remains a normal compile-only
receipt. OT-093 clean-source, two-run, candidate-token denylist, provenance,
and pending-reconciliation requirements apply only to the explicit `ot093-a`
and `ot093-b` profiles.

The accepted public artifact set contains exactly the application BIN, ELF,
linker map, bootloader, partition table, generated sdkconfig, and source
partition CSV. Both runs produced the exact same name, byte count, and SHA-256
for every artifact. The 471,456-byte application has 4,705,888 bytes of
headroom in the smallest 5,177,344-byte application slot. This is build-size
evidence only, not a runtime, memory-use, performance, power, radio, or
regulatory result.

## Decision 0003 and candidate boundary

`OTCBL0/v0` is not an `OTCB0/v0` candidate result. No OT-005 candidate or
secure-LoRa adapter was imported or executed. Existing ESP-IDF/NimBLE
cryptographic objects in the baseline are framework dependencies and are not a
suite selection. No suite/library, handshake/KDF, packet-v1 wire, radio
profile, production retry timing, key storage, or protected transport is
selected.

The OT-005 plan remains `draft_blocked`, its execution authority remains false,
and the historical blocker list remains an immutable snapshot of that plan.
OT-093 resolves the reproducible pre-selection build prerequisite only. Final
candidate-ready target/toolchain/sdkconfig applicability, candidate source and
dependency locks, and the direct-radio MTU/PHY profile still require explicit
reconciliation before any benchmark execution. Every later candidate build must
use the same frozen baseline configuration so its measured differences remain
attributable to that candidate rather than configuration drift.

## Authority, privacy, and cleanup

OT-093 grants build-only host authority. It grants no flash, erase, device,
phone, BLE, LoRa, key, entropy, signing, installation, account, upload,
distribution, implementation, or physical-test authority. Neither build
accessed hardware. Public evidence contains only categorical results, versions,
byte counts, and digests; it contains no local path, device identifier, address,
key, secret, PIN, packet content, or private trace.

The two accepted ignored build trees and their isolated Python-cache siblings
were removed after validation. Their absence is cleanup evidence, not part of
the canonical build claim.

## Progress and next gate

This deterministic build-only baseline adds no scored implementation or
physical evidence. Android remains 60%. V1 Companion remains exact 43.75% and
displayed 44%. The historical standalone baseline remains exact 31.75% and
displayed 32%. V1.5 remains unmeasured.

Next, make the OT-005 plan genuinely `ready` for the final candidate set and
applicable target/radio configuration, then execute the exact candidate
benchmark. Only a later explicit decision may accept the suite/library,
handshake/KDF, and packet-v1 wire selection before secure-LoRa implementation.
