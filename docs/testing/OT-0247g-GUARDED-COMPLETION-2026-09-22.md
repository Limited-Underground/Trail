# OT-0247g guarded completion and receive rearm

OBSERVED 2026-09-22. Software implementation and focused paired replay complete.
The final affected host matrix, both firmware builds, enrolled reproducibility
and independent lifecycle review pass. Owner acceptance is **pending**. This report does not claim physical acceptance.

## Scope and source boundary

Canonical project: C:/lu/OpenTrail. Active worktree:
C:/lu/OpenTrail/.private/ot177-publication. Approved OT-0247g revision 1 permits
software correction and builds only. No device enumeration, hardware mutation,
RF trial, signing, Git mutation, publication or deployment occurred in this work.

The additive `OTENROLL1 RFFINISH` command combines sender completion observation
and guarded receive rearm. Older firmware rejects the unknown command; the bridge
does not fall back to assumed readiness. Existing `RFSTAT` and `RFREADY` diagnostic
semantics remain unchanged. OTA packet formats and session deadlines are unchanged.

## Lifecycle and ownership

| Before state / owner | Trigger | Required effect and evidence | Failure boundary |
|---|---|---|---|
| Queued TX / driver | Initial sender RFPOLL | Existing transport operation starts queued TX; sender requires WAIT | Unexpected RX refuses before peer advance |
| Started TX / driver | RFFINISH session entry | Existing completion-only maintenance observes TX completion | No queued TX start, RX consumption or RX rearm from service_pending_transmit |
| Pending TX / driver | Guarded maintenance | Report pending counters with readiness false; bridge waits and repeats RFFINISH | Driver deadline/error remains terminal |
| Completed TX / transport | finish_transmit operation | Fresh endpoint/MTU admission, session checks around rearm, post-maintenance and final durable freshness | Context, durable state, deadline and reentry failures refuse output and clean up |
| Idle sender / driver | rearm_after_transmit | Reject queued TX, buffered RX and an RX IRQ; otherwise arm receive or verify idle receiving state | Never consumes unexpected frames or starts queued TX |
| Completed and receive-ready / bridge | Six-field RFFINISH reply | Require valid counters, zero errors, not stopped, completed equals attempts and explicit readiness 1 | Missing/malformed readiness refuses before peer advance |
| Destination / transport | Existing RFPOLL admission | Authenticate/admit frame, receive-only rearm, final durable checks, then publish | No premature success or deadline extension |
| Any failed operation / session and host | Existing cleanup | Refuse staged response, contain radio and clear session where further I/O is allowed; close host handles | Protocol verification and handle closure remain distinct |

The new transport method uses `EnrolledPeerTransport::operation`; session observe
alone is insufficient. The callback retains post-maintenance and final durable
checks. The old post-receive check has no corresponding receive operation in this
path: RFFINISH never consumes RX. Initial queued-TX servicing remains explicit.
Radio readiness is a physical state snapshot, never cached authorization.

## OT-0247f erratum and shared replay correction

The [OT-0247f report](OT-0247f-ASYNC-SERIAL-MODEL-2026-09-22.md) said all nominal
cases delivered eight statuses. Its retained asynchronous `baseline` and
`delayed_only` records actually failed confirmation with zero statuses. These
were nominal scenarios, not stress tests. An initial OT-0247g comparison also
misclassified those failures as stress. Those assertions are superseded here;
historical files and original failures are preserved unchanged.

The adapter's immediate-button path advanced the asynchronous host clock by
600 ms even though target press processing had already advanced the target clock.
This shortened the hold admitted by the session. Session tick records press after
durable offer readback but records release before that work. The corrected adapter
waits 600 ms after target press processing, matching the original synchronous
semantics. It records this interval separately from the raw button-high interval:
nominal immediate cases have 600,000 us after press processing and 710,770 us raw high.
This is not a claim of a physical 600 ms button hold or a firmware deadline change.

Both production versions were rerun with this same corrected adapter and identical
costs/schedules. The private reconstructed old bridge matches its frozen baseline
SHA-256 `a1c7a33f8b83b96bd8c2059297eb25b9d47ce6dcbef4cb0063b8f2e5ed4fd8bc`.
The before run uses the retained OT-0247f C++ executable; the after run uses the
freshly compiled frozen correction executable. Mixed-EOL preservation for the two
affected test files was verified against their exact pre-change baseline hashes.

## Measured paired results

All nine nominal scenarios deliver eight statuses and verify cleanup in both
versions: four synchronous schedules, four immediate asynchronous schedules, and
the 87 us byte-spacing sensitivity. All use 209 us per SDK get and 14,623 us per host
guard. No saved failed scenario was discarded or renamed to obtain this result.

| Schedule | Before s | After s | Saving s |
|---|---:|---:|---:|
| Synchronous baseline / delayed-only | 48.889109 | 46.022154 | 2.866955 |
| Synchronous polling-only / combined | 57.612444 | 54.745489 | 2.866955 |
| Asynchronous baseline / delayed-only | 48.709694 | 45.847501 | 2.862193 |
| Asynchronous polling-only / combined | 57.518483 | 54.656290 | 2.862193 |
| Asynchronous 87 us byte spacing, combined | 57.518483 | 54.656290 | 2.862193 |

Combined synchronous commands fall 153 to 138, guards 781 to 706 and SDK reads 214,494
to 206,024. Combined asynchronous commands fall 155 to 140, guards 1,545 to 1,444,
host reads 538 to 510 and SDK reads 215,024 to 206,554. Counts exclude startup SYNC
from target-command rows. SDK modeled work falls 1.770230 s in both combined cases;
serial/guard time overlaps SDK work and must not be added as independent savings.
The correction does not claim the whole prior 9.942914 s rearm occupancy as savings.

Combined asynchronous final invitation margins improve from A 3.132517/B 3.234517 s
to A 5.994710/B 6.096710 s. Synchronous margins improve from A 3.038556/B 3.140556 s to
A 5.905511/B 6.007511 s. Full role margins and command histograms are retained.

The single read-bound stress case (100 ms initial delay and 100 ms per 64-byte chunk)
continues to fail confirmation with zero statuses and verified cleanup. Before:
62.409843 s; after: 62.478196 s, 68.353 ms slower. It is not the physical seven-delivery
failure, and it is not evidence that every transport schedule improves.

## Validation and outstanding gates

Focused checks passed: 71 driver groups, 40 session groups, 40 bridge tests and
actual two-process protocol interoperability. Driver cases cover pending TX,
rearm failure, expiry before/during rearm, reentry, queued TX, RX IRQ and buffered
RX refusal. Session RFFINISH cases inject driver failure, context loss, durable
corruption and expiry during maintenance; they require zero response and cleanup.
The initial sender unexpected-frame control refuses before peer advancement.

Seven replay negatives pass: identity loss, late authority, malformed readiness,
rearm failure, context loss during response, host read expiry and authority expiry
during response. The historical `malformed_RFREADY` scenario name is retained for
input comparison; the corrected bridge's malformed response is now RFFINISH.
Protocol cleanup is verified only when its response sequence proves it. Identity
loss forbids further target I/O; host handle closure does not prove target cleanup.

Nine deterministic controls pass: the eight retained transport/process controls
plus a target-ahead-of-host button regression. Constructor/RPC failures close and
reap the child; ordered partial/empty responses and exactly-once execution remain.

- **Verified final affected host matrix:** all 44 suites pass in
  `.private/ot0247g-final-matrix-v2/result.json`, including the final source
  consistency gate. The first run passed all 44 suites but its consistency gate
  was invalidated by the authorized replay-adapter correction during execution;
  that run is retained and is not the final gate. The second run passes against
  the frozen corrected adapter.
- **Verified firmware builds:** `heltec_v4_enrolled_eval` and
  `heltec_v4_pair_radio_eval` both build and pass source audits. These are exactly
  the two affected targets; the USB pair target excludes the radio interface
  through its compile conditional. Two enrolled builds match all seven compared
  raw artifact pairs. The enrolled BIN is 533,360 bytes, SHA-256
  `0779d6d3622966efc8b93ff5ac39ad2eeb9988ffe544d68e17291b0830f16bd8`.
  Evidence: `.private/ot0247g-firmware/target-build-result.json`.
- **Independent final lifecycle review passed:** production lifecycle and corrected
  adapter reviewed; `.private/ot0247g-correction/independent-review.txt` retains
  the review. Owner acceptance remains pending.

Private evidence under `.private/ot0247g-correction/`: `corrected-comparison.json`,
`corrected-pins.json`, `corrected-run-commands.json`, `corrected-before/`,
`corrected-after/`, and `final-replay/build.json` plus `artifact-pins.json`.
The corrected adapter pin is
`51eeff8b4483b7695edaab5836d0c3ae3516ea5cbc392076a48022ec89c88439`.
The after executable SHA-256 is
`73bdb5b773f8e5f5a39a2f652b62c747975cca4f2f788fba0e1c71d330178e59`.

Existing board/PHY/partition/toolchain boundaries remain unchanged. Hardware
identity/ports, physical radio/USB, restoration and other physical gates are not
applicable to this software-only execution and remain mandatory before a separately
authorized trial. Host timing models serialize target execution and omit real
USB/flash/crypto concurrency; they do not prove physical improvement or resolve
the seven-delivery expiry. No V1 credit or public website status change follows.
Implementation is local and uncommitted; publication remains separately gated.
