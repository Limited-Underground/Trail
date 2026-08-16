# Companion authorization protected storage v0

Status: OT-054 host-tested persistence prerequisite plus OT-063 target-linked
read-only admission probe; target admission denied; 2026-08-16.

This contract defines the smallest durable boundary required for the one-phone
authorization authority to survive reboot. It does not claim that ordinary NVS
is secure, rollback-resistant, or ready for production.

## Durable record

`OAP0/v0` is exactly 32 bytes:

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| 0 | 4 | ASCII `OAP0` |
| 4 | 1 | version `0` |
| 5 | 1 | state: unowned tombstone or owned |
| 6 | 2 | reserved, zero |
| 8 | 4 | nonzero little-endian owner generation |
| 12 | 16 | opaque 128-bit owner token, or zero for unowned |
| 28 | 4 | CRC-32 over bytes 0 through 27 |

The CRC detects accidental format corruption only. It is not a MAC,
authenticity proof, or rollback defense. An owned record requires a nonzero
owner token; an unowned tombstone requires a zero token. Revoke, replacement,
and reset advance generation rather than erasing history into an ambiguous
absence.

## Protected-store contract

The injected backend must freshly read both the complete record and an
independently rollback-resistant generation floor. Commit must compare the
expected generation against fresh protected state, atomically publish the
complete candidate and new floor, and return an exact verified readback.

`failed` is valid only before durable mutation is possible. Every error after a
write may have occurred is `uncertain`; authorization then remains faulted and
closed until trusted recovery. Absence with a nonzero floor, a record/floor
mismatch, stale compare, corrupt record, or readback mismatch also fails
closed. The interface is single-owner and externally serialized; it supplies
no concurrent-task primitive.

## Private bond binding

The trusted bond store supplies an opaque private 16-byte reference and nonzero
bond generation. That reference is not a BLE address, public identifier,
peer-provided value, or raw key. A separately provisioned device-secret PRF
derives the stable opaque 128-bit controller token from a domain-separated
message containing only the private reference and generation. Re-pairing must
allocate a new private reference or advance its generation. No uniqueness or
security claim follows from a public peer address.

The PRF seam is intended for a protected backend such as an independently
provisioned ESP32-S3 `HMAC_UP` eFuse key. Raw key material never crosses the
interface. Input and derived scratch buffers are explicitly cleared on every
post-PRF exit. No token, private reference, raw key, address, public identifier,
or peer name is logged or added to public status.

## Current target admission

The generic `heltec_v4_bench` target separates configuration from runtime
proof. `CONFIG_NVS_ENCRYPTION` or an HMAC key-selection option can show only
configuration intent; neither proves that the selected partition was securely
initialized nor that an eFuse key is provisioned, read-protected, distinct,
and usable.

Admission requires all of the following:

- NVS encryption configured and the protected partition initialized and
  verified;
- HMAC key protection configured and its selected key verified provisioned,
  protected, and usable;
- a private bond-reference store that changes identity on re-pair;
- a separate verified binding-PRF key;
- an atomic record-plus-floor compare/commit/readback backend; and
- an independently rollback-resistant generation floor.

The current target satisfies none of the runtime proof gates and has NVS
encryption disabled. Preflight therefore returns
`nvs_encryption_not_configured`. OT-063 adds a target-linked read-only probe,
but the current configuration short-circuits before any partition, eFuse, or
HMAC read. A future reviewed configuration can only inspect the named NVS
partition and a separately selected HMAC_UP key's purpose, read protection, and
private operational self-test. The probe has no NVS initialization/open/write,
key-generation, eFuse-programming, bond-resolution, or GATT-admission surface.
OT-056 separately codes NimBLE startup/registration/advertising, but the denied
preflight remains latched before host start, every connection is immediately
terminated, and no claim or normal command is admitted. The target-local reboot
self-check continues to use deterministic in-memory fakes only.
## Evidence and exclusions

Seventeen strict host groups pass at 100/100, including exact vectors,
corruption/coherence, power-loss ambiguity, rollback, reentry, private-binding,
re-pair, owner restore, second-controller denial, replacement, revoke, reset,
and faulted publication. The complete OT-054-era native matrix passes 122 enabled
executables. Two pinned ESP-IDF v6.0.2 builds reproduce a 175,701-byte image and
175,824-byte BIN with SHA-256
`D39430096B7BEDD0F69D9ECCDE2424EDCD635C0BEA904EB2E4FCA3EEED307080`.
See [OT-054 evidence](../../tests/hardware/OT-054-2026-08-15.md).

This is `BUILD-LINKED-NOT-RUN` evidence. It does not prove protected NVS,
eFuse provisioning, physical-presence input, live bonding, application
authorization, BLE startup, persistence across a physical power loss, device
runtime, or support. No target was selected, opened, written, or flashed by
OT-054.

OT-056 raises only the surrounding build/runtime-owner evidence: all 123 native
entries, 13 owner groups at 100/100, target self-check 100/100, static 3/3,
pinned NimBLE teardown/stop ordering, and two reproducible builds pass. It does
not change any storage admission result or provide physical runtime evidence;
see [OT-056 evidence](../../tests/hardware/OT-056-2026-08-15.md).

## OT-063 read-only admission probe

OT-063 adds typed, ordered observations for the prerequisite configuration,
named NVS partition, selected HMAC_UP key purpose, key read protection, and one
bounded operational HMAC self-test. Six strict host groups cover every missing
or invalid stage, exact short-circuit ordering, the successful observation-only
path, and the still-denied target admission. The deterministic target boot
self-check remains green at 100/100; target-only static admission passes five
groups; and two pinned ESP-IDF v6.0.2 builds reproduce identical artifacts.

This is `BUILD-LINKED-NOT-RUN` evidence. The build is not flashed. The accepted
OT-061 device still runs the prior experimental image. No partition layout,
sdkconfig selection, HMAC key ID, eFuse, NVS contents, bond, authorization, GATT
exchange, or device state changed. The current first denial remains
`nvs_encryption_not_configured`; private bond storage, a distinct binding-PRF
key, protected atomic record/floor storage, and an independent rollback floor
remain absent. See [OT-063 evidence](../../tests/hardware/OT-063-2026-08-16.md).
