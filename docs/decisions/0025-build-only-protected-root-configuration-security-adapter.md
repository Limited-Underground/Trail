# Decision 0025: build-only protected-root configuration/security adapter

- Status: Accepted for offline implementation evidence only
- Date: 2026-08-18
- Increment: OT-082

## Decision

Add one target-local, one-use source that normalizes the default build's NVS
encryption/protection selection and four decoded ESP32-S3 security-state values.
The source compiles into the Heltec target but has no dependency edge from
startup, BLE, storage, commands, or any other runtime composition.

The NVS portion reports only the exact default build configuration selected by
the five pinned ESP-IDF Kconfig symbols. It does not prove that no runtime
security scheme override exists, and it does not resolve a configured-key
conflict. The current target build is truthfully normalized as NVS encryption
disabled with no configured HMAC key slot.

The security portion calls exactly, in order:

1. `esp_secure_boot_enabled()`;
2. `esp_efuse_is_flash_encryption_enabled()`;
3. `esp_efuse_read_field_bit(ESP_EFUSE_ENABLE_SECURITY_DOWNLOAD)`;
4. `esp_efuse_read_field_bit(ESP_EFUSE_DIS_DOWNLOAD_MODE)`.

The two download controls remain distinct. A complete but unfavorable set of
values remains factual metadata for later private review; this leaf does not
adjudicate recovery compatibility or provider suitability.

## Failure and privacy boundary

The source is one-use, all-or-none, and poisoned by re-entry. Invalid build
configuration or re-entry publishes only a default denial and cannot retry. It
contains no serializer, path, port, identity, log, raw key, raw block, raw field
blob, HMAC, NVS initialization, write, burn, protection, reset, repair, or
transport surface.

The source and its tests may not be described as a device observation. Until a
separate operation is approved and executed, all device security values remain
unknown and all read, provisioning, provider, write, and runtime authority
remains false.

## Consequences

OT-082 closes only the build-tested source boundary for default NVS
configuration and security-state metadata. It does not create a complete
inventory reader or mapper. Provisioning/reservation facts, configured-NVS
conflict composition, runtime-override proof, and the rollback-floor descriptor
remain missing. V1 completion does not change for build-only plumbing.

Any later device execution requires a separately accepted reader composition,
fresh one-use owner authority, private operation/evidence binding, and a new
hardware result.
