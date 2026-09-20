# OT-247: complete enrolled-radio lifecycle audit

This audit covers the two-Heltec evaluation process, including original capture
and restoration. It does not claim the two-phone V1 product path is integrated.
The trigger is the [OT246 physical capture](OT-246-FAILURE-CAPTURE-2026-09-18.md):
B accepted confirmation, missed the first activation control and eventually
rejected at its exact invitation deadline. Both originals are restored and the
user confirmed normal screens.

## Conditions that must remain true

- The intended two boards are independently identified, with antennas attached.
  USB port names alone are not identity. Each real I/O operation is guarded.
- Before either candidate write, both original application and NVS spans exist
  and match their capture descriptors. Protected bootloader, partition and OTA
  spans match the accepted baseline. Writes and restorations are sequential.
- Each target has its own clock and invitation deadline. The window is 60000ms
  from the sampled issued time, not from comparison or confirmation. Time spent
  in command transport, user input, NVS checks and radio exchange consumes it.
- Local physical confirmation, durable trust commitment, peer activation and
  traffic readiness are different states. None may stand in for another.
- A queued transmission is not a completed transmission. Completed transmission
  is not peer acceptance. Peer acceptance does not imply the receiver is rearmed.
- Unexpected responses, changed identity, stale authority, invalid records,
  expired deadlines and failed radio operations stop advancement. Failure must
  still close passive handles and complete verified original restoration.

## Every phase and its expected result

| Step | Operation | Expected state and proof before advancing |
| --- | --- | --- |
| 1 | Admit exact proposal, sources, runtime and candidate | Current one-attempt authorization matches hashes. No active custody or reused trial. Original recovery spans and allowed writes are fixed. |
| 2 | Freshly resolve A, then B | Each intended private identity has one matching USB route and the expected board/flash layout. Recheck before each mutation. |
| 3 | Claim and capture A, then B | Save application733184B at0x10000 and ordinary NVS12288B at0xd000; compare protected spans. Reject unexpected retained evaluation namespace. Neither candidate may be written until both originals are recoverable. |
| 4 | Candidate A write/readback/boot; then B | Exact padded application image matches independent readback. Protected ranges are not written. Each boot is separately admitted; do not infer the peer booted. |
| 5 | Open passive handles; SYNC A/B | Fresh identity checks; exactly one startup line synchronization. No ROM operations while the protocol owns passive handles. |
| 6 | HELLO/INIT A; HELLO/INIT B | Each target initializes its role, isolated evaluation storage and identity under the fixed context. Malformed or changed identity stops the session. |
| 7 | TIME A/B; signed BEGIN A/B | Reconfirm identity/context and monotonic samples; construct and validate the signed invitation. Each target's issued time/deadline remains authoritative throughout the exchange. |
| 8 | RADIO A/B | Both receivers start armed, using915MHz, BW125kHz, SF7, CR4:5 and configured2dBm. At most16 transmissions per role,158B frames, no radio beyond the admitted deadline. |
| 9 | Handshake1, A to B | A's RFSEND is followed by guarded RFPOLL/RFSTAT until TX completed, then sender rearm. B must return the expected HANDSHAKE and complete a guarded WAIT rearm. |
| 10 | Handshake2, B to A | Same completion, acceptance and two-radio rearm requirements in the opposite direction. |
| 11 | Handshake3, A to B | Same requirements. B must be listening again before any activation transmission. This is the transition omitted before OT247. |
| 12 | REVIEW A/B | Full transcript values match, both local windows have positive time left, and the displayed comparison belongs to this fresh session. Codes are neither logged nor typed into the host. |
| 13 | Physical confirmation; STATUS A/B | Start from a released button; each user press/release satisfies the500–3000ms hold rule. Target checks physical edges and current durable authority. Both STATUS values must reach state4. Host acknowledgement cannot confirm a device. |
| 14 | Activation1, A confirmation control to B | A completes TX. B authenticates the control and commits trust, then rearms through a guarded poll. A is also rearmed. |
| 15 | Activation2, B confirmation control to A | A authenticates and commits trust; both radios finish ready for the next transfer. |
| 16 | Activation3, A durable-activation control to B | B records peer activation; local confirmation alone is insufficient. Complete and rearm both sides. |
| 17 | Activation4, B durable-activation control to A | A records peer activation; complete and rearm both sides. |
| 18 | TRAFFIC A/B | Both return1: local confirmation, durable commitment, activation sent and peer activation are all satisfied. |
| 19 | Status1 A to B, then B to A | Each RFSTATUS1 must complete TX and produce exact RECEIVED1 at the peer. Rearm sender and receiver after each transfer. |
| 20 | Status2 A to B, then B to A | Same acceptance for code2, with fresh authority and authenticated counter handling. |
| 21 | Status3 A to B, then B to A | Same acceptance for code3. |
| 22 | Status4 A to B, then B to A | Same acceptance for code4. All eight deliveries are required; one successful direction is insufficient. |
| 23 | CLOSE A/B; DIAG/TRACE/terminal; RFSTAT | Require CLOSED1, complete valid diagnostics/trace, radios stopped, zero RX errors, and exact totals: A8/8TX and7RX, B7/7TX and8RX. Missing terminal data cannot count as clean protocol closure. |
| 24 | Close both passive handles | Independent handle closure must be confirmed before any ROM restoration. Uncertainty retains custody; do not launch another controller. |
| 25 | Restore/readback/reset A; then B | Compare protected spans, restore original application and NVS, independently read back every final span, then reset the original application. Failures retain custody for bounded restore-only recovery. |
| 26 | Close controller; visual check | Require closed journal, no active lock, verified originals and controller exit. Separately ask whether both normal Trail screens are visible. |

The status codes correspond to OK, need assistance, anyone online and available
to help in the semantic adapter. This evaluation does not yet prove protected
phone action routing or phone receive presentation. See the
[adapter](../../firmware/components/security_evaluation/include/opentrail/companion_status_bridge.hpp).

## What the implementation audit found

### Receiver rearming was missing after packet consumption

The [target driver](../../firmware/targets/heltec_v4_enrolled_eval/main/enrolled_radio_driver.cpp)
finishes RX with the radio in standby. Consuming its buffered frame clears that
slot; a later full service call rearms reception. The
[transport](../../firmware/components/security_evaluation/include/opentrail/enrolled_peer_transport.hpp)
services before consuming, not afterward. Confirmation ticks service pending TX
only. The bridge previously returned from HANDSHAKE reception immediately, then
sent the first activation control while B had not been rearmed.

The correction requires an ordinary, fully guarded RFPOLL with exact WAIT after
every accepted received frame. The new host radio seam models standby and drops
transmissions to an unarmed receiver. The old bridge fails at activation1; the
corrected ordering completes three handshake, four activation and eight status
transfers. Unsolicited/malformed rearm responses or identity refusal prevent
the next transmission. This is host evidence; physical delivery remains open.

### Per-byte identity checks consumed the exchange window

OT246 measured5197 guard callbacks costing48.3695648s over72 commands. Each
guard enumerates USB devices. Activation1 alone spent19.1690729s in guards and
13.4778567s in serial reads. Target NVS cost overlaps host waiting and cannot
be added again. The terminal-response collection also occurs after rejection.

The receive-order correction alone is insufficient preparation for a new trial.
Its full successful command sequence contains60 RFPOLL operations. After local
confirmation it still needs48 RFPOLL,12 RFSTAT,4 RFCONTROL,8 RFSTATUS and2 TRAFFIC
operations. Substituting observed successful command timings gives55.276s to
73.929s for known post-confirmation work, excluding unmeasured RFSTATUS/TRAFFIC.
These are modeled substitutions, not physical bounds or promises. The physical
attempt had approximately33s left after confirmation.

Bounded response reads are implemented and host-validated, retaining fresh
guards before/after actual I/O and all parser/deadline checks. No invitation extension
or authority cache is permitted as a timing workaround.

### Expired protocol cleanup and original restoration are separate

The target main loop ticks before dispatching a command. Expiry can therefore
stop the session before CLOSE reaches its otherwise limit-independent branch.
Complete post-expiry protocol cleanup cannot be assumed. The host must close
passive handles and independently restore originals even when CLOSE fails.
OT246's B trace was captured; A's complete terminal trace was not. Neither fact
changes the independently verified restoration result.

## Validation boundary before another trial

1. Keep the original RX lifecycle failure as a negative regression.
2. Validate rearm responses and guard refusal, fragmented/coalesced responses,
   excess lengths, disconnects, deadlines and complete/partial diagnostic capture.
3. Exercise the entire corrected command sequence and account for every transfer,
   sender completion, receiver acceptance, rearm and final counter.
4. Check the time budget with explicit measured inputs and unknown target costs;
   do not present a host model as physical timing acceptance.
5. Run the affected host/operator/custody suite and real C++ interoperability;
   bind final source hashes to new evidence. Old trial bindings remain immutable.
6. Only then prepare a separately authorized physical confirmation attempt. A
   pass requires all eight statuses, final diagnostics/counters and restoration.

Primary sequence authority is the [bridge](../../tools/enrolled_pair_bridge.py),
with [session admission](../../firmware/components/security_evaluation/include/opentrail/enrolled_bench_session.hpp),
[activation state](../../firmware/components/security_evaluation/include/opentrail/independent_peer_traffic_endpoint.hpp)
and [operator acceptance](../../tools/enrolled_trial_operator.py). The audit is a
review and test map, not a substitute copy of those implementations.


## Final host result and remaining gate

The [host receipt](../../tests/benchmarks/crypto/OT-247-HOST-FLOW-REVIEW-2026-09-20.json)
binds the four changed files. All97 Python tests,8 real C++ formatter cases,
two-process session interop and13 composed groups pass. The actual composed
session/NVS path now follows all15 destination rearms and exact final counters.
With172us per synthetic SDK read and no host cost, all8 statuses and cleanup
complete in51.499s, leaving8.546s before cleanup. This is not physical acceptance.

Using209us per SDK read (rounded physical aggregate mean) and100ms modeled host
cost per command, the full sequence expires at60.028s after5 statuses. With200ms,
it refuses at60.114s after3 statuses. Neither case measures post-fix hardware
performance; both are retained as failing readiness cases rather than hidden
behind the passing default profile. The model has106 commands and234296 SDK
gets in its successful control. Startup, independent idle scheduling, actual
airtime and target crypto execution are not completely represented.

The next bounded task is to profile per-command durable reads and evaluate
receiver rearm inside the existing admitted receive transaction. That could
remove15 extra host command boundaries, but necessary fresh checks must remain.
Require rearm failure, expiry and context-loss propagation; a generic service
call must not consume another packet or start unrelated queued TX. Exact SDK
savings have not been measured. Starting TX inside send is a separate, larger
mutation-order change and is not included in this correction.

No new physical trial is prepared or authorized by this document. Both boards
remain on their verified originals. Firmware inputs are unchanged; existing
build evidence is reused. Full physical status delivery remains unaccepted.
