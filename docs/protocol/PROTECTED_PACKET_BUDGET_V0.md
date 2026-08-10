# Protected Packet Candidate Budget v0

Status: host-tested sizing model, not a packet-v1 wire format, 2026-08-10

## Purpose and boundary

The protected-packet budget model answers one question before cryptography and
the production transport are frozen: how much plaintext, fragmentation, and
theoretical LoRa airtime remain after every frame pays for authenticated routing
metadata and an AEAD tag?

It does not encode/decode a packet, select a cipher/library, derive a nonce,
perform authentication, authorize forwarding, or approve fragmentation. Packet
v0 remains the only implemented envelope and remains unauthenticated test
traffic.

## Candidate accounting profile

The model accepts explicit header/tag sizes. The current candidate sizing
profile is 44 authenticated header bytes plus a 16-byte tag. The 44-byte subtotal
reserves space for the fields already required by the threat model:

| Field group | Candidate bytes |
| --- | ---: |
| envelope magic/version/type/flags/header length | 6 |
| immutable forwarding/reserved plus fragment index/count | 4 |
| group epoch | 4 |
| sender alias | 8 |
| destination alias | 8 |
| message identifier | 4 |
| rollback-safe per-frame counter | 8 |
| ciphertext length | 2 |
| **Authenticated header subtotal** | **44** |
| IETF ChaCha20-Poly1305 candidate tag | 16 |
| **Base group-access overhead** | **60** |
| Ed25519 source-signature candidate | +64 |
| **Signed-group candidate overhead** | **116** |
| Optional future per-hop wrapper | explicit variable input |

These are accounting reservations, not final offsets or protocol identifiers.
The proposed 96-bit nonce combines a derived 32-bit sender/key prefix with the
transmitted 64-bit counter; the derived prefix is not added to the wire budget.
The host-tested [composition boundary](../security/AEAD_NONCE_COMPOSITION_V0.md)
now fixes exact byte order and requires the durable lease and traffic key to
carry the same full 128-bit domain. Cryptographic domain/key/prefix derivation
remains unresolved and cannot use an unverified short alias alone.

Mutable routing is not hidden inside the 44-byte number. Decision 0004 keeps the
first-release sender-protected object immutable and allows at most one
authorized repeater to forward the exact bytes once. A future multi-repeater
wrapper is a separate explicit budget input and may increase overhead.

Source authentication is also charged separately. The 16-byte group AEAD tag
does not by itself distinguish one current member from another when group
material permits them to derive the same keys. The 64-byte Ed25519 candidate
makes the signed-group cost visible without selecting the final signature
construction.

Each fragment is independently charged the full header and tag because every
protected frame needs authenticated routing/fragment metadata and a unique
counter. The implementation limits a calculation to 16 fragments to keep the
model bounded; that limit is not an approved fragmentation policy.

## Current deterministic comparisons

| Case | Result |
| --- | ---: |
| 163-byte example transport MTU | 103 plaintext bytes per protected frame |
| 255-byte direct-radio ceiling | 195 plaintext bytes per protected frame |
| 16-byte position payload at 163-byte MTU | 1 fragment, 76 frame bytes |
| Position airtime at 62.5 kHz/SF7/CR5 | 276,992 us |
| Signed-group 16-byte position | 140 bytes, 461,312 us |
| Signed group plus 16-byte future wrapper | 23 plaintext bytes per 163-byte frame |
| 300 plaintext bytes at 163-byte MTU | 3 fragments, 480 total frame bytes |
| 300-byte theoretical airtime at the same PHY | 1,568,256 us |

For comparison, packet v0's 22-byte overhead leaves 141 bytes at a 163-byte MTU,
and its 38-byte position frame has 164,352 us theoretical airtime at the same
PHY. The candidate security accounting therefore consumes 38 additional bytes
per frame and adds 112,640 us to this position example. Those numbers are sizing
evidence, not measured radio performance.

The 163-byte value is an existing adapter example, not a frozen direct OpenTrail
MTU. Every target adapter must supply its measured usable MTU. The LoRa airtime
calculation excludes contention, firmware scheduling, transport encapsulation,
retries, forwarding copies, regulatory limits, and non-LoRa delays.

## Fail-closed model

`calculate_protected_packet_budget()` rejects:

- zero or greater-than-255 transport MTU;
- zero header, tag, or fragment bound;
- a fragment bound greater than 16;
- overhead that leaves no plaintext capacity;
- plaintext that exceeds the requested fragment bound; and
- invalid LoRa settings or an airtime accumulation failure.

On fragment/airtime failure it returns no partial byte or airtime totals.
Zero-length plaintext still costs one authenticated frame and one tag.

## Host evidence and remaining gates

Ten scenario groups cover both example MTUs, the base protected-position cost,
the signed-group source-authentication cost, a separate future-wrapper input,
the signed 64-byte critical-alert two-fragment cost, multi-fragment full-
overhead accounting, empty plaintext, the exact one/two-
fragment boundary, fragment-limit refusal, and invalid capacity/PHY behavior.

The [requirements reconciliation](PROTECTED_HEADER_REQUIREMENTS_V0.md) records
why the earlier 36-byte candidate was undercounted. Before a real packet-v1
codec exists, OpenTrail still needs a selected and
target-benchmarked crypto suite, exact nonce-domain/key derivation, final field
semantics/offsets, replay/fragment reassembly rules, mutable-routing protection,
measured target MTU/airtime, regulatory review, and physical multi-node evidence.
