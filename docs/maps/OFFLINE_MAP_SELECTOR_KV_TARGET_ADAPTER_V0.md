# Offline Map Selector Key/Value Target Adapter v0

Status: host-tested backend-neutral adapter and ESP-IDF implementation plan,
2026-08-11. No ESP-IDF backend or on-device result exists.

This boundary maps the abstract two-slot
[`OTM0/v0` store](OFFLINE_MAP_SELECTOR_STORE_V0.md) onto two exact 64-byte
key/value blobs without choosing an unreceived display board or claiming flash
durability. It is deliberately below the selector lifecycle coordinators and
above a future ESP-IDF NVS implementation.

## Fixed storage identity

| Item | Fixed v0 value | Purpose |
| --- | --- | --- |
| Candidate NVS partition label | `ot_state` | Isolate OpenTrail state from map packages and unrelated application data |
| Namespace | `ot_maps` | Isolate selector keys inside that partition |
| Slot A key | `otm_sel_a` | Exact 64-byte selector record A |
| Slot B key | `otm_sel_b` | Exact 64-byte selector record B |

The adapter passes those names on every backend operation and rejects any slot,
pointer, size, marker offset, or marker value outside the v0 contract. Both
keys must contain exactly 64 bytes; a different blob length is I/O failure, not
an empty or recoverable selector.

The `ot_state` label is a contract name, not a committed partition size,
offset, encryption mode, or flash layout. Those require the exact target,
pinned ESP-IDF version, complete state inventory, and endurance budget.

## ESP-IDF NVS mapping

ESP-IDF documents NVS as key/value flash storage, with namespaces and binary
BLOB values. `nvs_set_blob()` stages a value and requires `nvs_commit()` for a
guaranteed non-volatile update; `nvs_erase_key()` likewise may not update
storage until commit. See the official
[ESP32-S3 NVS guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/storage/nvs_flash.html)
and its [`nvs_set_blob`](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/storage/nvs_flash.html#_CPPv412nvs_set_blob12nvs_handle_tPKcPKv6size_t)
and [`nvs_commit`](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/storage/nvs_flash.html#_CPPv410nvs_commit12nvs_handle_t)
contracts.

A future backend must map the four abstract operations as follows:

1. `read_slot`: `nvs_get_blob` from the exact key and require returned length
   64; `ESP_ERR_NVS_NOT_FOUND` is distinct from every other error.
2. `write_slot`: `nvs_set_blob` with the complete prepared 64-byte record,
   requiring byte 59 is zero, followed by `nvs_commit`; no success may be
   returned before commit succeeds.
3. `commit_slot`: read the exact prepared blob, require byte 59 is zero, change
   only byte 59 to `0xA5`, then rewrite the complete 64-byte blob and call
   `nvs_commit`. NVS is not treated as an in-place byte writer.
4. `erase_slot`: `nvs_erase_key` for only the selected key followed by
   `nvs_commit`; already missing is idempotent success.

The existing store performs exact readback/decode after marker commit. The
[service-reseed coordinator](OFFLINE_MAP_SELECTOR_RESEED_COORDINATOR_V0.md)
also reads both keys after erase and refuses a reported-success clear if either
key remains.

The backend must never use `nvs_erase_all`, erase the partition, touch map
package bytes, or share a handle containing unrelated pending changes. One
task/lock must own the handle and the complete coordinator operation. The host
generation recheck is not a synchronization primitive.

## Error and uncertainty rules

- Exact missing-key results map to `not_found` only for reads. Erasing a
  missing key is successful empty state.
- Invalid native arguments and binding errors fail without a commit.
- Type, length, space, initialization, handle, corruption, flash, and commit
  errors fail as storage I/O unless a later exact target adapter can preserve a
  safer, more specific distinction.
- A commit call may have reached storage even when its caller receives an
  error. The upper store already treats marker-commit failure as uncertain and
  requires inspection/reconciliation rather than retrying blindly.
- No adapter error may be converted into defaults, an empty first boot, or map
  exposure.

ESP-IDF states that NVS is designed to recover across sudden power loss, while
also warning that unstable power can cause erase verification/page exhaustion
problems. `CONFIG_NVS_FLASH_VERIFY_ERASE` is available to verify physical page
erase, but selecting it is a target configuration decision—not host evidence.

## Security and service authority

CRC detects accidental record corruption; it does not authenticate a record or
operator. NVS encryption is a separate target option. The official
[NVS encryption guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/storage/nvs_encryption.html)
describes encrypted NVS, while the NVS guide explicitly notes that encryption
does not prevent erasure.

Before setting all five reseed acknowledgement booleans, a target service
workflow must independently provide:

1. an authenticated and authorized local service session;
2. explicit identification of the device being serviced without publishing a
   reusable device identifier;
3. a clear statement that maps will be temporarily unavailable;
4. confirmation that only `otm_sel_a` and `otm_sel_b` are in scope;
5. independent evidence that retained package bytes are still available;
6. review of the protected/trusted generation source; and
7. one atomic outcome record for success, service required, or reconciliation
   required without paths, keys, locations, or credentials.

The five booleans remain caller-supplied intent evidence, not an authenticated
command. Radio-delivered reset/reseed is prohibited by this v0 plan. Exact USB,
local display/input, recovery, expiry, challenge, and audit mechanisms remain
open until the target and security model are selected.

## Physical acceptance matrix

Before calling an ESP-IDF implementation durable or production-ready, capture
restart evidence at each boundary:

- before/during/after prepared `nvs_set_blob` and its `nvs_commit`;
- before/during/after full-blob marker rewrite and its `nvs_commit`;
- after reported commit failure where the new record may exist;
- before/during/after each slot erase and commit;
- between slot A and slot B erase during service reseed;
- after verified empty state but before/during new baseline save;
- with one missing, invalid, uncommitted, unreadable, or conflicting key;
- with insufficient NVS space, corrupted/truncated partition state, and native
  initialization recovery;
- across repeated saves/resets sufficient to justify a wear budget; and
- during firmware rollback/update and USB/local recovery.

Every result must record exact board/revision, flash part and layout, ESP-IDF
commit/version, sdkconfig, compiler, security configuration, firmware commit,
power-interruption method, operation boundary, native error, observed slot
states, and safe restart result. Public evidence must remain aggregate and
free of serial ports, MACs, credentials, exact device identifiers, and package
paths.

## Current evidence and limits

Ten deterministic host groups cover fixed binding names, argument validation,
exact/missing/wrong-sized reads, write-plus-commit ordering, staged-write and
commit failure, full-blob marker rewrite, malformed/already-committed refusal,
idempotent committed erase, and composition with normal two-slot save/rotation
plus verified clear. All nine map suites pass 100/100 repeats, and the complete
67-executable host matrix passes under strict C++17 warnings-as-errors.

This proves only adapter semantics against a deterministic fake. It does not
provide an ESP-IDF component, partition table, NVS handle, target lock/task,
encryption or trusted-generation source, real service authorization, physical
atomicity/endurance/power-loss evidence, package storage, renderer, display, or
on-device map result.
