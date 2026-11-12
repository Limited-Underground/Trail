# Trail V1 group design: pairwise delivery

Status: design proposal, 2026-11-12. The owner chose the direction (workspace
decisions D-057 and D-058); this document turns it into a design. It is not
reviewed and not implemented. It needs an independent security review before
any group code is built (see [Review questions](#review-questions)). No
algorithm, library, wire byte layout or radio setting is selected here.

## Purpose

V1 requirement 4 asks for groups that people create, join and talk in. The
accepted security designs only cover two boards:
[OTSL0/v0](SECURE_LORA_KEY_TRANSPORT_V0.md) allows exactly two members with
pairwise unicast and no broadcast, and the accepted
[enrollment design](PRODUCT_ENROLLMENT_REKEY_DRAFT_V1.md) requires an in-person
fingerprint check for every pair. This document describes how groups of up to
six boards work on top of those pairs, and which accepted rules it changes.

Owner choices this design follows:

1. A group message is sent once to each other member over that pair's own
   protected link. There is no group key in V1. Groups have at most 6 members.
2. A new member pairs in person with the group admin only. The admin's board
   then vouches for member identities, and every other pair sets up its own
   keys without a human step. Only the two boards of a pair hold its keys.
3. Removing or losing a member deletes only that member's pairs. The other
   pairs keep their keys.
4. Groups that share location use fast radio settings (around SF7 at 125 kHz)
   and send a position no more often than every 60 seconds.

## What stays the same

- Every protected packet still goes from one board to exactly one other board,
  under keys only that pair holds. Each pair is an OTSL0 pair: two members,
  pairwise unicast, no relay, no server.
- Packet-v1 authenticated data already binds sender, recipient, group,
  epoch, direction and purpose ([host profile](PACKET-V1-HOST-PROFILE.md)).
  Group messages use it unchanged with new purpose values.
- The first pairing between two people (in person, full fingerprints, matching
  transcript code, local confirmation on both boards) is unchanged. In a group
  it is used once, between the admin and each new member.
- Phones never hold LoRa keys and cannot vouch for anyone.
- Factory reset erases all group and pair records as it does today.

## What changes

| Accepted rule | Change in this design |
|---|---|
| Enrollment: verification "is scoped to one pair and attempt, not a general certificate authority". | The group admin's board can vouch for member identities inside one group. Trust in a vouched identity is limited to that group's roster and to pairs inside that group. |
| Enrollment: every first pairing needs an in-person fingerprint check and local confirmation on both boards. | Pairs between two non-admin members of a group (introduced pairs) are set up from admin-vouched identities with no human step. |
| [Group lifecycle v0](GROUP_LIFECYCLE_V0.md): after joining, the new member receives "current group material". | There is no group key. The new member receives the signed roster and then sets up pairs. |
| Group lifecycle v0: revoking a member advances the whole group epoch and every member must rekey. | Removing a member publishes a new roster version; members delete their pairs with the removed member. Other pairs keep their keys and epochs. |

## Roles and limits

- **Admin**: the group's creator (V1 requirement 6). The admin invites and
  removes members, sets the location rule, and signs the roster. V1 has one
  admin per group; promotion of further admins from group lifecycle v0 is not
  part of V1.
- **Member**: sends and receives group messages.
- At most **6** current members including the admin, so at most 15 pairs per
  group. A seventh join is refused (matches OT-0306 acceptance).
- A member's pairs belong to one group. The same two people in two groups, or
  in a group and an outside-group conversation (requirement 5), hold separate
  pairs: keys are bound to the group ID.

## The roster

The roster is the admin board's signed list of who is in the group. Every
member keeps the newest roster it has accepted.

Contents:

- version label and purpose (domain separation from every other signature);
- group ID (64-bit, nonzero);
- roster version (32-bit, starts at 1, each new roster is exactly one higher
  than the admin's previous one);
- admin's full identity fingerprint;
- each member's full identity fingerprint, identity public key, alias and
  role;
- the group's location rule (off / preferred / required, requirement 6);
- signature by the admin's identity signing key.

Acceptance rules on a member board:

- the signer must be the admin identity this board verified in person when it
  joined (stored with its group record); any other signer is refused;
- the group ID must match;
- the version must be higher than the newest accepted version. Because each
  roster is complete, a board that missed versions can accept a later one
  directly. Equal or lower versions are ignored;
- the board's own identity must be listed, unless the roster removes it (then
  the board leaves the group, see [Leaving and removal](#leaving-and-removal));
- at most 6 members, no duplicate identities or aliases.

Because rosters are signed, any member may pass on the newest roster; it does
not have to come from the admin directly. Each group message carries the
sender's roster version, so a board that sees a higher version asks the sender
for that roster.

## Creating a group

The creator's board makes a group record: new random group ID, roster version
1 listing only the creator as admin, and the chosen location rule. Nothing is
sent over the radio.

## Joining

1. **In person with the admin.** The admin creates an invitation for this
   group, and the admin's board and the joiner's board run the accepted first
   pairing (fingerprints, possession proofs, invitation, transcript code, local
   confirmation on both boards). The invitation names the group and the
   joining role, so the transcript both people confirm shows which group they
   are joining. This is the only human step.
2. **New roster.** The admin's board publishes roster version N+1 with the
   joiner added and sends it over its pair to every member, including the
   joiner.
3. **Introduced pairs.** The joiner's board starts a pairwise handshake with
   each existing member listed in the roster. Each side authenticates the
   other with the identity key in the newest accepted roster and contributes
   fresh ephemeral material. The transcript binds the group ID, the roster
   version that introduced the pair, both full fingerprints, the ordered roles
   (initiator, responder) and the purpose "introduced pair". A responder only
   answers an initiator listed in its own newest roster; if the initiator's
   roster is newer, it fetches it first.
4. **Activation.** Each new pair is stored and activated with the accepted
   durable steps (pending record, read back, activation intent, peer
   activation). A pair that fails or times out is retried later while both
   members are still on the roster; a failed attempt never activates a pair.
5. **Fully joined.** The joiner is fully joined when it has an active pair
   with every other member. Until then the app shows, per member, whether the
   pair is ready, and messages reach only members with an active pair.

When a 6th member joins, step 3 runs 5 handshakes. Their radio time must be
measured once message sizes are fixed.

## Group messages

Each group message is sent as one Packet-v1 frame per other member, over that
member's pair, with a group purpose (group text, group quick status, group
position). The protected payload starts with a small group header:

- roster version the sender used;
- sender's group message number (counts up per sender per group; with the
  sender identity it names the message);
- message type and the message body (for text, an OTXT v1 message).

Receivers:

- accept only from members on their newest roster with an active pair;
- show each message once (by sender and group message number), with gaps
  visible;
- refuse group positions when the roster's location rule is off.

Senders keep delivery state per member ("delivered to 4 of 5"). There is no
relaying: a member whose pair is not ready does not get the message through
another member.

## Leaving and removal

- **Removal.** The admin's board publishes a roster without the member.
  Every remaining member that accepts it deletes its pair with the removed
  member (stop traffic, retire keys as OTSL0 requires) and refuses later
  traffic from it. The admin also tells the removed member over their pair,
  then deletes that pair.
- **Leaving.** The member's board sends a leave request to the admin over
  their pair and deletes all its pairs in that group. The admin publishes a
  roster without it.
- **Admin leaving** deletes the group, following the confirmed-delete rule in
  OT-0307.
- Other pairs are not touched: the removed member never held their keys.

A removed member's old pairs stay usable on any board that has not yet seen
the new roster. Rosters spread with ordinary group traffic, so the gap closes
when the board next hears from the admin or any up-to-date member. The app
shows when the roster was last updated.

## Lost or reset boards

- A member's board that is factory reset has a new identity. To rejoin it
  pairs in person with the admin again as a new member; the admin removes the
  old identity.
- If the admin's board is lost or reset, nobody can change the roster. Members
  can keep messaging with the current roster but cannot add or remove anyone.
  V1 recovery is to create a new group. (See review question 6.)

## Radio budget

From the I-005 airtime estimate (workspace review, 2026-11-12), one channel,
without acknowledgements or retries:

| Group | Settings | Positions every 60 s | Plus one text per member per minute |
|---|---|---|---|
| 6 members | SF7, 125 kHz | about 7% busy | about 20% busy |
| 6 members | SF9, 125 kHz | about 23% busy | about 64% busy |

So groups that share location need the faster settings and a position
interval of at least 60 seconds. The final radio settings, acknowledgement
cost and the US915 regulatory check (time on air per transmission) remain open
and belong to radio selection, not this design.

## What a misbehaving admin or member can do

- The admin can add any identity to the roster. That is no more than the
  admin can already do by inviting someone, and all members see the roster.
- The admin cannot read messages between two other members: it never holds
  their pair keys.
- A member can read only messages sent to it.
- A non-admin cannot change the roster: other boards refuse rosters not
  signed by the admin identity they verified in person.
- Replaying an old roster does nothing: lower versions are ignored.

Members who want to can still compare fingerprints in person with any other
member; the app shows each member's fingerprint. This is optional and does not
change the pair.

## Review questions

The independent review should answer at least these before implementation:

1. Is trusting admin-signed identities, scoped to one group, an acceptable
   change to the "not a general certificate authority" rule?
2. Introduced pairs have no human step. Is the transcript binding (group,
   roster version, both identities, roles, purpose) enough to stop a
   substituted identity or a pair being reused across groups?
3. Is "accept any higher complete roster" safe against rollback and against a
   lost admin board signing an old version again after a restore?
4. Is it acceptable that a removed member can still reach boards that have not
   yet seen the new roster, until they do?
5. Routine rekey: the accepted design requires a local transcript
   confirmation on every rekey. Should introduced pairs, which never had a
   human step, rekey without one, and should in-person pairs do the same?
6. Admin loss: is "create a new group" acceptable for V1, or is a second
   admin needed?
7. Storage: how many groups and pairs must one board hold, and how does that
   fit the protected storage design?

## Implementation order once reviewed

1. Roster record, signature domain and acceptance rules (host tests).
2. Introduced-pair handshake on top of the selected pairwise handshake
   (host tests, including substituted identity, wrong group, old roster).
3. Group header, per-member delivery state and duplicate suppression.
4. Android group screens bound to the board's roster (OT-0305, OT-0306,
   OT-0307).
5. Persistence and interruption tests (OT-0308), then a three-board bench
   test.
