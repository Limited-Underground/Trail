# Decision 0024: Build-only target-side protected-root key-roster adapter

Date: 2026-08-18

Status: Accepted for build and host testing only

## Decision

Implement the OT-080 target-side boundary as a Heltec-local, one-use,
compile-only coarse key-roster adapter. The adapter calls exactly the five
decoded read-only ESP-IDF 6.0.2 APIs admitted by `OTPRR0/v0` for the six logical
key slots `KEY0` through `KEY5`:

- `esp_efuse_get_key_purpose`
- `esp_efuse_get_key_dis_read`
- `esp_efuse_get_key_dis_write`
- `esp_efuse_get_keypurpose_dis_write`
- `esp_efuse_key_block_unused`

It publishes only purpose category, proven-unused state, and the three
protection states after every slot and invariant succeeds. It returns an empty
denial on invalid purpose, contradictory unused evidence, re-entry, or reuse.
Re-entry permanently poisons that adapter instance, and every attempted read is
one-use regardless of outcome.

## Evidence boundary

The adapter is compiled into the target component but has no startup, task,
BLE, storage, command, transport, or other runtime call path. Synthetic host
tests exercise the exact call order and failure boundary. A pinned target build
proves only that the source compiles against the reviewed ESP-IDF declarations.

`esp_efuse_key_block_unused(false)` means only that the slot was not proven
unused. It does not prove a provisioned key. The admitted API set also cannot
prove reservation state. Therefore this adapter does not populate the complete
OT-079 inventory evidence, select a provider, identify a usable key slot, or
authorize a device read.

## Excluded authority

This decision grants no device connection, reset, eFuse read execution, raw
key/block access, HMAC operation, field selection, key generation,
provisioning, protection change, burn/write, protected-storage initialization,
runtime injection, GATT authorization, or Ready authority. It adds no complete
inventory reader/orchestrator and performs no hardware operation.

Configured-NVS conflict, security-state, and rollback-floor observations remain
unavailable. The exact non-secret rollback-floor descriptor remains unselected.

## Validation

- strict warning-as-error host adapter test: five groups;
- target static admission: ten groups;
- complete host validation: 144 executables;
- two pinned ESP-IDF 6.0.2 target builds with identical BIN, ELF, and map
  hashes;
- compiled-object and `BUILD-COMPILED-NOT-RUNTIME-INJECTED` evidence;
- no device access or execution.

See [OT-081 evidence](../../tests/hardware/OT-081-2026-08-18.md).
