# Trail V1 group design: pairwise delivery

Status: design proposal, revision 2, 2026-11-12. The owner chose the direction
(workspace decisions D-057, D-058) and the answers to the independent security
review (D-059, D-060). This revision adds the ten changes that review required
and the fixes from its re-review (see
[Review record](#review-record)). Implementation order step 1 (the roster) is
implemented on the host; see
[`group_roster.hpp`](../../firmware/components/identity/include/opentrail/group_roster.hpp).
Step 2 (the group state as single authority, the signed binding, epochs and
interruption rules) is implemented on the host, without the handshake suite or
key derivation; see
[`group_pair.hpp`](../../firmware/components/identity/include/opentrail/group_pair.hpp).
Nothing else is implemented. No algorithm, library
or radio setting is selected here; the byte sizes given are budgets for the
current Heltec V4 radio, not a frozen wire format.

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
   then vouches for member identities, and the other pairs set up their own
   keys without a confirmation step. Only the two boards of a pair hold its keys.
3. Removing or losing a member deletes only that member's pairs.
4. Groups that share location use fast radio settings (around SF7 at 125 kHz)
   and send a position no more often than every 60 seconds.
5. Group pairs and outside-group conversation pairs rekey automatically after
   a reboot or recovery, with no confirmation step (D-059).
6. A board stops sending positions to a member it has not heard from with the
   current roster for 10 minutes (D-059).
7. Each member taps once to accept a new member before its board pairs with
   them (D-059).
8. A board holds at most 4 groups and 8 outside-group conversations (D-059).
9. Group texts are at most 80 bytes, in one radio frame (D-059).
10. Rosters are the only group data that may be split across radio frames
    (D-060).

## What stays the same

- Every protected packet still goes from one board to exactly one other board,
  under keys only that pair holds. Each pair is an OTSL0 pair: two members,
  pairwise unicast, no relay, no server.
- Packet-v1 authenticated data already binds sender, recipient, group,
  epoch, direction and purpose ([host profile](PACKET-V1-HOST-PROFILE.md)).
  Group frames use it with new type values (see [Group messages](#group-messages)).
- The first pairing between two people (in person, full fingerprints, matching
  transcript code, local confirmation on both boards) is unchanged. In a group
  it is used once, between the admin and each new member.
- Traffic keys are never kept across a restart (enrollment design, "Rekey,
  restart and reconciliation"). This design does not change that; it adds an
  automatic way to get new keys (see [Restart and rekey](#restart-and-rekey)).
- Phones never hold LoRa keys and cannot vouch for anyone.
- Factory reset erases all group, roster and pair records.

## What changes in the accepted documents

| Accepted rule | Change |
|---|---|
| Enrollment: verification "is scoped to one pair and attempt, not a general certificate authority". | A group admin's roster can vouch for identities inside that group only. That trust is used only for pairs inside the same group; it never shortens or replaces the in-person check for an outside-group conversation, and never carries to another group, even if the same key is in both rosters. |
| Enrollment: "Remote unattended first enrollment is excluded." | Introduced pairs (two non-admin members of one group) are set up remotely from the roster, after each member's own tap to accept (see [Joining](#joining)). This is the only exception. |
| Enrollment: a routine rekey needs a new invitation and a local transcript confirmation. | For every V1 pair (group pairs and outside-group conversation pairs) a rekey is automatic: no invitation, no confirmation. All other rekey rules stay. |
| [Group lifecycle v0](GROUP_LIFECYCLE_V0.md): shared "current group material", 16-member rosters, repeater role, administrator promotion, whole-group epoch advance on revocation. | Superseded for V1 groups, except the rules listed in [What remains from group lifecycle v0](#what-remains-from-group-lifecycle-v0). |

## Naming a group

A group is named everywhere (records, roster, key derivation, the app) by the
pair **(admin full fingerprint, group ID)**. The group ID is 64 random bits
chosen by the admin's board. Two admins choosing the same ID, by chance or on
purpose, still give two different groups, and one can never overwrite or match
the other's records or pairs. Where Packet-v1 carries only the 64-bit group
ID, the pair session is bound to the full group name by its handshake (see
[Introduced pairs](#introduced-pairs)).

## Roles and limits

- **Admin**: the group's creator (V1 requirement 6). The admin adds and
  removes members, sets the location rule, and signs the roster. V1 has one
  admin per group. The app labels every group **single-admin risk** and says
  what happens if the admin's board is lost (see
  [Lost, stolen or reset boards](#lost-stolen-or-reset-boards)).
- **Member**: sends and receives group messages.
- At most **6** current members including the admin, so at most 15 pairs per
  group. A seventh join is refused.
- Per board: at most **4 groups** and **8 outside-group conversations**, so at
  most 28 pairs. A new group or conversation at the limit is refused with an
  explanation.

## The roster

The roster is the admin board's signed list of who is in the group. Every
member keeps the newest roster it has accepted, stored durably.

Contents:

- version label and signature purpose (domain separation from every other
  signature);
- group name: admin full fingerprint and group ID;
- roster version (32-bit, starts at 1, exactly one higher than the previous
  roster);
- digest of the previous roster (all zero for version 1), so rosters form a
  chain;
- each member's identity public key, alias, role and the roster version in
  which the member was added (the fingerprint is recomputed from the key, not
  carried);
- the location rule (off / preferred / required, requirement 6);
- signature by the admin's identity signing key.

The **roster digest** is a hash over the complete signed roster. It, not the
version number alone, identifies a roster.

### Admin rules

- A member can be **added** only from a completed in-person enrollment on the
  admin board itself: a typed local receipt from the accepted first-pairing
  flow for this group. A phone request or a radio message can never add an
  identity. Each new roster adds at most one member.
- A member can be removed on the admin's request from the phone, confirmed on
  the admin board.
- The admin board signs a new roster once, stores the exact signed roster
  durably and reads it back, and only then sends it. After a power cut it
  resends those stored bytes; it never signs a different roster with the same
  version.

### Member acceptance rules

A board accepts a roster only if all of these hold:

- signed by the admin identity this board verified in person when it joined;
- same group name;
- version higher than the newest accepted. When it is exactly one higher, its
  previous-roster digest must continue the chain from the roster this board
  holds. A board that missed versions accepts a higher, admin-signed roster
  across the gap (no old rosters are stored or fetched); the admin signature is
  the authority, and forks are caught by the full-digest comparisons below;
- fingerprints are recomputed from the listed public keys, and each member's
  "added in" version is not higher than the roster's own version;
- at most 6 members, no duplicate identities or aliases;
- the board's own identity is listed, unless the roster removes it (then the
  board leaves the group).

**Roster conflict.** If a board sees the same version with a different digest,
or a roster that does not continue its chain, it shows "roster conflict" (or
"admin out of step"), keeps its current roster, and accepts no roster changes
for that group until its user acts (keep current, or leave the group). This
detects an admin board that signed two rosters or was restored from an old
image. Preventing such a restore is outside V1 (OTSL0 makes no rollback-proof
claim); detecting it is required. The full roster digest is compared in every
introduced-pair handshake, rekey and roster transfer; the 4-byte digest prefix
in group frames and acknowledgements is only an early warning, since a holder
of the admin key could make two rosters with the same prefix.

What a board can detect by itself: it remembers the versions and digests it has
seen (each roster it held and that roster's previous-roster digest, a bounded
recent window), and treats any admin-signed roster that contradicts them, older
or newer, as a roster conflict. A fork whose branch point lies only in versions
the board never saw (for example a policy change, or an addition removed again,
in rosters it missed) cannot be told apart from a legitimate history by the
offered roster alone; it is caught as soon as the board sees a roster or a peer's
full digest from the other branch for a version it knows. A conflict the user
dismissed with "keep current" is remembered, so an exact replay of that roster
does not block the group again; a different conflicting roster still does.
A board joining the group accepts no roster older than the one its confirmed
join produced.

Every roster change is shown on every member board as an event. A known
person's key changing is shown as "removed, and a different device added",
never as the same person. Names and aliases stay presentation only; the app
shows each member's fingerprint, and flags when a name matches a different
device in another group.

Because rosters are signed, any member may pass one on. Fetch requests are
limited to once per version per peer, and only roster members get an answer.

## Creating a group

The creator's board makes a group record: a new random group ID, roster
version 1 listing only the creator as admin, and the chosen location rule.
Nothing is sent over the radio.

## Joining

1. **In person with the admin.** The admin creates an invitation for this
   group, and the two boards run the accepted first pairing (fingerprints,
   possession proofs, invitation, transcript code, local confirmation on both
   boards). The invitation and the transcript summary both people confirm show
   the group name, the joining role and the group's location rule.
2. **New roster.** The admin board publishes roster N+1 with the joiner added
   (using the receipt from step 1) and sends it to every member, including the
   joiner, as described in [Roster delivery](#roster-delivery).
3. **Each member accepts.** Each existing member's board shows the new member
   (alias and fingerprint) and asks its user to accept, one tap. Until the
   user accepts, that board does not pair with the new member and the app
   shows "waiting for your OK". Declining is shown to the admin.
4. **Introduced pairs.** Each pair of the new member and an accepting member
   runs the introduced-pair handshake below.
5. **Fully joined.** The joiner is fully joined when it has an active pair
   with every other member. Until then the app shows, per member, whether the
   pair is ready, waiting for acceptance or failed, and messages reach only
   members with an active pair.

### Introduced pairs

An introduced pair is set up by a pairwise handshake that both long-term
identities sign, under its own signature domain ("introduced pair"). The
signed binding covers:

- the group name (admin fingerprint and group ID);
- the digest of the roster each side used (both must be the same roster);
- both full identity fingerprints and their ordered roles;
- both fresh session (ephemeral) public keys;
- fresh challenges from both sides and both boards' boot tokens;
- a lifetime of at most 60 seconds, enforced by each board on its own clock
  (D-073; see [Restart and rekey](#restart-and-rekey)).

This follows the accepted first pairing, where both identities sign an
invitation that pins both fresh session keys. Signatures over fresh values are
the possession proof; an old handshake cannot be replayed into a new one.

Only the roster verifier can produce the **typed introduced-pair authority**
that lets this handshake activate a pair: an object that names the exact
roster digest and both identities, and that stands in for the fingerprint
review and local confirmation of the first pairing. It is a separate path, not
a "skip confirmation" option on the first-pairing path.

**Who starts.** For the first introduced handshake, the more recently added
member (by its "added in" roster version) starts; each roster adds at most one
member, so there is never a tie. For rekeys and recovery, on every pair
including outside-group pairs, either side may start; if both start at once,
the order in [Restart and rekey](#restart-and-rekey) decides which attempt
continues (an expired start does not count, an introduced pair start beats a
rekey, then the higher epoch, then the lower fingerprint). A responder answers only an initiator listed in its own newest
roster; if the initiator's roster is newer, the responder fetches it first.

Activation uses the accepted durable steps (pending record, read back,
activation intent, peer activation). Handshake answers are rate-limited per
peer.

## Restart and rekey

Traffic keys exist only in memory and are never restored after a restart. After
every reboot, battery swap or recovery, each of the board's pairs needs a new
key before it can carry traffic.

For every V1 pair (introduced pairs, each member's in-person pair with the
admin, and outside-group conversation pairs) the rekey is **automatic**:

- same two identities (and, for group pairs, both still on the current roster);
- exactly epoch+1, as made precise below (D-072); zero, larger skips and wrap
  are refused;
- fresh possession proof and the binding above. It replaces the invitation the
  accepted rekey used and gives the same freshness: fresh session keys, fresh
  challenges from both sides, boot tokens and a deadline. Group pairs bind the
  group name and current roster digest; outside-group pairs bind their
  conversation name (below);
- the accepted durable states PREPARED, ACTIVATION_POSSIBLE and
  ACTIVE_COMMITTED.

**Replay protection.** A rekey start alone changes nothing. The board that did
not restart keeps its working pair until the new handshake has proved fresh
values from both sides (its own fresh challenge signed back by the peer); only
then does it retire the old keys. Rekey starts are rate-limited per peer. A
replayed old start therefore cannot freeze a working pair. The board that
restarted has no keys and sends nothing on that pair until the rekey
completes.

**Outside-group conversations** are named by both full fingerprints and a
conversation ID fixed at their in-person first pairing. That name is bound
into every rekey and into the pair's Packet-v1 context, so an outside-group
pair and a group pair between the same two people are never confused.

**One group state per group.** On each board a single group state holds the
roster and the record of every introduced pair together; it cannot be copied
or moved, and every setup, rekey, activation and commit goes through it. It
alone can issue a verified binding. Each pair record is bound to the group name, both
identities and both members' "added in" versions: applying a roster that removes
or re-adds a member ends that record, so a re-added member needs a new first
pairing and a new acceptance tap. Applying any new roster drops attempts made
under the old one; a binding in ACTIVATION_POSSIBLE cannot commit under a
changed roster. The group state signs only with this board's own identity, takes
challenges only from the board's random source, answers only starts whose
signature it has verified (roster version hints included), keeps up to three
pending answers, budgets new answers and repeated replies, and activates and
commits only the exact binding it verified. A binding is named by an id that is
unique across every group on the board until it restarts, so a call kept from
an earlier membership or another group cannot select a different binding.

Pending answers: a start for the same epoch never displaces an answer that is
still within its time limit (all slots full means "try again later"). Accepting
a start for a higher epoch drops the answers for lower epochs, which can no
longer finish; with a D-074 catch-up this includes an answer whose confirm was
delayed, and that confirm then fails.

A pair step cannot be interrupted by its own callbacks: it refuses roster
changes and nested pair steps while it runs, the group state cannot be moved
away under it, and it copies the incoming message first, so a callback that
changes the caller's copy changes nothing that is verified, signed or issued.

Cancel retires the own start and the pending answers. A roster conflict drops
the own start, the pending answers and a binding not yet in
ACTIVATION_POSSIBLE; none of these resume after "keep current". A binding in
ACTIVATION_POSSIBLE is held through the conflict: it cannot commit while the
conflict lasts, and may commit once the board keeps the same roster it names.
Stopping trust ends every pair record.

**Activation intent.** ACTIVATION_POSSIBLE is never retired by a start alone,
however authentic: it stays until a new binding for the pair verifies, or until
restart, where it is retired and never resumed. A board holding it may start a
rekey itself, so two uncertain boards always reach a fresh epoch. Once a
D-074 catch-up shows that the peer used a later epoch, the older binding can
no longer commit; the intent stays only so the board knows to recover.

**Epoch numbers (owner decision D-072).** An epoch counts as used only when a
board has verified the complete binding signed by both boards. A start that is
lost uses up nothing; the retry reuses the number with fresh values. A board
proposes one above its highest used epoch and accepts that number, or one past it
to catch up with a peer that used one this board missed (the peer verified the
binding, this board never saw the confirm). A start signed by the peer at one
past the next number counts as proof that the peer used the number between
(owner decision D-074), so the answering board records that number as used at
once; the gap therefore never grows beyond one, however many confirms are lost.
Only that peer can make such a claim, so a dishonest member can only burn
numbers on its own pair with this board. At the last epoch the pair is exhausted and
must be set up again from a new roster entry. When two starts cross, an expired
own start does not count, an introduced pair start beats a rekey (its sender has
no history and would refuse the rekey), and otherwise the higher epoch wins, then
the lower fingerprint; the loser drops its own unanswered start, which used
nothing, and answers.

**Time limit (owner decision D-073).** Clocks of two boards are never compared.
The starting board allows 60 seconds from sending its start; the answering board
allows 60 seconds from receiving it. The worst-case total is 120 seconds.

**First pairing or rekey after an interruption.** A board starts an introduced
pair until it has history for this membership (it signed a binding over the
peer's verified part), then rekeys. A responder answers an introduced pair start
whenever the acceptance and role order hold, and a rekey only with that history.
Two boards left in different states by an interruption therefore always reach a
fresh epoch.

Triggers: boot and recovery (D-059). Any other trigger, such as a counter
domain running out, is a separate decision. Both boards must be in radio range
at the same time. After a reboot a board may need up to 28 rekeys (4 groups
and 8 conversations); they are staggered and rate-limited. The app shows each
pair as "reconnecting" until it is ready; messages to that member wait or
report undelivered.

For the outside-group conversation design (OT-0309 to OT-0311): add a local
"end conversation" that deletes the pair and refuses that identity's later
rekeys, and show "reconnected with X" after each automatic rekey, so a
conversation with a stolen board can be ended.

## Recovery of interrupted pairs

A pair whose setup or rekey was interrupted (left in PREPARED,
ACTIVATION_POSSIBLE or an uncertain state) is **never resumed**. On the next
contact, both boards retire that attempt and run a fresh handshake at the next
epoch. This is the comparison step from the
[recovery design](ENROLLMENT_CANDIDATE_RECOVERY_V1.md), started automatically
instead of by a local request on both boards. Old traffic keys are never
reused, and a pair is never assumed active because one side was.

## Group messages

Each group message is sent as one Packet-v1 frame per other member, over that
member's pair.

**Packet-v1 type values for groups** (proposed; added to the Packet-v1 profile
when implemented):

| Type | Purpose |
|---|---|
| 1 | data (existing, outside-group conversations) |
| 2 | acknowledgement (existing) |
| 3 | group text |
| 4 | group quick status |
| 5 | group position |
| 6 | roster transfer |
| 7 | roster fetch |
| 8 | leave |
| 9 | group closed |

**Group frame header** (14 bytes, replaces the 8-byte OTXT envelope for group
text):

| Bytes | Field |
|---|---|
| 1 | format version |
| 1 | body length |
| 4 | roster version the sender used |
| 4 | first 4 bytes of that roster's digest (detects disagreement; not used for authentication) |
| 4 | sender's group message number |

**Size.** The Heltec V4 radio driver reports a 154-byte MTU; Packet-v1 uses 60
bytes, leaving 94 plaintext bytes. The group header takes 14, leaving **80
bytes** for a group text (UTF-8, same text rules as OTXT v1). Group texts are
limited to 80 bytes and are never split across frames. If the radio settings
change the MTU, this limit must be recomputed.

**Message numbers.** A sender's group message numbers never repeat, including
after a reboot: they are reserved durably in batches, like the outbound counter
leases. A receiver that sees the same sender and number with different content
treats it as a conflict and shows it.

**Acknowledgements** on group pairs carry the receiver's roster version and
digest prefix, so a sender learns of a newer roster the first time any
up-to-date member acknowledges anything. Today the Packet-v1 profile fixes an
acknowledgement at exactly 8 plaintext bytes (the acknowledged counter), so
this is a proposed profile change: group-pair acknowledgements are 16 bytes
(counter 8, roster version 4, digest prefix 4), still unfragmented.
Outside-group acknowledgements stay 8 bytes.

Receivers:

- accept only from members on their newest roster with an active pair;
- show each message once (by sender and message number), with gaps visible;
- refuse group positions when the location rule is off.

Senders keep delivery state per member ("delivered to 4 of 5"). There is no
relaying.

## Roster delivery

**Fragmented transfer (D-060).** A 6-member roster is about 390 bytes without
the per-member fingerprints (recomputed on receipt), so it does not fit in one
94-byte frame. Roster transfer (type 6) is the only group frame that may be
split: it uses the Packet-v1 fragment fields (index and count, authenticated,
at most 16 fragments, every non-final fragment full), so a 6-member roster is
5 fragments at the 154-byte MTU. Each fragment uses a fresh durable counter, as
the profile requires. The receiver reassembles with the bounded
[protected reassembler](../protocol/PROTECTED_REASSEMBLY_V0.md): one roster in
flight per direction per peer, a timeout that discards incomplete state, and
rejection of inconsistent counts, indices or message metadata. The roster is
checked (signature, chain, limits) only after it is complete; a partial roster
is never used. Group texts, statuses and positions stay one frame.

The admin board sends each new roster over every remaining pair and retries
until each member acknowledges it. The admin's app shows progress, for example
"removal reached 3 of 4". Members also receive rosters from each other (see
[The roster](#the-roster)).

## Leaving and removal

- **Removal.** The admin board publishes a roster without the member and
  delivers it as above. Every member that accepts it first stores the roster,
  then deletes its pair with the removed member and refuses later traffic from
  it. On boot, any pair whose peer is not on the stored roster is treated as
  retired. The admin also tells the removed member, then deletes that pair.
- **Leaving.** The member's board sends a leave request (type 8) to the admin
  and to the members it can reach, and deletes all its pairs in that group.
  Others show "left (not yet confirmed by admin)" until the admin publishes a
  roster without it.
- **Stop trusting the group.** Any member can do this locally at any time: its
  board deletes all its pairs in that group and refuses all later rosters from
  that admin.
- **Admin leaving or deleting the group** sends a signed "group closed"
  statement (type 9) to every member; members delete the group on receiving it.
- **Silent members.** A board stops sending positions to a member it has not
  heard from with the current roster for **10 minutes**, and resumes when it
  hears from that member with the current roster.
- Other pairs are not touched: the removed member never held their keys.

The app says plainly that a removed board can keep receiving from boards that
have not yet updated, and that nothing already received can be taken back.

## Lost, stolen or reset boards

- A member's board that is factory reset has a new identity. To rejoin it
  pairs in person with the admin again; the admin removes the old identity.
- If the admin's board is lost or reset, nobody can change the roster. Members
  can keep messaging with the current roster. V1 recovery is to create a new
  group; there is no second admin.
- If the admin's board is **stolen**, whoever holds it holds the admin signing
  key. Protections: additions need the admin board's own in-person enrollment
  receipt (a thief must enroll a device in person on the stolen board); every
  member must tap to accept each new member; every roster change is shown on
  every board; and any member can stop trusting the group. Members should do
  that as soon as they learn the admin board is lost.

## Storage

Estimated per pair, in the current two-slot stores: identity binding and
evidence about 380 bytes, membership 128, four counter and replay stores 128
each, plus activation and session generation: about 1.5 KB. At 28 pairs that is
about 40-45 KB, plus about 0.6 KB per group roster in two slots. This fits the
1 MB `ot_state` partition of the evaluation layout, not the 12 KB ordinary NVS.

Required before OT-0305:

- an indexed per-pair layout keyed by (group name, peer) in `ot_state`, keeping
  each store's write and read-back order (today each store holds one pair);
- roster first, then pair changes, so an interrupted removal is safe;
- durable group message number reservations;
- a flash-wear check: one group message uses 5 counter reservations, at one
  position per minute;
- factory reset covering every new namespace.

These figures are estimates from the evaluation stores, not measurements.

## Radio budget

From the I-005 airtime estimate (workspace review, 2026-11-12), one channel,
without acknowledgements or retries:

| Group | Settings | Positions every 60 s | Plus one text per member per minute |
|---|---|---|---|
| 6 members | SF7, 125 kHz | about 7% busy | about 20% busy |
| 6 members | SF9, 125 kHz | about 23% busy | about 64% busy |

So groups that share location need the faster settings and a position
interval of at least 60 seconds. Rekeys after a reboot add up to 28 handshakes
per board (staggered). The final radio settings, handshake and acknowledgement airtime,
and the US915 regulatory check remain open and belong to radio selection.

## What a misbehaving admin or member can do

- The admin can add an identity it enrolled in person on its own board, and
  members must accept it. Substituting a different key under a known name is
  shown as a removal plus a new device.
- The admin cannot read messages between two other members.
- A stolen admin board is covered in
  [Lost, stolen or reset boards](#lost-stolen-or-reset-boards).
- An admin board that signs two rosters with one version, or is restored from
  an old image, is detected as a roster conflict.
- An admin can sign version 2^32-1 and freeze the group; it could delete the
  group anyway.
- A member can read only messages sent to it, cannot change the roster, and
  can be removed.
- Replaying an old roster or an old handshake does nothing.

## What remains from group lifecycle v0

Still applies to V1 groups: the single-admin risk label; the list of visible
states (adapted: not a member, invitation states, join pending, waiting for
acceptance, active, reconnecting, roster conflict, left, removed); and the
reset wording (each destructive confirmation says what is erased). Everything
else in group lifecycle v0 is superseded for V1 groups.

## Review record

Independent security review, 2026-11-12 (read-only, by a reviewer who did not
write the design; workspace file `I-005-group-design-security-review.md`):
acceptable with listed changes. Its ten required changes and where this
revision addresses them:

| # | Required change | Section |
|---|---|---|
| 1 | Restart and automatic rekey | [Restart and rekey](#restart-and-rekey) |
| 2 | Recovery of interrupted group pairs | [Recovery of interrupted pairs](#recovery-of-interrupted-pairs) |
| 3 | Full introduced-pair binding, typed authority, who starts | [Introduced pairs](#introduced-pairs) |
| 4 | Tamper-evident rosters | [The roster](#the-roster), [Group messages](#group-messages) |
| 5 | Additions only from the admin board's in-person receipt; changes shown to all | [Admin rules](#admin-rules), [The roster](#the-roster) |
| 6 | Stolen-admin protection | [Leaving and removal](#leaving-and-removal), [Lost, stolen or reset boards](#lost-stolen-or-reset-boards) |
| 7 | Bounded removal spread | [Roster delivery](#roster-delivery), [Leaving and removal](#leaving-and-removal) |
| 8 | Storage limits and layout | [Roles and limits](#roles-and-limits), [Storage](#storage) |
| 9 | Explicit amendments to the accepted documents | [What changes in the accepted documents](#what-changes-in-the-accepted-documents) and the notes in those documents |
| 10 | Group text size and Packet-v1 type values | [Group messages](#group-messages) |

Its lower-rated findings are also covered: the location rule in the join
transcript, rate limits on fetches and handshakes, "left (not yet confirmed)",
and name reuse across groups.

Re-review of revision 2, same reviewer, 2026-11-12: still acceptable with
listed changes; 7 of the 10 met, 3 partly met. This revision adds its fixes:
"added in" roster version and the rekey start rule (N3), accepting a signed
roster across a gap (N4), replay protection for rekey starts (N2), naming of
outside-group conversations (N5), the "end conversation" note (N6), the digest
prefix as early warning only (N7), up to 28 rekeys after a reboot (N9) and
triggers limited to boot and recovery (N10). Roster transfer size (N1) is
settled by owner decision D-060: rosters only are fragmented (see
[Roster delivery](#roster-delivery)). Also noted while writing that fix:
group-pair acknowledgements need 16 bytes instead of the profile's fixed 8
(see [Group messages](#group-messages)). The reviewer stated that once N1 to
N4 are written in, no further security re-review is needed before host
implementation; host implementation and its tests still need review.

Still open, for implementation: the handshake suite, key derivation and
fingerprint encoding (not selected); the production storage layout; handshake
and acknowledgement airtime; physical reboot and range behaviour.

## Implementation order

1. Roster record, digest chain, signature domain and acceptance rules,
   including roster conflict (host tests).
2. Introduced-pair handshake and typed authority on top of the selected
   pairwise handshake; automatic rekey and recovery (host tests: substituted
   identity, look-alike group ID, same version with a different roster,
   replayed handshake, interrupted activation).
3. Indexed per-pair storage, durable message numbers, group header, delivery
   state and duplicate suppression.
4. Android group screens bound to the board's roster (OT-0305, OT-0306,
   OT-0307), including member acceptance and stop trusting.
5. Persistence and interruption tests (OT-0308), then a three-board bench
   test.
