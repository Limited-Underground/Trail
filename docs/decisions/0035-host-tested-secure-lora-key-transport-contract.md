# Decision 0035: Host-tested secure-LoRa key and transport contract

- Status: Accepted host-only algorithm-neutral contract and validation
- Date: 2026-08-19
- Work item: OT-091
- Scope: V1 direct-LoRa key provisioning, epoch replacement, protected transport, replay, acknowledgement, and bounded delivery

## Context

[Decision 0033](0033-permanent-v1-v1-5-scope-and-security-boundary.md)
requires a separate versioned LoRa key-provisioning/replacement and protected-
transport contract before implementation. BLE ownership controls which phone
may operate one Heltec; it does not authenticate or encrypt radio traffic.

[Decision 0003](0003-crypto-benchmark-gate.md) remains controlling. OpenTrail
cannot select packet-v1 cryptography, a handshake instantiation, a library, a
KDF, or final wire identifiers until the exact target benchmark passes. The
public OT-005 benchmark plan is still blocked on the exact client board and
revision, pinned ESP-IDF/toolchain/sdkconfig, candidate dependency locks, and
direct-radio MTU/PHY profile.

## Decision

OpenTrail accepts `OTSL0/v0`, the host-tested algorithm-neutral secure-LoRa
lifecycle and admission contract documented in
[`SECURE_LORA_KEY_TRANSPORT_V0.md`](../security/SECURE_LORA_KEY_TRANSPORT_V0.md)
and represented by
[`OT-091-SECURE-LORA-KEY-TRANSPORT-CONTRACT-V0.json`](../../tests/release-plans/OT-091-SECURE-LORA-KEY-TRANSPORT-CONTRACT-V0.json).

V1 is exactly one authenticated pairwise-unicast conversation between the two
current Heltec members. There is no V1 relay, group-broadcast, server, internet,
or V1.5 authority. The phone may request and present a workflow through the
separately authorized BLE path, but LoRa private material stays on the Heltecs.
BLE pairing, a phone request, a display name, a network alias, a parsed
invitation, a received packet, or a caller-supplied Boolean cannot manufacture
cryptographic authority.

Provisioning uses one single-use, secret-free authenticated invitation bound to
the exact group, epoch, inviter identity/key binding, requested role, nonce,
and deadline. One invitation admits one candidate and one handshake attempt.
The device-to-device exchange must mutually authenticate the exact identities,
bind the complete transcript, and receive matching local human confirmation on
both devices before candidate material may be staged. Exact durable commit and
readback plus exact peer activation precede routine traffic. Expiry,
cancellation, replay, wrong binding, authentication failure, restart, or
confirmation rejection consumes the attempt without releasing material.

The original deadline and an exact nondecreasing clock remain mandatory through
handshake and confirmation; expiry or rollback burns the attempt and cannot be
resurrected with an earlier timestamp. Malformed, busy-state, or re-entrant
evidence carrying an exact valid invitation sequence burns that sequence before
rejection.

Same-sequence replay while active burns and closes the active attempt. A
different valid busy-state sequence is consumed without opening a second
invitation, while its exact clock observation advances and remains bound to the
original deadline; rollback or expiry burns the active attempt.
Premature confirmation or a repeated/out-of-phase handshake likewise burns and
closes the active attempt rather than preserving it for a later ordered retry.

Epoch replacement advances its nonzero 32-bit epoch by exactly one, uses fresh
distinct material without wrap, and admits only retained exact identities.
Routine traffic is blocked while the transition is unresolved. A known pre-
mutation abort may retain the exact old
epoch. Any possible mutation, activation, retirement, or roster ambiguity
requires reconciliation with no traffic. After exact activation there is no
fallback or grace acceptance for the old epoch. Logical key retirement means
the old record is unreachable through the ordinary application interface after
exact readback; this decision makes no physical secure-erasure claim for
wear-levelled flash.

The complete derivation chain binds the contract/version, nonzero group and
epoch, both full authoritative fingerprints, ordered direction, purpose, and
authenticated transcript before using the existing host-tested traffic-context,
counter-lease, and nonce-composition boundaries. The current `OTKD/v1` context
does not by itself bind a destination, so production pairwise composition must
supply that binding or adopt a separately reviewed context revision. Nonzero
rollback-safe counters are reserved durably before use; retry retransmits the
exact sealed bytes and never reseals changed content under the same counter.
Each valid message ID and counter is consumed independently when its own
durability evidence is literal `True`; malformed evidence or an invalid
identifier for the sibling resource, and any later admission failure, cannot
release the valid resource for reuse.

This burn precedes ready/pending-state rejection, so re-entry cannot recover a
presented durable resource or upgrade transport state.

Packet v0, plaintext fallback, downgrade, unknown versions/types, nonzero
reserved fields, malformed lengths, wrong group/epoch/destination, invalid
identity binding, failed authentication, corruption, replay, and conflicting
duplicates are denied. Authentication precedes replay mutation; durable replay
and receive-state admission precede plaintext release and positive
acknowledgement. A byte-identical authenticated retry is not redelivered and
may receive another protected acknowledgement. The current time-based
`DuplicateWindow` is application duplicate evidence, not cryptographic replay
authority, and the current caller-facing delivery acknowledgement seam may be
used only behind exact protected-ACK admission.

A positive LoRa acknowledgement means only that the peer device durably
admitted the message. It does not mean the receiving phone displayed it, a user
read it, or delivery is guaranteed. Send acceptance remains local queue
acceptance, and only an exact frame accepted into that queue makes a later
protected acknowledgement eligible. A rejected transport attempt does not.
Retry and failure are finite; exact production timing remains
blocked on measured radio and regulatory evidence. Restart never automatically
reseals or retries an ambiguous pending delivery, and consumed identifiers and
counters remain consumed.

A queue-accepted report paired with wrong/noncanonical sealed bytes, no pending
object, or an exhausted attempt bound is transmit ambiguity and requires
reconciliation with no traffic. Only a definite no-queue result remains a
bounded failed attempt.
An exact expired acknowledgement deadline terminally fails that pending
delivery before sibling ACK evidence can revive it; later ACK input cannot
resurrect the delivery or release consumed values.

The checked host model permits at most 255 opaque frame bytes in storage. This
is not a selected target-radio MTU, packet-v1 size, PHY, or regulatory payload
claim.

## Algorithm and implementation boundary

`OTSL0/v0` freezes semantic obligations only. It does not select Ed25519,
X25519, Noise XK, ChaCha20-Poly1305, libsodium, mbedTLS/PSA, Monocypher, a KDF,
tag size, handshake encoding, alias derivation, packet offsets, MTU, PHY, or
deployed retry values. It does not create packet v1. An incompatible future
suite cannot bypass the accepted context/counter/nonce obligations; it requires
a new reviewed contract version.

## Accepted V1 limitation

Ordinary restart, corruption, and power-interruption paths must fail closed and
must not reuse a key/counter domain. V1 still does not claim protection against
factory reset, reflashing, invasive physical access, or old-flash restoration.
Those operations may reset or roll back membership, keys, counters, and replay
state under the disclosed physical-firmware-access limitation.
Within ordinary restart handling, direct transport cannot restore a lower epoch
or silently advance; an exact plus-one advance requires explicit verified rekey
evidence, and all other epoch movement reconciles with no traffic.
Restoring inactive state requires exact group absence, epoch zero, and exact
state/peer/context/counter/replay absence proofs. Out-of-order results claiming
a verified mutation force reconciliation; a known-no-change result cannot
upgrade authority.

## Authority and evidence boundary

OT-091 accepts deterministic host-contract evidence only. It grants no target,
storage, entropy, key-generation, signer, radio, BLE, phone, display/input,
installation, account, upload, or distribution authority. It does not prove a
crypto library, packet v1, provisioning, rekey, encryption, authentication,
replay protection, acknowledgement, delivery, supported target, or physical
V1 path.

Keys, secrets, private fingerprints, aliases, group/epoch values, invitation
values, confirmation values, message IDs, counters, packet bytes, plaintext,
ciphertext, tags, correlations, and private storage contents remain prohibited
from ordinary logs and public evidence. Public evidence is categorical and
aggregate only.
Host model categories require exact built-in types, and validator/CLI failures
must not echo supplied values, hostile argument text, or local paths.
Direct validation bounds nesting/cycles, and file loading uses one bounded
maximum-plus-one read before oversize rejection.

## Progress

This host-only contract closes no scored implementation or physical evidence
gate. Android remains 60%. V1 Companion remains exact 43.75% and displayed
44%. The historical standalone baseline remains exact 31.75% and displayed
32%. V1.5 remains unmeasured.
