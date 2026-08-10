# AEAD Nonce Composition v0

Status: host-tested composition boundary, not packet-v1 cryptography, 2026-08-10

## Purpose

This component closes the mechanical boundary between a rollback-safe outbound
counter lease and an IETF ChaCha20-Poly1305 candidate's 96-bit nonce. It makes a
caller prove that the durable lease and the traffic key belong to the same
nonzero 128-bit domain before composing any nonce.

It does not derive a traffic key or nonce prefix, allocate counters, perform
AEAD, define packet-v1, or select a cryptographic library. Those remain behind
the exact-target benchmark gate.

## Contract

`compose_aead_nonce()` accepts:

- the nonzero 128-bit domain attached to the durable counter lease;
- the nonzero 128-bit domain attached by the crypto adapter to the traffic key;
- the crypto-adapter-supplied 32-bit key-domain prefix; and
- one nonzero rollback-safe 64-bit counter.

The two full domain identifiers must match exactly. On success, the 12-byte
nonce is:

| Offset | Size | Value |
| ---: | ---: | --- |
| 0 | 4 | Key-domain prefix, byte-exact |
| 4 | 8 | Counter, unsigned network byte order |

Network byte order is fixed to eliminate target-dependent integer layout and
make vector comparison straightforward. A zero prefix is allowed because it can
be a valid KDF output; uniqueness is a property of the complete key/domain/
counter construction, not a rule that individual prefix bytes be nonzero.

## Failure behavior

The function returns an all-zero output and a typed error for:

- either full domain identifier being all zero;
- a lease/key domain mismatch; or
- counter zero.

It has no fallback, truncation, random generation, counter allocation, or alias-
based domain inference. A failure must stop protected-packet creation.

## Host evidence

Seven deterministic scenario groups cover exact canonical bytes, consecutive
counters, a different key-domain prefix, a valid zero prefix, domain mismatch,
zero domains, counter zero, and the maximum 64-bit counter.

## Remaining security gates

The target crypto adapter must still define and benchmark a domain-separated
KDF for the sender-specific traffic key, 32-bit nonce prefix, and full 128-bit
counter-domain identifier. It must bind authoritative group, epoch, sender, and
key purpose; prove independent-vector interoperability; and prevent reuse when
keys or epochs change. Protected rollback-resistant target storage, real AEAD,
packet-v1 AAD/codec, replay handling, power interruption, and physical radio
evidence remain open.
