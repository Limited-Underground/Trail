# MeshTrail group joining by broadcast and PIN

Status: design for owner decision D-169 (2026-11-23). Built after V1. V1
groups join with the QR invite on phones ([Group design](GROUP_PAIRWISE_V1.md),
"Invite and Join"). Needs an independent security review before it is built,
like the QR route (D-058).

## Goal

Any device (phone, MeshTrail Console or another device) can join a group
without a camera. The group's administrator device broadcasts that the group
is open; the joiner sees it, enters a PIN the administrator gives them, and
both boards then finish the same secure join as the QR route.

## One end state for both routes

Both routes end on the joiner board with the same **pending join**: a signed
invite from the admin board that the joiner board has verified and claimed
once (`GroupPendingJoin` in `companion_group_owner.hpp`). Everything after it
is shared and is built once:

1. the authenticated QR handshake between the two boards (`qr::Session`:
   start, reply, admission, both board confirmations, signed receipts);
2. the durable receipt and the single-use claim of the invite ID
   (`ot_group_join`);
3. activation of the admin pair record and the signed roster N+1.

The QR route carries the signed invite inside the code. The PIN route carries
the same signed invite over the radio, protected by a key that only a device
knowing the PIN can derive. The invite's rendezvous byte (offset 5, today
1 = QR) becomes 2 = broadcast and PIN, so a transcript always shows which
route admitted it. Nothing else becomes join authority.

## What is broadcast, and over which link

The joiner's **board** listens, not its phone. That is what makes the route
work for any device: a phone or a Console only asks its own board, over its
existing protected link, which groups are open nearby.

- **Link:** LoRa, board to board, in a single frame (well under the 236-byte
  cap). No BLE advertising by phones: it would leak the group to anyone near
  the phone and would not reach a Console's board.
- **When:** only while the administrator has opened joining for that group:
  one window of at most 3 minutes, one beacon at most every 10 seconds, ending
  early when a join completes or the administrator closes it. Exact airtime
  waits on the qualified radio profile and the 112 ms question (I-079), as for
  every new frame (D-163).
- **Beacon contents (about 60 bytes):** magic and version; the group label
  (up to 24 bytes, what people already see as "only the group name");
  an 8-byte rendezvous ID that is fresh for each window; the CPace session ID
  (16 bytes); seconds left in the window. It carries no admin key, group ID,
  roster digest or member list. Those come only after the PIN step.

## Guessing-resistant PIN handshake

The D-085 pairing code is not enough here: it only sets the pair's group
number and trust is confirmed by comparing on both boards. A broadcast is
heard by anyone in range, so the PIN is the only secret and must not be
testable offline from a recording.

**Recommendation: CPace** (the CFRG balanced PAKE) over ristretto255, using
the pinned libsodium 1.0.22 (`crypto_core_ristretto255_from_hash`,
`crypto_scalarmult_ristretto255`) and SHA-512, as already shipped on the
board. No new library.

1. The administrator's board makes a random **6-digit PIN** for the window and
   shows it on the administrator's screen and app. It is never broadcast.
2. The joiner enters the PIN on their device; it goes to their board over the
   protected link.
3. The two boards run CPace with the PIN, the session ID from the beacon and
   both boards' identity public keys in the channel identifier: one message
   each way (32-byte points), then a key confirmation each way. Each side
   derives a 32-byte session key.
4. The admin board sends the signed invite (rendezvous 2), encrypted and
   authenticated under that key. The joiner board decrypts, verifies the admin
   signature and the key confirmation, and claims the invite ID. That is the
   pending join, and the shared steps above follow.

An eavesdropper learns nothing it can test PINs against. An active attacker
gets one PIN guess per attempt, enforced by the admin board.

**PIN length and attempt limits.** 6 digits (1,000,000 values), like the
D-085 code, so it is easy to read out. The admin board allows **3 failed
attempts per window**, then closes the window; a new window gets a new PIN and
a new session ID. One attempt at a time per window. The chance that an
attacker joins in one window is at most 3 in 1,000,000. Failed attempts are
counted in memory: a restart closes the window (it fails closed). Option for
the owner: 8 digits lowers that to 3 in 100,000,000 at the cost of a longer
code.

The administrator still confirms the new member on their board, and every
existing member still accepts them with one tap (D-059 part 4). The PIN
replaces only the QR code as the way the signed invite travels.

## Storage under D-163

No new storage namespace and no new resident entries:

- the window, PIN, CPace state and attempt counter live in memory only;
- the joiner's single-use claim reuses the existing `ot_group_join` record
  (19 entries), keyed by the invite ID as for QR;
- receipts and pair records are the shared ones already budgeted by D-163.

The modeled total stays 12,441 of the 16,128-entry limit.

## How the V1 code is shaped for this

- The join owner accepts one input, a verified signed invite, and produces one
  output, the pending join. The PIN route adds a second producer.
- The claim store, the board's invite issuer and the pending-join lifetime do
  not depend on how the invite arrived.
- The app's Group tab sends Join with invite bytes; the QR codec sits behind
  one small interface (`V1InviteQr`), so a "join with PIN" screen is a new
  input next to it, not a change to the join path.

## Work to build it (after V1)

1. Security review of this design.
2. Firmware: beacon codec and scheduler; CPace session (host tests with
   vectors, wrong-PIN and replay cases, the 3-attempt limit across interleaved
   joiners); rendezvous 2 accepted by `verify_descriptor`; encrypted invite
   transfer; protected commands Open Joining (admin), List Open Groups and Join
   with PIN (joiner).
3. App: open and close joining with the PIN shown; a nearby-groups list; a PIN
   entry screen. MeshTrail Console: the same three commands.
4. Bench: two boards and both device kinds, wrong PIN, window expiry and
   restart during a window.

Rough size: comparable to the QR route itself. That is why D-169 builds it
after V1.

## Open points for the owner

- PIN length: 6 digits (recommended) or 8.
- Whether the beacon may show the group label to anyone in range (recommended:
  yes, as the Group screen already says), or only a generic "group open".
