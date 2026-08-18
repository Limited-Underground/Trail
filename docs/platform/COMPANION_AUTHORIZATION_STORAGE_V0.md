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

## OT-065 reversible two-slot composition

OT-065 adds a concrete implementation of the existing protected-store
interface over two injected authorities: slot media and an independent
monotonic generation floor. Load reads the floor before media and admits only
one exact current slot with the other absent or older. Commit prepares the
inactive slot, reads and decodes it back exactly, compare-advances the floor,
then rereads the floor and both slots before reporting success. A failure after
the prepared write is treated as uncertain; the implementation does not erase,
repair, retry, or silently select a record.

Twelve strict host groups cover empty media, exact load, stale-only,
prepared-ahead, duplicate-current, corrupt, conflicting and ambiguous states,
inactive-slot selection, write/readback faults, compare-advance rejection, and
final reconciliation. The Heltec target also carries an exact inactive
`OTPS0/v0` candidate layout plus a design-only provisioning plan. Neither file
changes the accepted `partitions.csv`, `sdkconfig.defaults`, target contract,
or runtime composition.

This is reversible build/host evidence only. No protected NVS backend,
encryption key, distinct binding-PRF key, independent rollback-floor provider,
migration path, reset/recovery procedure, eFuse operation, physical write,
GATT authorization, or Ready state is selected or authorized. See
[Decision 0010](../decisions/0010-reversible-companion-protected-storage-foundation.md)
and [OT-065 evidence](../../tests/hardware/OT-065-2026-08-17.md).

## OT-067 exact protected KV slot media

OT-067 binds the two record slots to injected exact key/value operations using
only `ot_auth` / `ot_owner` / `oap_slot_a|b`. Present values are exactly 32
bytes, missing values are absent, and staging requires a separate commit. The
adapter publishes no bytes after malformed reads, reentry, or ambiguous writes.
The existing two-slot coordinator retains reread and rollback-floor authority.
See [Decision 0012](../decisions/0012-protected-authorization-kv-slot-media.md)
and [OT-067 evidence](../../tests/hardware/OT-067-2026-08-17.md).

## OT-068 inactive ESP-IDF NVS binding

OT-068 adds a target-local ESP-IDF implementation of that exact blob backend.
It accepts an already-opened handle and exposes only get/set/commit. Eight
strict groups prove exact binding, size, missing-value, error, write, commit,
and ambiguity behavior; the ten-group slot-media regression remains green.
Two target builds reproduce the same 470,928-byte BIN with SHA-256
`9F5AFB320A015E3BFFD866A9EE31F76198739521FA7519845ACDA12B9B52BAE5`.

This is compilation evidence only. No runtime source constructs the backend;
the active target still has no `ot_auth` partition or NVS encryption. No key,
floor, bond, pairing, authorization, Ready, device write, or physical storage
evidence is added. See
[Decision 0013](../decisions/0013-inactive-heltec-authorization-nvs-backend.md)
and [OT-068 evidence](../../tests/hardware/OT-068-2026-08-17.md).

## OT-069 inactive existing-context owner

OT-069 adds the target-local lifecycle above OT-068. It admits only the fixed
encrypted candidate partition and namespace, consumes an existing security
configuration, zeroes the temporary native structure immediately after secure
initialization is invoked, and exposes the backend only after exact success.
Native ambiguity or reentry releases acquired resources and latches closed.
Normal destruction closes the handle before partition deinitialization.

Ten strict groups plus a disabled-configuration zero-I/O executable pass, as
do nine target-admission groups and two reproducible target builds. This owner
is not runtime-injected. The active layout/configuration cannot satisfy its
preconditions, and no key, floor, bond, pairing, authorization, Ready, device
write, or physical storage evidence is added. See
[Decision 0014](../decisions/0014-inactive-heltec-authorization-nvs-context.md)
and [OT-069 evidence](../../tests/hardware/OT-069-2026-08-17.md).

## OT-070 partition-transition admission

OT-070 adds a pure target-neutral evaluator for the exact protected-storage
layout transition. It requires fresh installed-layout readback, verified blank
source media or a separately implemented and verified semantic migration,
exact recovery evidence, no runtime/key/eFuse/other-flash request, and one
operation-scoped partition-only authority. It performs no I/O, retains no
source bytes, and publishes only the first ordered denial.

The Heltec `OTPST0/v0` manifest requires a future exact all-`0xFF` proof over
the complete 1 MiB source region with zero retained bytes. That proof alone
does not authorize promotion. Before any authorization commit, restoration of
the old table is conditional and separately authorized; after a commit it is
forbidden. Partition recovery is not the independent generation floor.

Thirteen strict C++ groups pass across 100 repeats; five manifest groups, the
existing target admission, and the complete host gate pass. No active target,
runtime, image, device, key, eFuse, storage, GATT, or Ready state changes. See
[Decision 0015](../decisions/0015-safe-heltec-protected-storage-partition-transition.md)
and [OT-070 evidence](../../tests/hardware/OT-070-2026-08-17.md).

## OT-071 read-only transition evidence

OT-071 adds a streaming offline verifier for the two exact physical artifacts
needed by the OT-070 source-proof gate: the 3,072-byte installed partition
table and the complete 1 MiB source region. The table must match the accepted
OTHP0 binary, and every source byte must be `0xFF`. Both observations are bound
to nonzero operation and evidence-set identities, but output is reduced to a
fixed schema and sanitized outcome. Raw bytes, paths, ports, identities, and
nonblank details are not retained.

The associated physical-read plan remains denied, selects no unit, and grants
no read or mutation authority. It contains no executable hardware reader. A
later executor must be one-use, privately bound to the exact unit, operation,
evidence set, and port, and must remove task-private temporary artifacts before
emitting a result. No physical read occurred in
OT-071, and a satisfied result proves only the source prerequisite; it cannot
authorize partition promotion, protected-storage activation, keys, eFuses,
GATT authorization, or Ready. See [Decision 0016](../decisions/0016-read-only-protected-storage-transition-evidence.md)
and [OT-071 evidence](../../tests/hardware/OT-071-2026-08-17.md).
## OT-078 offline protected-root providers

OT-078 selects only provider classes. Two distinct ESP32-S3 `HMAC_UP` eFuse
blocks are required for the `ot_auth` NVS-encryption role and the private
bond-binding PRF role. Factual admission requires exact provisioning, purpose,
read protection, operational self-test, freshness, distinctness, and one shared
operation/evidence binding; the configured NVS block may match only the NVS
role.

The independent rollback floor conditionally uses a dedicated custom user-eFuse
thermometer field. Canonical bits are a contiguous low-order set prefix followed
only by unset bits. An advance is exactly one bit and requires an exact reread.
Holes, unknown width/protection, exhaustion, and possible post-burn ambiguity
keep authorization closed. Reset never lowers the floor, prepared-ahead records
are not published, and a floor ahead of the record requires forward recovery.

Exact physical blocks, floor field, capacity, inventory, provisioning, runtime
injection, and every key/eFuse/device authority remain absent. See
[Decision 0021](../decisions/0021-offline-protected-root-provider-selection.md)
and [OT-078 evidence](../../tests/hardware/OT-078-2026-08-18.md).

## OT-079 offline protected-root inventory admission

OT-079 defines the supplied-evidence boundary for a possible later private
read-only inventory. The pure evaluator requires one fresh operation/evidence
binding, a complete six-slot key roster with provisioning and protection facts,
configured-NVS conflict state, complete candidate-floor facts, cleanup, and the
disabled secure-boot, flash-encryption, and secure-download state required by
OT-077. Unknown, incomplete, contradictory, stale, mixed, or secret-bearing
evidence denies.

A complete inventory may truthfully show that no viable allocation exists and
still pass for private review. That result remains selection-pending and grants
no device access, allocation, provider admission, provisioning, eFuse write, or
runtime authority. OT-079 includes no device reader. See
[Decision 0022](../decisions/0022-read-only-protected-root-inventory-admission.md)
and [OT-079 evidence](../../tests/hardware/OT-079-2026-08-18.md).
