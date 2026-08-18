# Decision 0022: Read-only protected-root inventory admission

- Status: Accepted for offline planning and supplied-evidence verification only
- Date: 2026-08-18
- Work item: OT-079

## Context

Decision 0021 identifies an ESP32-S3 custom user-eFuse thermometer as a conditional rollback-floor provider class. It does not identify an eFuse block, bit range, or capacity and does not authorize any physical operation. Those facts cannot be selected safely from board-family assumptions or rewritable-flash evidence.

Before a later decision can select an exact protected root, OpenTrail needs a bounded inventory contract that states what evidence would be required, how it would be kept private, and which unknown or mismatched states deny admission. Defining and validating that contract offline must not itself become authority to connect to, reset, read from, or change a device.

## Decision

OpenTrail accepts the OT-079 protected-root inventory plan and pure supplied-evidence verifier as an offline-only contract.

The plan may describe a future, separately authorized, read-only observation of an exact ESP32-S3 unit. It does not authorize that observation. Initially, every device-access, connection, port-discovery, reset, bootloader, ROM-command, security-metadata, key-metadata, eFuse-read, raw-dump, key-material, eFuse-write, eFuse-burn, protection-change, provider-admission, provisioning, and runtime-integration authority is false.

The candidate provider remains `ESP32S3_CUSTOM_USER_EFUSE_THERMOMETER`. Its exact block, first bit, and capacity remain absent. It is not selected, admitted, provisioned, active, or available to runtime code.

## Future evidence boundary

A future inventory requires a new, explicit owner authorization for one bounded read-only operation. Its evidence must be fresh and bound privately to the same exact unit, connection, tool identity and version, observation time, operation identifier, and evidence set. Stale, cross-operation, incomplete, unknown, or mismatched evidence denies admission.

The bounded inventory may describe only the metadata necessary to review conflicts and recovery assumptions: chip family and revision, coding scheme, security-feature states, download and debug restrictions, `SECURE_VERSION` state and capacity, eFuse block purposes and protections, HMAC/key-block purpose metadata, and possible custom user-region availability. It must never collect or expose secret key material, BLE PINs, private pairing identities, authorization secrets, or an unbounded raw device dump.

The expected security state remains the state admitted by OT-077: secure boot disabled, flash encryption disabled, and secure download disabled. A future observation must establish those facts freshly within its own operation. Unknown values or a mismatch deny the route and require a new decision; the inventory operation may not change the device to make it match.

## Offline verifier boundary

An offline verifier may evaluate supplied evidence only. It has no command, device-I/O, eFuse, logging, reset, or runtime API. Missing, unknown, stale, wrongly bound, internally contradictory, or secret-bearing evidence denies.

The strongest possible passing result is `PRIVATE-INVENTORY-CAPTURED-SELECTION-PENDING`. A complete inventory remains reviewable even when it proves that no viable provider allocation exists; suitability is a later decision, not a prerequisite for truthful capture. A pass does not select an eFuse field, admit a provider, authorize a write, authorize provisioning, or enable runtime integration.

## Privacy and publication

Exact unit and transport identifiers, raw eFuse output or bitmaps, exact per-unit block mappings, and the operation journal remain private. Public evidence may contain only the plan identity, an offline-plan result, and a fixed sanitized admission or denial category.

The accepted offline plan supports no hardware, factory-state, allocation, irreversible-action, or runtime claim. It does not change project scores.

## Consequences

- OT-079 can be reviewed and tested deterministically without a connected device.
- A later read-only inventory remains blocked until the owner grants fresh, explicit authority.
- Exact provider allocation and all irreversible work require later independent decisions.
- Any security-state mismatch closes the current OT-077/OT-079 route instead of causing an automatic repair or reconfiguration.
