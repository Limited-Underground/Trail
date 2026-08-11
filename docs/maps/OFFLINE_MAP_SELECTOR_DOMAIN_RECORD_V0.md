# Offline Map Selector Trust-Domain Record v0

Status: deterministic host-tested canonical record, 2026-08-11. An abstract
recoverable store and bounded provisioner now exist; no protected backend or
on-device persistence exists.

`OTMD/v0` is a separate 80-byte lifecycle record for the map selector's trust
domain. It does not change the fixed 64-byte `OTM0/v0` selector checkpoint.
`OTM0` has only six reserved bytes, so placing a truncated or partial 128-bit
domain there would create an ambiguous compatibility boundary.

## Canonical layout

All integers use little-endian byte order.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `OTMD` |
| 4 | 1 | Version `0` |
| 5 | 1 | Lifecycle state |
| 6 | 1 | Domain origin |
| 7 | 1 | Canonical zero reserved byte |
| 8 | 16 | Nonzero current map trust domain |
| 24 | 16 | Retired domain, or all zero for first commissioning |
| 40 | 8 | Retired/quarantined selector-generation floor |
| 48 | 8 | Accepted selector generation, or zero while pending |
| 56 | 8 | Nonzero domain epoch |
| 64 | 8 | Nonzero record generation |
| 72 | 3 | Canonical zero reserved bytes |
| 75 | 1 | Commit-last marker `0xB6` |
| 76 | 4 | CRC-32 over bytes 0-75 |

The domain values are pseudonymous runtime bindings. They are not credentials,
but target code must keep them out of public logs and UI. The record contains
no device identifier, participant identity, package name, path, geographic
content, URL, credential, key, proof, timestamp, or free text.

## Lifecycle invariants

Fresh-device commissioning uses origin `fresh_device_commissioning`:

- current domain is nonzero;
- retired domain and retired selector floor are zero;
- domain epoch is exactly 1;
- `pending_first_baseline` has accepted selector generation zero; and
- `active` has a nonzero accepted selector generation.

Same-device replacement uses origin `same_device_replacement`:

- current and retired domains are both nonzero and different;
- domain epoch is at least 2;
- `pending_selector_reseed` has accepted selector generation zero; and
- `active` requires the accepted selector generation to be strictly greater
  than the retired/quarantined selector floor.

Unknown origin/state, cross-origin pending state, zero current domain, missing
or reused retired domain, zero epoch/record generation, an accepted pending
record, or an active generation at/below the retired floor fails closed.

The accepted selector generation is intended to be written only after a future
coordinator proves the selector record and protected-generation source agree at
that exact value. This codec does not make that claim itself.

## Integrity and persistence boundary

CRC detects accidental corruption only. It is not authentication, encryption,
anti-rollback protection, or evidence that the current/retired domain chain is
physically durable. The CRC covers the committed marker; a prepared record with
byte 75 cleared is explicitly uncommitted.

The codec performs no read, write, erase, reset, provisioning, selector import,
package operation, logging, network, radio, or UI action. The separate abstract
[two-slot store](OFFLINE_MAP_SELECTOR_DOMAIN_STORE_V0.md) now preserves the
previous committed record while writing a successor, enforces exact lifecycle
transitions and record-generation advance, commits byte 75 last, and verifies
exact readback. The permit-consuming provisioner orders pending-record
persistence before any protected-source or selector mutation.

## Authorization tightening

The [protected-domain authorizer](OFFLINE_MAP_SELECTOR_DOMAIN_AUTHORIZATION_V0.md)
now binds both the proposed and retired domain:

- same-device replacement requires a nonzero retired domain different from the
  proposed domain; and
- fresh-device commissioning requires the retired domain to be all zero.

This prevents an authorization grant from describing “retirement” without
naming the domain being retired, or from smuggling a prior domain into first
commissioning. It still does not prove where either value came from.

## Current evidence and limits

Ten deterministic codec groups cover exact layout, fresh pending/active,
replacement pending/active, zero/reused domains, state/origin coherence,
generation invariants, magic/version/reserved/commit/CRC rejection, argument
and output atomicity, full 64-bit boundaries, and field-sensitive canonical
bytes. The tightened authorization suite covers retired-domain absence, reuse,
new-device contamination, and exact echo.

All twenty-two map suites pass 100/100 focused repeats in the complete
80-executable host matrix under strict C++17 warnings-as-errors.

The codec and abstract store are host evidence only. No authenticated integrity,
rollback-resistant target storage, domain entropy source, device-continuity
proof, target lock/task, ESP-IDF composition, power-loss result, or physical
behavior is claimed. The provisioner and stable activation coordinator prove
bounded common-code ordering only.
