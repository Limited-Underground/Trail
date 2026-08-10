# OpenTrail Identity and Group Threat Model v0

Status: architecture input for OT-005 and host-tested lifecycle input for OT-013, 2026-08-08

This document defines threats and lifecycle rules. It does not select or claim
an implemented cryptographic protocol. Packet v0 remains unauthenticated test
traffic and must not carry real group, location, or emergency information.

## Protected assets

- long-term device private identity material;
- group traffic keys and current group epoch;
- membership, revocation, and administrator state;
- precise current and historical locations;
- message and alert contents;
- recovery exports and one-time invitations; and
- firmware/configuration integrity and anti-rollback state.

Display names, short network aliases, channel names, and QR presentation text
are labels or routing conveniences. They are never proof of identity.

## Threat actors and failure cases

| Threat | Required response |
| --- | --- |
| Passive radio observer | Do not expose stable device identity, group membership, precise location, or plaintext content unnecessarily; padding/traffic-analysis limits remain a later budget decision |
| Active injector or impersonator | Authenticate every protected packet and bind version, type, routing context, group epoch, sender alias, message ID, and ciphertext length as authenticated data |
| Replay attacker | Use a per-sender monotonic message/nonce discipline, duplicate window, epoch binding, and persistent rollback-safe state |
| Malicious current member | Treat membership as access to current group traffic, not proof of an individual sender or administrator authority. Require separate source authentication for named-origin claims; rate-limit and support removal/key rotation |
| Lost or stolen node | Revoke it, advance the group epoch, distribute a new traffic key only to retained members, and disclose that old captured traffic/secrets cannot be remotely erased |
| Compromised setup phone/computer | Minimize secret exposure, require human confirmation for privileged operations, expire invitations, and provide a direct physical recovery/reset path |
| Identity/alias collision | Compare authoritative full fingerprints; never merge peers solely because a short alias or display name matches |
| Partitioned/offline group | Carry explicit epochs and tolerate delayed key updates without silently accepting stale-epoch traffic |
| Radio denial/jamming | Fail visibly and safely; cryptography cannot guarantee availability or emergency delivery |
| Physical flash extraction | Current development evidence shows secure boot and flash encryption disabled on `OT-DEV-001`; do not claim protection against a physical attacker on these boards |
| Downgrade or corrupt update | Require signed/versioned updates, anti-rollback policy, interruption recovery, and a physical recovery path under OT-019 |

## Identity boundaries

OpenTrail separates five concepts:

1. **Device key identity:** long-lived cryptographic identity material generated
   from a cryptographically secure random source and held locally.
2. **Identity fingerprint:** a full, authoritative public identifier used to
   verify identity and resolve collisions. It is not normally broadcast in
   routine packets.
3. **Network alias:** a shorter group-scoped routing identifier. It may rotate
   with a group epoch to reduce cross-group/cross-time tracking. A collision
   triggers explicit disambiguation using full fingerprints.
4. **Display name:** mutable user-facing text. Rename never changes identity,
   membership, trust, or alias.
5. **Group membership:** authorization for one group and epoch, separate from
   the device identity and from administrator authority.

The C++ `IdentityModel` exercises these boundaries without implementing key
generation or cryptography. Its 32-byte fingerprint is an algorithm-neutral
placeholder and its 64-bit alias/group fields are model inputs, not a final wire
derivation.

The separate `GroupAccessController` exercises administrator-gated invitation,
join, promotion, revocation, epoch-change, and rekey behavior. Its authentication
evidence flags are adapter obligations, not implemented cryptographic proof. See
[the OT-013 lifecycle and UX specification](GROUP_LIFECYCLE_V0.md).

## Lifecycle

```text
unprovisioned
  -> identity_ready
  -> join_pending
  -> active
      -> left -> join_pending
      -> revoked -> left

configuration reset: any provisioned state -> identity_ready, identity retained
factory reset: any state -> unprovisioned, identity and memberships erased
```

- Provisioning rejects an all-zero identity fingerprint.
- Joining requires a nonzero group ID and epoch. Activation must match the exact
  pending group/epoch and receive a nonzero network alias.
- Only `active` state may originate protected group traffic.
- Revocation must advance the epoch, clears the active alias, and disables group
  traffic locally. Retained members still need authenticated rekey delivery.
- Leaving clears group state but retains the device identity.
- Configuration reset and factory reset must be visibly distinct operations.
  Factory reset generates a new identity on next provisioning unless an
  explicit, authenticated recovery import is performed.

## Required offline join properties

The intended UX may use a QR or short join code, but the code must be a
single-use, short-lived invitation rather than a reusable plaintext group key.
An invite should bind:

- protocol and invitation version;
- target group and current epoch;
- inviter identity/fingerprint and ephemeral handshake material;
- random nonce, expiry, and allowed role;
- human-verifiable short authentication value; and
- an inviter signature or authenticated handshake transcript.

The joining device and group administrator must authenticate each other before
current group traffic material is provisioned. Joining an unauthorized group
and admitting an unauthorized node are both explicit threats. Interactive
Noise-style patterns are candidates because they can provide mutual
authentication, forward secrecy, and identity hiding, but handshake bytes,
round trips, loss recovery, library quality, and ESP32 resource use require
measurement before selection.

## Revocation and recovery rules

- Group traffic keys are versioned by epoch. Removing a member requires a new
  epoch/key; a deny-list alone cannot revoke a key already copied to a node.
- A node behind the current epoch cannot send normal group traffic. Recovery
  messages need a separately bounded authenticated path.
- At least two administrator-capable recovery paths should be considered so a
  lost sole administrator does not permanently strand a group.
- Recovery exports must be encrypted and integrity-protected using an explicit
  user-controlled secret. They must never be logged or embedded in reusable QR
  screenshots.
- A reset cannot claw back previously transmitted information. The UI must say
  whether identity is retained, rotated, or restored.

## Cryptographic decision gate

The [2026-08-10 candidate review](CRYPTO_CANDIDATE_REVIEW_2026-08-10.md)
now fixes the benchmark order and leading invitation prototype without making a
production selection. Espressif's libsodium component is first, the pinned
ESP-IDF mbedTLS/PSA build and Monocypher are comparisons, and Noise-C is only a
reference/vector source. `Noise_XK_25519_ChaChaPoly_SHA256` is eligible only
when a signed invitation pins the responder Noise key. Packet v0 remains
unauthenticated.

Do not implement new cryptographic primitives. Before packet v1, compare audited
ESP32-capable libraries and record test vectors, nonce construction, key
separation, entropy source, side-channel behavior, persistent counter rollback,
packet overhead, performance, license, and maintenance status.

Candidates for evaluation—not selections—include:

- X25519 key agreement from [RFC 7748](https://www.rfc-editor.org/rfc/rfc7748.html);
- Ed25519 signatures from [RFC 8032](https://www.rfc-editor.org/rfc/rfc8032.html);
- HKDF key separation from [RFC 5869](https://www.rfc-editor.org/rfc/rfc5869.html);
- ChaCha20-Poly1305 AEAD from [RFC 8439](https://www.rfc-editor.org/rfc/rfc8439.html); and
- an appropriate [Noise Protocol Framework](https://noiseprotocol.org/noise.html)
  handshake pattern for interactive joining.

The current MeshCore source uses Ed25519 public identities and an ECDH conversion
for shared secrets. That is useful adapter context, not an automatic OpenTrail
choice; OpenTrail must review its own threat model, wire budget, libraries, and
compatibility goals.

The device/lifecycle requirements also draw on NIST's
[IoT device cybersecurity capability baseline](https://csrc.nist.gov/pubs/ir/8259/a/final)
and its guidance on
[trusted IoT onboarding and lifecycle management](https://csrc.nist.gov/pubs/sp/1800/36/final).

## Evidence still required to complete OT-005

- complete the selected candidate matrix and benchmark an audited crypto library
  on the exact ESP32 target;
- instantiate the invitation and authenticated handshake transcript/timeout/retry with the selected crypto library;
- decide whether production privilege changes need multi-administrator approval beyond OT-013's one-current-administrator policy;
- define alias derivation/collision recovery and privacy rotation;
- define key/epoch storage, rollback protection, and physical reset behavior;
- add standard cryptographic vectors and negative/interoperability tests; and
- perform a two-device join, rename, revoke/rekey, reset, and recovery test.
