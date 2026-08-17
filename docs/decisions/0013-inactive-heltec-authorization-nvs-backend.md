# Decision 0013: Inactive Heltec Authorization NVS Backend

Status: accepted for build compilation and host validation, 2026-08-17.

## Decision

OpenTrail will compile one target-local ESP-IDF NVS adapter for the exact
Decision 0012 authorization slot-media contract without injecting it into the
Heltec runtime. The adapter receives an already-opened `nvs_handle_t`; it does
not initialize, open, close, erase, reset, repair, retry, provision, or log.

Every operation revalidates the fixed candidate binding:

- partition `ot_auth`;
- namespace `ot_owner`;
- keys `oap_slot_a` and `oap_slot_b`; and
- exact 32-byte values.

Reads query the native size before one exact read. Missing keys remain absent;
an inexact size or native ambiguity publishes no record bytes. Writes stage one
exact value and require a separate `nvs_commit`. Any invoked write or commit
failure is uncertain.

## Build boundary

The Heltec component compiles both the target adapter and the target-neutral KV
slot media against ESP-IDF v6.0.2 and `nvs_flash`. This proves that the admitted
API compiles in the real target toolchain. It does not prove that a protected
partition or handle exists at runtime.

The active partition table has no `ot_auth` entry, NVS encryption is not
enabled, and no runtime source includes or constructs this adapter. The current
denied storage, binding, and authorization authorities remain unchanged.

## Deferred authority

Selecting or opening a protected NVS partition, provisioning encryption or PRF
keys, selecting an independent rollback floor, migrating records, pairing,
authorizing GATT, entering Ready, writing firmware, or changing a physical
device requires a later separately accepted increment. This decision grants no
such authority.
