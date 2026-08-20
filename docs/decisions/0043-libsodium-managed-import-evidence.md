# Decision 0043: Record libsodium managed-import evidence

- Status: Accepted evidence; source-lock admission and readiness remain blocked
- Date: 2026-08-20
- Work item: OT-099

## Decision

OpenTrail accepts host-only `OTLMI0/v0`, audit ID
`OT-099-OT005-LIBSODIUM-MANAGED-IMPORT-V0`, raw SHA-256
`8285fa7308bfc83a5d55503a7a3e1fa4c21895a42b095197b3ec75f634411ec9`,
and result:

`ESPRESSIF-LIBSODIUM-1.0.22-MANAGED-IMPORT-EVIDENCE-COMPLETE; SOURCE-LOCK-ADMISSION-PENDING; ISOLATED-COMPUTER-BUILD-PASSED; NO-DEVICE-OR-CRYPTO-EXECUTION; OTCBR0-READINESS-BLOCKED`

The evidence binds the exact Espressif registry component, component hash,
managed dependency lock, 733-entry source manifest, registry checksums,
two-file ISC license inventory, candidate-scoped SPDX record, managed
dependency inventory, and empty patch set. These layers are complete evidence
for later review, but the controlling `OTCSL0/v1` accepted-source registries
remain empty. The source lock is therefore not accepted.

An isolated generic ESP32-S3 computer build passed. The probe compiled, the
candidate archive built and entered the link graph, and the application ELF
linked. Linker garbage collection removed the uncalled probe symbols, so the
result does not claim candidate symbols or code were retained in the final
image. It proves neither exact received-target compatibility nor final
candidate configuration.

All six readiness blockers remain open. No device, flash, radio, cryptographic
execution, key/entropy, benchmark, candidate or suite selection, packet-v1,
physical evidence, or score authority is granted. Android remains 60%; V1
remains exact 43.75% and displayed 44%; the historical baseline remains exact
31.75% and displayed 32%; V1.5 and V2 remain unmeasured.

## Next gate

Independently admit the complete evidence through a new license-aware source-
lock revision before closing the libsodium source-lock blocker. API/final-
configuration eligibility, retained integration, benchmark execution, and
selection remain separate gates.
