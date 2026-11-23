# F5 R2: first board pairing in the product firmware (plan)

Status: local host/build candidate, 2026-11-18. Part of #232. Decisions: D-076 (LoRa carrier), D-077 (Packet-v1), D-078 (test limits), D-082 (phone admission), D-112 (recoverable pairing), D-111 (protected phone outcome). Physical acceptance and independent review before merge remain open.

## What already exists

The whole enrollment sequence exists and is host-tested. It was also run on two real boards through the USB evaluation target `heltec_v4_enrollment_candidate_eval`, where a PC relayed every message between the boards:

1. `EnrollmentCandidateSession::start_pairing(role, group)` routes a saved decision into recovery; with no saved decision it captures the admitted request and creates the board identity if absent (`EnrollmentIdentityStore::initialize`, real entropy). It also allocates a fresh session generation.
2. `export_candidate` and `receive_candidate` exchange public identities. A received identity is an untrusted candidate.
3. The fingerprint review shows the board's own key, then the peer's key (all 64 hex digits). A 1-3 s press on each board confirms. `HeltecEnrollmentInputArbiter` already owns the BOOT button and the OLED for this.
4. `finish_review` then:
   - possession proofs;
   - clock marks;
   - device-issued invitation;
   - handshake;
   - transcript confirmation;
   - authenticated confirmation controls;
   - durable Prepared receipts;
   - exact local public commit;
   - signed Committed receipts and peer-proof finish.

The product target (`heltec_v4_bench`) already admits the phone's start-enrollment request (action 6) into `SelectedEnrollmentRequestOwner`, with a 5-minute window (D-110; 120 s under D-087) that is cancelled on disconnect. The app-task board runtime consumes each exact admitted request once and rechecks its original link and deadline throughout the attempt.

## What R2a adds

- **Pairing coordinator.** A product pairing coordinator in the bench target, modelled on `candidate_runtime.cpp`. It runs in the app task and consumes the admitted request. It drives `EnrollmentCandidateSession` without a USB operator.
- **LoRa wire.** A small versioned LoRa frame for each session message (candidate, offer, possession, clock mark, invitation, handshake, control, status). Every received frame stays untrusted. The largest frame is 194 bytes, under the product radio's 255-byte MTU. The product radio (R1) carries them under D-110 (5-minute window; was 120 s under D-087), at most 4 accepted sends per frame (D-107) and the hello for the whole window (D-106), and the D-079 transmit permission.
- **Product storage.** Product NVS namespaces for identity, journal, binding and boot, included in factory reset.
- **Screen.** The OLED shows a pairing status line and the review pages.

Protected phone results are implemented in the local D-111 increment below. Retained fresh-session Packet-v1 traffic is a separate candidate; recovered pairing never restores traffic keys.

## Recoverable pairing decision (D-112)

The dedicated double-bank completion journal records the exact transaction,
verified peer identity, original role/group/generation/epoch and signed decisions.
It stores no traffic keys. A locally Prepared vote survives disconnect, expiry and
restart; a missing final radio copy cannot turn it into a clean refusal. A fresh
phone request with the original Start/Join role and a new matching routing code
runs a fresh-nonce recovery exchange for that saved transaction. The code does
not replace the saved group or peer authority.

Both Prepared decisions complete the exact public commit; an authenticated
negative decision before a positive vote undoes the uncommitted pairing. When
the peer is absent, an unresolved decision remains recoverable and cannot admit
traffic or a fresh generation. A verified terminal negative receipt is archived
per generation before cleanup so an older pending peer can still obtain it after
a later attempt. Unknown transactions and malformed or conflicting stores fail
closed. Initial descriptor/save and cleanup interruptions have an explicit
negative-only recovery path; they never invent a positive vote.

The target uses completion tuple namespace 11 (generation 0 for the current
journal and 1..32 for signed terminal history), under the existing reset-covered
`ot_pair`/`ot_state` store. Its worst-case budget is 2,364 NVS entries, below one
eighth of the partition. This branch keeps tuple name 11 and integrates first;
Messaging moves the conflicting D-113 F5/data records to 12 and above (I-074).
The current key has one hexadecimal namespace digit: 12..15 fit that format,
while 16+ requires a format/parser change. Any added names still require an
explicit inventory, budget, reset and compatibility review. Older firmware
rejects the added inventory; restoring an image alone does not establish
storage compatibility.

## Recovery journal containment (review M1/M2)

Recovery classifies both public-journal banks before any membership or journal
mutation. The selected record must match the signed binding, generation/parity
and requested recovery intent. Positive completion needs its permitted stage
and room for every remaining advance. Exact negative cleanup uses a separate
entry point: already-aborted state is a no-write retry, including at the counter
limit; a staged record needs one remaining advance. Completed state cannot
enter negative cleanup.
The other bank must be blank, the allowed retired predecessor, or an exact
recognized interrupted transition for this binding. Unknown/corrupt/foreign
bytes remain untouched. Classification replays the actual erase/payload/header/
marker order in RAM and does not repair storage by guessing.

Completion initialization likewise requires canonical OTPC first-record fields,
sequence, unvoted phase/flags, reserved bytes and write order. An installed marker
byte proves that all 316 payload bytes must already be canonical; an erased or
partly retired marker alone never makes arbitrary bytes empty. Bound successor
and predecessor interruption checks preserve genuine fault recovery while
unrecognizable state remains uncertain. Storage format, key numbering and budget
are unchanged. Exact regression and integration evidence is in REPORT-5.md.

## Queued launch eligibility (I-073)

Queue acceptance reserves one copy; it is not a radio launch. The coordinator
captures that exact accepted copy's original remaining window and its
pre-increment first/resend floor. Its launch query ages the captured budget,
rechecks live session deadlines while running, and retains only the original
budget during completed-session linger. It also rejects retired/proven-received
copies, clock rollback, pending receive processing and the receive quiet window.
The query neither advances the script nor changes the retry/refund token.

The existing radio permission callback retains the independent region check and
adds the queued pairing owner check. It samples the monotonic clock on each
call, including the driver's check immediately after CAD. The runtime rechecks
the original phone authority and samples again after that potentially blocking
lookup. The one app task owns the queue tag and copy token. A failed permission
check uses the existing provably-prelaunch discard counter; its exact copy is
refunded once before another admission. TX-start failure remains uncertain and
receives no refund. D-106/D-107 limits, 15/10 s creation reserves, and 6.5/3 s
first/resend floors remain unchanged.

Tests cover queue wait, CAD back-offs 1/2/3 and time crossed within CAD, for v1
controls and v2 controls/Prepared/Committed, with equality checks and real-session
integration. The composed bench test compiles the actual app callback, driver
and coordinator against simulated SDK/chip/session seams. It does not establish
physical CAD or RF acceptance. Exact results and the three-commit review handoff
are in D:/LimitedUnderground/Build/codex-pairing-design/out/REPORT-4.md.

## Protected phone outcome (D-111)

The metadata-only ProtocolInfo descriptor originally advertised pairing results
with value 1. [F11 firmware](../../firmware/components/device_parameters/README.md)
uses value 2 to advertise pairing results and device parameter reads. After the
unchanged V0 protected Ready bootstrap, the app opts into OTX1/OTN1 status.
The 40-byte snapshot preserves ordinary telemetry and adds state/reason,
the confirmed peer suffix, originating action exchange/nonce and an independent
result revision. Discovery contains no outcome; a result read requires the
current authorized Ready owner. A result cannot establish Ready or grant trust. Every handed-off pairing action
sets a new result-origin fence before platform write, including refused or
uncertain outcomes; an old result cannot replace that attempt's refusal.

The runtime captures the exact admitted ticket before consuming its session
request and publishes before teardown. Only successful session/coordinator
startup permits projection of a cached journal: failed identity/binding checks
cannot expose an old successful phase. Local durable commit reports
"Paired with board XXXX" and "Saved on this board"; durable authenticated peer
Committed proof reports "Both boards paired". XXXX is the opposite confirmed
identity's final two bytes in four uppercase hex digits. Archive-only recovery
cannot project an unrelated current journal. Unresolved saved decisions report
incomplete rather than assumed success or refusal.

One boot-local result lasts ten minutes from a terminal transition. Repeated
reads/publications do not extend it. A newer admission supersedes it; reset,
revocation, owner change and cold boot retire RAM history. A freshly authorized
same-owner reconnect may read terminal history as "Last pairing attempt";
a running attempt is not exposed on a new link. Exact ended-request release
happens after session cleanup and the final-response linger, preserves result
history and cannot cancel a newer admission.

Foreground polling remains five seconds through local save and returns to
normal after peer proof. Incomplete offers explicit Resume pairing: use the
original Start/Join phones and a new matching code. Resume sends no action
until the person submits the form; Cancel only exits that form. No reply carries
a check-code hint without asserting a mismatch. Old/stale-feature firmware
falls back to ordinary status and board guidance. Hardware verification remains
open; see the task report's independent checklist and owner-present procedure.

## Role and group (decided: D-085, option A)

`start(role, group)` needs, on each board:
- a role (initiator or responder);
- the same nonzero group number.

The existing security rule says a received peer value must never choose the local role or group. On the USB evaluation, the person typed them on each PC.

| Option | How it works | Security rule | Ease |
|---|---|---|---|
| **A. Phone chooses role, shows a pairing code (recommended)** | Phone 1: "Start pairing" shows a 6-digit pairing code. Phone 2: "Join pairing", and the person types that code. Each app sends action 6 with role and code; the code is the group. | Kept: both values come from the owner's own phone. | One code typed once. |
| B. Derived on the boards | Boards exchange identities first. The lower key becomes initiator, and the group is derived from both keys. | Changed: peer values select role and group. Needs a security review and a decision record. | Nothing to type. |
| C. Typed on the boards | Not possible: one button only. | - | - |

Decided 2026-11-14 (D-085): option A. Done in the first R2a slice: action 6 now carries the role at byte 16 and the code at bytes 17-19 (little-endian), validated in the codec and in `SelectedEnrollmentRequestOwner`. The R2b app brief was updated before Codex started it.

## R2a slices

1. Action 6 role and code, and the request carries them (done).
2. LoRa pairing frame codec, `pairing_frame.hpp` (done; host tests).
3. Pairing coordinator, `pairing_coordinator.hpp` (done; host tests). It runs each board's fixed script, buffers early frames, ignores other attempts and traffic, and recovers any single lost frame:
   - a board repeats each frame it sent until a later frame from the peer proves the peer received it (in the peer's script, that frame comes after the receive of ours); hearing the peer's copy of a frame exchanged at the same time is not proof, because the two copies may have collided;
   - a repeated frame from the peer identifies exactly which of our frames it still needs (not for hellos, which repeat on their own timer, and not for a frame already proven received; otherwise the two boards echo repeats back and forth).
   It stops resending after `max_sends` (4, D-107; 2 under D-087) but keeps listening until the request window (5 minutes, D-110) ends, because the other person may still be reviewing. Exception (D-106, I-057): the first frame, the candidate "hello", repeats for the whole window (up to 70 sends: every 5 s from the starter, every 4.3 s from the joiner), because the second phone's person is still typing the code when the first board starts; with 2 sends the boards could only pair if both started within about 2 s.
   The radios are half-duplex and cannot hear each other while sending, so two frames that overlap on air are both lost (an offer, 115 bytes, is on air 195 ms at SF7; the largest frame, 194 bytes, 307 ms). The boards are kept out of step:
   - different hello intervals for the two roles (above);
   - the joiner holds each new frame 0.6 s and resends other frames every 2.3 s instead of 3 s;
   - neither board transmits until 0.5 s after it last heard the peer, so it never answers into the rest of the peer's burst (a simple stand-in for listen-before-talk).
   The product uses version 2 completion frames. After both people confirm the exact transcript, authenticated controls precede the durable Prepared exchange. Each board commits the exact public write set only after both Prepared votes, then sends a signed Committed receipt. Finish requires a verified peer Committed receipt. The original 22-step version 1 script remains available to existing callers; it is not the product completion path.

   The first final frame requires 15 s remaining and later final frames require 10 s at creation. Control, Prepared and Committed offers retain the 6.5 s first-copy and 3 s resend floors, including a refunded pre-launch copy. The first positive vote rechecks the signed invitation and original phone deadline after payload readback, immediately before the durable decision marker. No later live send can extend those deadlines. The outer request/attempt remains 300 s, the board-issued signed invitation 180 s, the hello limit 70 and other accepted sends 4. At actual launch, I-073 now requires a fresh coordinator check before and after CAD, including each back-off; see the queued-launch section above. Independent review and hardware acceptance remain open.

4. Session adapter: converts session values to and from the exact frame payloads, and drives the local review and transcript confirmation through the existing review API (tests in the security suite, with real crypto).
5. Target composition in `heltec_v4_bench`, in steps:
   - 5a (done). `pairing_nvs_storage.cpp/.hpp`: the pairing stores in a standard NVS instance inside `ot_state` (D-086), modelled on the evaluation storage. Factory reset unmounts it before erasing `ot_state`. Compiled only with the radio.
   - 5b (done, `board_pairing_runtime.cpp/.hpp`). Pairing runtime:
     - consume the admitted request;
     - hand the board identity from the load-only boot owner to the session;
     - fence factory reset with a storage lock and a close before the reset commits (the product does not use the evaluation's `EnrollmentCandidateReset` gate);
     - start it with the request's role and code;
     - drive the coordinator.
   - 5c. `app_main` wiring, link-test-only (done with 5b):
     - service the runtime and send its frames when transmit is permitted;
     - pass non-beacon frames to it;
     - pause beacons while pairing;
     - close it before factory reset.
     OLED status line (done 2026-11-15): on the normal screen, row 2 shows "PAIRING..." while an attempt runs, then "PAIRED - THIS BOARD" or "PAIRING FAILED" for 60 s. "Paired" names this board only, never the peer. The review pages draw through the arbiter as before.
   - 5d. Owner-present two-board session.


## Message order over LoRa (from `enrollment_candidate_session_tests.cpp`)

A is the initiator ("Start pairing"), B the responder ("Join pairing"). Frame types are in `firmware/components/protocol/include/opentrail/pairing_frame.hpp`.

| Step | A | B | Frames |
|---|---|---|---|
| 1 | `start(initiator, code)`, `export_candidate` | `start(responder, code)`, `export_candidate` | candidate, both ways |
| 2 | `receive_candidate`, review: own key, peer key, hold-press, `finish_review` | same | (local only) |
| 3 | `prepare_possession(B offer)` | `prepare_possession(A offer)` | offer, then possession, both ways |
| 4 | `clock_mark`, `receive_clock_mark` | same | clock mark, both ways |
| 5 | `issue_invitation` | - | invitation payload and signature, A to B |
| 6 | `sign_invitation`, `begin_handshake(B cosign)` | `sign_invitation`, `begin_handshake(A cosign)` | invitation cosign, both ways |
| 7 | `next_handshake` x2 | `next_handshake` x1 | handshake A to B, B to A, A to B |
| 8 | transcript confirmation: hold-press | same | (local only) |
| 9 | `next_control`, `receive_control` | same | control A to B, B to A, A to B, B to A |
| 10 | `commit`, `ready` | same | - |

Coordinator rules this implies:
- Each received frame is passed to the session at most once. A repeated identical frame is ignored, and a different frame of an already-consumed type ends the attempt.
- A board repeats its last frame until the next expected frame arrives, within the D-078 limits.
- Everything is bounded by the 5-minute request window (D-110), which the phone connection must hold. After a board's clock mark, its signed invitation is valid for 180 s (D-110), which must cover the code check, the final messages and the save. Only board pairing issues and expects 180 s (`EnrollmentCandidateSession::set_invitation_window` before start); every other path, including enrollment evaluation and rekey, keeps the 60 s default. The decoding ceiling is 180 s so that board-paired records still re-verify later.

## Slice 5 storage (corrected 2026-11-14)

An earlier note here said the pairing stores could use `ot_state` like the confirmation evaluation. That was wrong:
- `ConfirmationNvsBackend` opens namespaces in the **default** NVS partition; its `ot_state` argument is only a label check.
- Nothing in the product writes the raw `ot_state` partition. Factory reset erases it, along with the `ot_v1_owner`, `ot_name_v1`, `ot_region_v1` and `ot_identity_v1` namespaces.

Budget, from the evaluation target's own constants (`candidate_nvs_storage.hpp`): 37 fixed slots plus 7 per generation (capacity 2), at 4 NVS entries per 64-byte slot, comes to 204 entries. The default NVS (12 KB) has about 252 usable entries in total, already shared with the Bluetooth bond, owner record, name, region and the board identity (10 slots, about 40 entries). The pairing stores do not fit there.

Options (owner decision):
- A. A dedicated pairing NVS partition, taken from the unused end of `ota_1`. The bench app never uses OTA. This reuses the board-tested evaluation storage design, and factory reset gains one partition. It changes the partition table: both boards need one full reflash, and the Loader's layout checks follow Trail's `partitions.csv`.
- B. New crash-safe storage over raw flash in `ot_state`. No layout change, but it is new security-critical code.
- C. Default NVS. Does not fit.

## Before the two-board session (5d)

- The phone app must send action 6 with a role and code (R2b, Codex brief ready). Nothing else creates a product pairing request.
- Recommended: an independent review of 5a to 5c before the boards run it.
- Known limits to observe on the boards:
  - a frame the radio refuses to queue counts as lost, which the coordinator's repeat logic covers;
  - frames are sent only when the region permits transmit (D-079);
  - after a failed attempt, retained enrollment records may block a fresh first enrollment until factory reset (the session's rekey and recovery paths are not wired).

## Independent review and fixes (2026-11-14)

The Codex review of PR #253/#254 (`REVIEW-R2A.md`) requested changes. What changed:

- **R2A-1, lost final control:**
  - A local step, including commit, now waits until every produced frame has been accepted by the radio.
  - After local commit the runtime keeps answering the peer for 15 s (`kLingerMs`), so the initiator's repeat recovers a lost final control (test `test_final_control_loss_needs_linger`).
  - Local "paired" means this board committed; it is not proof that the other board did.
- **R2A-2, send accounting:**
  - `service()` only offers a frame. The caller reports `sent(accepted)`, and only an accepted frame counts against the send limit (4, D-107; 2 under D-087).
  - A new attempt starts only when the radio runs and transmit is permitted; a held transmit spends nothing.
  - Tests: `test_refused_offers_are_not_counted`, `test_held_transmit_spends_nothing`.
- **R2A-3, cancellation:**
  - Every active tick re-reads the exact phone request, and the attempt ends as soon as it is gone.
  - The attempt also ends if the radio stops.
  - A frame already queued in the driver's one TX slot may still launch once (the driver checks the region at launch); that is bounded and noted.
- **R2A-6, reset fence:** one recursive lock serializes every pairing-storage operation with `retire()` and the factory-reset hook, so a reset from the BLE side waits for an in-flight write and every later write fails. Session close results are checked and logged.
- **R2A-7, wording:** D-087 sets the pairing limits; this plan's frame-size and reset-gate statements are corrected.
- **R2A-8, NVS wrapper leak:** the pairing storage is mounted once per boot and retired only by factory reset, so there are no repeated mount and deinit cycles.
- **R2b dependency:** the board now answers a refused pairing request with a correlated rejected result:
  - `busy` (new reason 7) when one is already pending;
  - `policy_denied` for an invalid request;
  - `unavailable` when retired.

  Lane and security-gate failures stay unanswered. Expected bytes for busy: `4F 54 52 30 00 00 03 07 06 00 ...` (20 bytes).

Accepted for now, with reasons:
- **R2A-4, same-code stale traffic:** it cannot grant trust (the session authenticates and both people press), but it can disrupt an attempt. The person retries with a new code.
- **R2A-5, retry after durable work:** first enrollment refuses when retained records exist. The runtime never erases or retries them; factory reset is the current recovery, and rekey and recovery are not wired yet.
