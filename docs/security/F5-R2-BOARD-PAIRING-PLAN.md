# F5 R2: first board pairing in the product firmware (plan)

Status: plan, 2026-11-14. Part of #232. Decisions: D-076 (LoRa carrier), D-077 (Packet-v1), D-078 (test limits), D-082 (pairing starts from the phone app).

## What already exists

The whole enrollment sequence exists and is host-tested. It was also run on two real boards through the USB evaluation target `heltec_v4_enrollment_candidate_eval`, where a PC relayed every message between the boards:

1. `EnrollmentCandidateSession::start(role, group)` captures the admitted request and creates the board identity if absent (`EnrollmentIdentityStore::initialize`, real entropy). It also allocates a fresh session generation.
2. `export_candidate` and `receive_candidate` exchange public identities. A received identity is an untrusted candidate.
3. The fingerprint review shows the board's own key, then the peer's key (all 64 hex digits). A 1-3 s press on each board confirms. `HeltecEnrollmentInputArbiter` already owns the BOOT button and the OLED for this.
4. `finish_review` then:
   - possession proofs;
   - clock marks;
   - device-issued invitation;
   - handshake;
   - transcript confirmation;
   - controls;
   - `commit`.

The product target (`heltec_v4_bench`) already admits the phone's start-enrollment request (action 6) into `SelectedEnrollmentRequestOwner`, with a 5-minute window (D-110; 120 s under D-087) that is cancelled on disconnect. Nothing consumes the request yet.

## What R2a adds

- **Pairing coordinator.** A product pairing coordinator in the bench target, modelled on `candidate_runtime.cpp`. It runs in the app task and consumes the admitted request. It drives `EnrollmentCandidateSession` without a USB operator.
- **LoRa wire.** A small versioned LoRa frame for each session message (candidate, offer, possession, clock mark, invitation, handshake, control, status). Every received frame stays untrusted. The largest frame is 194 bytes, under the product radio's 255-byte MTU. The product radio (R1) carries them under D-087 (120 s window, at most 2 accepted sends per frame) and the D-079 transmit permission.
- **Product storage.** Product NVS namespaces for identity, journal, binding and boot, included in factory reset.
- **Screen.** The OLED shows a pairing status line and the review pages.

R3 (committed membership reported to the phone) and R4 (Packet-v1 DATA) follow.

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
   The final exchange is A control 1, B control 1, A control 2, B control 2; the responder commits once its control 2 is accepted by its radio, the initiator once it receives it. A board produces its first control only with 15 s left and each later control only with 10 s left, measured to the earlier of its own 120 s window, the original phone request (which can be admitted before the board starts), and its session's deadlines (its copy of the request, and the signed invitation's 180 s from the board's clock mark). While the attempt is running, a control frame goes to the radio only with time left for its answer: its first copy with at least 6.5 s left (room for that copy and one resend 3 s later; the radio may have refused offers for a while, so the first copy can go out long after the frame was made), a resend with at least 3 s (a lossless round trip takes about 1.4 s; a shorter floor let a late resend collide with the peer's held final reply). A running attempt ends as soon as transmit is no longer allowed (the saved region changed), instead of waiting out the hold: host sweeps found that no send floor makes a long hold plus a lost final reply safe. After a board has committed it still answers the peer's repeats during its short linger, without these limits. So a board that sends a control can still receive the answer it needs, and a board too late to finish fails before the other can commit: a late confirmation fails both boards instead of pairing one (host-tested timing cases; not a guarantee under every fault). In practice the second phone should start within a few minutes of the first, and each person should confirm well before the window ends. Known limits, tracked for a proper design (owner decision D-108): nothing confirms that the initiator received the responder's last control, so if every copy of it is lost the responder is paired and the initiator is not (I-059); and if the responder's save fails after that frame reached the radio, the initiator is paired and the responder is not (I-062). A frame already in the radio queue can still launch in the same poll that picks up a received frame (I-063). Listen-before-talk with the SX1262's channel activity detection (Semtech AN1200.48) is the standard way to avoid the collisions themselves; it is planned after the next board test (I-061).
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
- Everything is bounded by the 5-minute request window (D-110), which the phone connection must hold. After a board's clock mark, its signed invitation is valid for 180 s (D-110; it was 60 s), which must cover the code check, the final messages and the save.

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
