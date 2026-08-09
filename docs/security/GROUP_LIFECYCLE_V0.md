# OpenTrail Group Provisioning and Recovery Lifecycle v0

Status: host-tested OT-013 policy foundation, 2026-08-08

This document specifies operator-visible provisioning, joining, revocation, and
recovery behavior. The C++ controller proves state and authorization gates; it
does not generate keys, sign invitations, authenticate a handshake, encrypt
traffic, or persist secrets. Packet v0 remains unsuitable for real groups.

The design follows the trusted-onboarding principle that a device and the group
authority establish trust before group credentials are released. NIST's final
trusted IoT onboarding guidance treats both directions as threats: a device can
join an unauthorized network, or a network can admit an unauthorized device.
OpenTrail applies that principle to an offline radio group even though it is not
an IP network.

## Boundaries and roles

The authoritative identity remains the full device fingerprint. Display name,
group name, invitation ID, network alias, and short human code are never proof
of identity.

The lifecycle model has three roles:

- `member`: ordinary group traffic;
- `repeater`: ordinary group traffic plus separately configured forwarding; and
- `administrator`: member traffic plus invitation, promotion, and revocation
  authority.

An invitation can admit only a member or repeater. Administrator authority is a
separate, explicit promotion after the device is an authenticated active member.
The v0 policy permits one current administrator to perform an operation; a
multi-administrator approval/quorum policy remains a cryptographic and product
decision.

The host ceilings are 16 retained member records, eight invitation records, and
four pending joins. Consumed/cancelled invitations remain reserved until expiry
to model replay rejection. Revoked fingerprints remain as tombstones and count
against the roster until a future persistence/retention policy is defined.

## Group creation

Creating a group requires a nonzero group ID and epoch, a full nonzero owner
fingerprint, and a nonzero group-scoped alias. The owner becomes the first
active administrator. The UI must immediately label the group as **single-admin
recovery risk** until a second authenticated member is separately promoted.

The controller reports `recovery_ready` only when at least two administrators
are active on the current epoch. This means administrator redundancy exists; it
does not mean an encrypted recovery export or quorum protocol has been built.

## Invitation and join workflow

### Administrator

1. Choose **Invite member** or **Invite repeater**. There is no **Invite admin**.
2. Review group, role, expiry, and a warning that sharing the QR/code authorizes
   an onboarding attempt.
3. Create a single-use invitation. The host model uses a nonzero 64-bit ID and a
   maximum one-hour lifetime only as state-machine inputs; the real format needs
   a cryptographically strong nonce/token and an authenticated expiry.
4. Present the QR/code only while needed. The UI exposes **Cancel invitation**
   and its exact expired, cancelled, consumed, or capacity-limited status.

### Joining device and administrator

1. The joining device scans or enters the invitation and shows the claimed
   group, inviter fingerprint summary, role, and expiry before accepting.
2. The handshake authenticates the group to the device and the device to the
   group, and binds the transcript to the exact invitation, group, epoch, role,
   and ephemeral handshake data.
3. Both sides display the same short human-verifiable code and require explicit
   confirmation. A display name is insufficient confirmation.
4. Only after all four gates—group authentication, device authentication,
   transcript binding, and human-code confirmation—may the joining device
   receive current group material and a collision-free group alias.
5. Success shows **Active on epoch N**. Any authentication rejection burns the
   single-use invitation and returns to a non-member state.

The controller consumes an invitation when a join begins, not after activation.
One fingerprint cannot hold multiple pending joins, pending requests cannot
overbook the fixed roster, and exact-expiry time is already expired. Alias
collision leaves the authenticated pending join available for a different alias
until its original expiry.

The four authentication booleans in the host model are obligations on a future
audited handshake adapter. They are not caller-controlled security decisions in
production code.

## Administrator promotion

Promotion is available only for an active, current-epoch member. The acting
administrator must open a separate privilege screen that:

- shows and verifies the target's full fingerprint, not only its name/alias;
- obtains confirmation from the target user; and
- requires acknowledgement that the new administrator can invite, promote, and
  remove members and is responsible for recovery material.

An incomplete confirmation causes no state change. Promotion never occurs as a
side effect of scanning an invitation.

## Revocation and lost-node response

The operator chooses a roster entry by full fingerprint and sees that a reset or
revocation cannot erase data or keys already copied to the lost device. A valid
revocation then performs one atomic policy transition:

1. advance the group epoch by exactly one;
2. mark the target fingerprint revoked and clear its alias;
3. cancel every invitation and pending join from the old epoch;
4. clear aliases and mark every retained active member `rekey_pending`; and
5. block all group traffic until each retained member installs authenticated
   new-epoch material and receives a collision-free replacement alias.

The model refuses to revoke the last known administrator. This prevents a
deliberate UI action from orphaning the group, but it cannot recover a group
whose only administrator is already lost or destroyed. A remaining second
administrator can revoke the lost administrator, rekey itself, and resume group
administration; the UI then returns to the single-admin risk warning until
another administrator is promoted.

A revoked fingerprint cannot consume a new ordinary invitation. Restoring the
same identity would require a separately authenticated recovery decision, not a
normal join bypass.

## Reset and recovery UX

The existing device identity model keeps two reset operations visibly separate:

- **Clear group/configuration:** retain the device identity, erase membership
  and user configuration, then require a fresh invitation before rejoining.
- **Factory reset:** erase identity and memberships. The next provisioning
  creates a new identity unless the product later implements an authenticated
  recovery import.

Every destructive confirmation must state whether identity, group membership,
messages, map packages, and recovery material are retained or erased. Neither
reset remotely revokes credentials already copied elsewhere.

An eventual recovery export must be encrypted, integrity-protected, versioned,
and unlocked by a user-controlled secret. It must identify whether it restores
the old device identity, administrator authority, or only group metadata. It
must never appear in logs, reusable screenshots, crash reports, or ordinary QR
invitations. Import, rollback protection, storage wear, and interrupted recovery
belong to OT-014/OT-019 and remain unimplemented.

## Required visible states and failures

The UI must distinguish at least:

- identity ready but not a member;
- invitation active, consumed, cancelled, or expired;
- join pending, authentication failed, code rejected, alias collision, or full;
- active member/repeater/administrator and current epoch;
- rekey pending and unable to send;
- revoked and unable to rejoin normally;
- single-admin risk versus administrator redundancy; and
- reset/recovery completed, rejected, interrupted, or unsupported.

No state may say secure, authenticated, recovered, or active merely because a
QR parsed, names matched, radio traffic was received, or a key write was
attempted.

## Host evidence and remaining gates

Twelve scenario groups cover initialization, administrator authorization,
bounded invitations, expiry/cancellation, single use, complete mutual-auth
evidence, alias recovery, duplicate-pending and roster-capacity protection,
separate administrator promotion, epoch-advancing revocation, invitation/pending
invalidation, retained-member rekey, revoked-identity exclusion, last-admin
protection, and second-admin lost-node recovery.

Before production, OpenTrail still needs an audited ESP32-capable crypto library,
exact invitation encoding, a selected authenticated handshake pattern, entropy
and key derivation, transcript/role/epoch binding, nonce/counter persistence,
secure key storage, rollback protection, recovery export/import, rendered UX,
and physical multi-device join/revoke/reset/recovery evidence. Noise remains a
candidate framework, not a selection.

Primary guidance and candidate specifications:

- [NIST SP 1800-36, Trusted IoT Device Network-Layer Onboarding and Lifecycle Management](https://csrc.nist.gov/pubs/sp/1800/36/final)
- [NIST IR 8350, Foundational Concepts in Trusted IoT Device Network-Layer Onboarding](https://csrc.nist.gov/pubs/ir/8350/final)
- [NIST IR 8259A, IoT Device Cybersecurity Capability Core Baseline](https://csrc.nist.gov/pubs/ir/8259/a/final)
- [Noise Protocol Framework](https://noiseprotocol.org/noise.html)
