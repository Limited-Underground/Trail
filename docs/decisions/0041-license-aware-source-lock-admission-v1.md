# Decision 0041: Freeze license-aware source-lock admission v1

- Status: Accepted host-only governance contract; source locks and readiness remain blocked
- Date: 2026-08-20
- Work item: OT-097
- Scope: License-aware revision of candidate source-lock admission

## Decision

OpenTrail accepts strict [`OTCSL0/v1`](../../tests/benchmarks/crypto/OT-097-OT005-LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1.json), admission ID `OT-097-OT005-LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1`, status `license_aware_admission_contract_frozen_host_only`, and canonical/policy SHA-256 `51639e1b9342dc9e501fb0682d044c0f7c05e691e1a26f463358a753f28a123a`.

The accepted public result is:

`LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1-FROZEN-HOST-ONLY; ZERO-SOURCES-ACQUIRED-OR-IMPORTED; OTCBR0-READINESS-BLOCKED`

Version 1 preserves the OTCSL0/v0 evidence-layer and admission boundaries while requiring the upstream SPDX expression, an explicit project license choice, a complete license inventory, and its own SHA-256 digest for any future source-lock acceptance. OTCSL0/v0 remains valid historical governance evidence but is permanently non-admitting for future source-lock acceptance.

The candidate records preserve the reviewed upstream expressions and any current project choice without claiming legal clearance or compatibility: Espressif libsodium `ISC` / project choice `ISC`; ESP-IDF mbedTLS/PSA `Apache-2.0 OR GPL-2.0-or-later` / project choice `Apache-2.0`; and Monocypher `CC0-1.0 OR BSD-2-Clause` / no project choice yet. Every license inventory is incomplete and every inventory digest is null.

## Result and boundary

OT-097 accepted, acquired, and imported zero sources. Every accepted source, API/configuration, and candidate-import registry remains empty. All six OTCBR0 blockers remain open, OTCB0 remains `draft_blocked`, and no source lock or readiness advancement is accepted.

Focused validation passes 17 scenario groups. This decision provides no legal clearance or license-compatibility determination and grants no acquisition, import, benchmark build/execution, device, radio, key/entropy, suite-selection, packet-v1, support, implementation, physical-evidence, or score authority. Android remains 60%; V1 exact 43.75%/displayed 44%; historical baseline exact 31.75%/displayed 32%; V1.5/V2 remain unmeasured.

## Next gate

Produce independently accepted, candidate-specific immutable source, project-lock, API/final-configuration, complete license-inventory, inventory-digest, and import evidence. Close all six readiness requirements before accepting a new executable plan or running a separately authorized benchmark.
