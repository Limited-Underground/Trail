# Traffic-Key Derivation Context v0

Status: host-tested public context encoder, not a KDF, 2026-08-10

## Purpose

OpenTrail needs sender-specific traffic keys and separate derived outputs for
the AEAD nonce prefix and durable counter-domain identifier. This contract
provides one canonical, unambiguous public context for a future audited KDF
adapter. It binds the authoritative group, epoch, full sender fingerprint, and
one explicit output purpose.

The encoder never accepts an epoch secret or emits key material. It performs no
hashing, HKDF, encryption, signing, nonce construction, or library selection.

## Canonical 52-byte context

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII `OTKD` |
| 4 | 1 | Context version `1` |
| 5 | 1 | Purpose: `1` group AEAD key, `2` nonce prefix, `3` counter-domain ID |
| 6 | 1 | Reserved zero |
| 7 | 1 | Context length `52` |
| 8 | 8 | Nonzero authoritative group ID, network byte order |
| 16 | 4 | Nonzero group/key epoch, network byte order |
| 20 | 32 | Nonzero authoritative sender identity fingerprint |

Group ID and epoch use network byte order so every target produces identical
KDF input. The full 32-byte fingerprint is used; a display name or 64-bit
network alias is intentionally insufficient for key separation.

Each derived output uses a different purpose byte. The future crypto adapter
must not derive one output and truncate or reinterpret it as another.

## Failure behavior

Zero group ID, zero epoch, an all-zero sender fingerprint, or an unknown purpose
returns a typed error and an all-zero context. There is no default group,
fallback purpose, name/alias substitution, or partial encoding.

## Host evidence

Eight deterministic scenario groups cover the exact canonical layout, all
three purpose domains, group/epoch/sender separation, and fail-closed zero or
unknown inputs.

## Remaining cryptographic gate

The exact-target benchmark must still select and test the KDF algorithm and
library, define salt/input key material, output lengths, secret lifetime and
wiping, and independent vectors. It must show that epoch rotation changes the
epoch secret and that the 32-bit nonce prefix and 128-bit counter-domain ID are
derived consistently with the matching sender-specific AEAD key. This document
does not authorize packet-v1 or a security claim.
