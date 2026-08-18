# Decision 0023: Offline target-side protected-root inventory reader route

- Status: Accepted for offline route planning only
- Date: 2026-08-18
- Work item: OT-080
- Route schema: `OTPRR0/v0`

## Context

OT-079 accepted the evidence envelope and fail-closed verifier for a possible future read-only protected-root inventory. It did not provide a reader or authorize device access.

Source review found that the official host Python `EspEfuses` model reads and materializes every eFuse block, including raw bytes from KEY0 through KEY5, in host memory and can expose raw values through summary or JSON paths. That behavior violates OpenTrail's bounded normalized-metadata and private-key custody boundary even when no write is requested. The pinned Python sources are therefore rejection evidence, not an admitted implementation route.

## Decision

OpenTrail rejects host Python/espefuse inventory execution, including full summary, JSON, and raw-value routes.

OpenTrail accepts a narrower offline design boundary for a future target-side ESP-IDF 6.0.2 metadata adapter. The accepted official ESP-IDF source identities are pinned by full SHA-256 in `protected-root-inventory-reader-plan.json`.

No executable reader exists. `reader_present` remains false. No command, attempt, connection, unit identifier, port identifier, operation identifier, or output path is defined or claimed.

Every owner, reader-implementation, execution, device-access, connection, port-detection, reset, bootloader, ROM-command, security-read, key-read, eFuse-read, raw-dump, key-material, write, burn, protection-change, field-selection, provider-admission, provisioning, and runtime-integration authority remains false.

## Accepted target-side API boundary

A later adapter may use only these decoded read-only ESP-IDF APIs:

- `esp_efuse_get_key_purpose`
- `esp_efuse_get_key_dis_read`
- `esp_efuse_get_key_dis_write`
- `esp_efuse_get_keypurpose_dis_write`
- `esp_efuse_key_block_unused`

The adapter must execute directly in the target process. Shell, subprocess, external-command, stub, retry, and reset routes remain forbidden.

The following surfaces are explicitly outside the route:

- host Python `EspEfuses`, full summary, JSON, or `raw_value` output;
- `esp_efuse_get_key`;
- `esp_efuse_read_block` or `esp_efuse_read_field_blob` for any key block;
- HMAC self-tests or any operation that exercises a secret;
- every eFuse write, burn, or protection-change API; and
- raw key, raw block, or raw rollback-floor bitmap material.

## Metadata and floor boundary

The future adapter may normalize decoded metadata categories for six logical key slots, NVS protection, the rollback-floor review, and security state. Raw key bytes, block contents, bitmaps, exact device or transport identifiers, public operation identifiers, and paths never enter public output.

No exact rollback-floor descriptor exists. Its block, first bit, and capacity remain null, and a floor device read remains unavailable. The provider is unselected, unadmitted, unprovisioned, inactive, and unavailable to runtime code. A later descriptor decision is required before any bounded floor read can be designed.

The fixed sanitizer emits only the public strings enumerated in the route plan. Unknown, unexpected, incomplete, mixed, or unsanitizable input fails closed.

## Future connection and cleanup boundary

The offline route describes at most one future connection and one attempt using the no-reset detecting path. It does not authorize either. A stub, retry, automatic or manual reset, boot, or reset-on-close behavior is outside this route. Every future exit must close the transport without mutating the device.

## Consequences

- Unsafe host materialization is rejected before it can become an operational shortcut.
- The accepted source identity and target-side API allowlist can be audited offline.
- The next engineering gate is a target-side adapter implementation tested only against synthetic metadata.
- Device execution still requires a later explicit owner decision and a privately bound one-use operation.
- An adapter or verifier pass cannot select a field, admit a provider, authorize an eFuse operation, enable provisioning, or change runtime behavior.
- No hardware, factory-state, allocation, irreversible-action, reader-execution, or runtime claim is made.
- Project scores remain 39.75 weighted/displayed 40, with the historical baseline 31.75 weighted/displayed 32.
