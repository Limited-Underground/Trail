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

The product target (`heltec_v4_bench`) already admits the phone's start-enrollment request (action 6) into `SelectedEnrollmentRequestOwner`, with a 120 s window that is cancelled on disconnect. Nothing consumes the request yet.

## What R2a adds

- **Pairing coordinator.** A product pairing coordinator in the bench target, modelled on `candidate_runtime.cpp`. It runs in the app task and consumes the admitted request. It drives `EnrollmentCandidateSession` without a USB operator.
- **LoRa wire.** A small versioned LoRa frame for each session message (candidate, offer, possession, clock mark, invitation, handshake, control, status). Every received frame stays untrusted, and the frames are bounded to the existing 154-byte radio limit. The product radio (R1) carries them under the D-078 test limits and the D-079 transmit permission.
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
   - a waiting board repeats its latest batch;
   - a repeated frame from the peer identifies exactly which of our frames it still needs.
   It stops resending after `max_sends` (2, D-078) but keeps listening until the 120 s window ends, because the other person may still be reviewing.
4. Session adapter: converts session values to and from the exact frame payloads, and drives the local review and transcript confirmation through the existing review API (tests in the security suite, with real crypto).
5. Target composition in `heltec_v4_bench`:
   - consume the admitted request and start the session with the request's role and code;
   - feed the product radio both ways;
   - OLED status;
   - product NVS namespaces and factory-reset coverage.
6. Two-board session (owner present): identities exchanged over LoRa, fingerprints compared, a press on each board.

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
- Everything is bounded by the 120 s request window, which the phone connection must hold.
