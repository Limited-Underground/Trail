# OT-246: capture the rejecting check and actual timing

2026-09-18. Diagnostic implementation; validation and exact trial preparation
are recorded in the linked host receipt. The authorized physical trial on
2026-09-20 is closed with both originals restored; findings follow below.

## Troubleshooting case

OT244 reached both physical confirmations, then refused activation1 at B/RFPOLL.
Its fault3 category and cumulative counters cannot identify the rejecting check
or account for elapsed time. OT245 showed that adding concurrent idle work alone
does not explain that physical outcome. Another unchanged attempt is unjustified.

This successor preserves the protocol, deadlines, confirmation rules and durable
read checks. It records the actual rejection before cleanup, then emits a fixed
DIAG1 record, TRACE1 record and terminal result. No UART output or storage writes
are introduced in diagnostic callbacks.

| Observed result | What the new capture establishes |
| --- | --- |
| Invitation expired | Rejecting layer and original sampled time versus issued time/deadline; target review/button/confirmation times show the remaining budget at each recorded boundary. |
| Clock regressed or context changed | Explicit reason and original current/previous times; no later resampling to infer cause. Context values themselves are excluded. |
| Durable authority/current-record rejection | Named membership/evidence/boot/role boundary, or storage read failure; it is not relabeled as expiry. |
| Packet or receive rejection | Packet-format versus handshake/control/status rejection category. It does not disclose packet content or claim a cryptographic subroutine failure without evidence. |
| Host overhead or serial waiting | Per-command duration plus guard/read/write time and call counts. Guard time includes repeated device enumeration; serial-read time can include waiting for target work. |
| Partial terminal capture | A complete parsed TRACE is retained as explicitly unconfirmed evidence. It cannot satisfy success or cleanup acceptance. |

Host comparison notification includes each REVIEW response's remaining budget
and its age when the prompt was emitted. First STATUS4 observations are recorded
separately from target physical-confirmation timestamps. Host and target clocks
are not assumed synchronized; compare target intervals locally and use host
request/response bounds. Capture receipt time is not target execution time.

## Scope and limits

Target trace storage is fixed-size and first-failure data is immutable. Twenty-one
public integer fields encode category, sample validity, time bounds, phase times,
button counts and snapshot time. No keys, comparison codes, identities, payloads
or unrestricted exception strings enter this evidence. Observer callbacks reuse
existing samples and do not add authority or NVS reads.

The actual activation path's three nested authority checks are instrumented.
The earlier confirmation-owner guard is not separately instrumented; failures
there can retain a protocol/unknown category. Overlong button hold likewise
has no dedicated new reason code. The recorded button times and confirmation
acceptance marker remain available. `activation_ready` means the first successful
TRAFFIC-ready observation, not the precise internal transition instant. These
limits prevent a promise that every possible failure will have a unique cause.

The fresh controller buffers bounded host timing events and writes them after
the child exits, avoiding a per-command fsync. Process termination before that
flush can lose host timing; the controller must remain alive through restoration.
Routine confirmation STATUS entries are capped at 128 of the 256 command slots,
reserving room for activation. The failed command and its transport cost are
retained even at the global cap; total host events are limited to 262.
Target capture is bounded by existing USB deadlines. No timeout is extended.

## Validation and target preflight

Focused composed tests exercise exact deep expiry, original-sample preservation,
clock/context alternatives, durable membership tampering, malformed packets,
the no-resample counterexample and successful milestone ordering. Actual C++
formatter bytes are consumed by the Python parser/operator with fragmented and
empty reads; missing/malformed/duplicate/overflow/extra-field records are refused.
Host tests separate guard, read and write cost without adding operational calls.

Affected firmware targets are enrolled evaluation and USB/radio pair evaluation.
The Heltec V4.2 ESP32-S3, 16MiB QIO80MHz, no-PSRAM configuration, existing GPIO,
USB lifecycle, radio profile and storage layout remain unchanged. The enrolled
candidate is built twice in fresh directories and compared across the established
seven artifacts. Target helpers retain their historical OT242/OT230 build-name
prefixes; exact source/image hashes identify this diagnostic successor.

Physical stack usage, device startup and radio behavior are untested by this task.
No current device enumeration, flash, reset, phone or production admission action
is authorized or performed. OT244's original-restoration acceptance is retained.

See [validation and preparation receipt](../../tests/benchmarks/crypto/OT-246-FAILURE-CAPTURE-HOST-2026-09-18.json)
for final results, exact candidate and bindings. Private reproducible commands,
source pins, controller and preparation files are under
`.private/ot246-causal-capture`.

Validation passed: 44 host suites and all three affected firmware targets. Both
enrolled builds match across all seven artifacts. A final Python-only capture-budget
correction changed three of 1,016 matrix pins; the other 1,013 remained identical.
All four affected Python suites passed again (86 tests plus real C++ interop),
using the unchanged compiled binaries. No second full matrix was run. Exact
controller preparation passed with no grant or device enumeration.

The candidate is 532,272 bytes, SHA-256
`1c360458679e613d0a8c2890c0623a109cf8dc853685168d7ca7bac1bb594a8b`.
The proposal hash is
`8387f9e58f9fcb4d9197d6c20c442fc1528ffd26b3c33ed68e2926b449bed0a1`.

## One subsequent physical attempt

After all host/build gates pass, obtain current approval/readiness for the exact
candidate/controller/request and the two intended Heltecs. Capture fresh original
application733184B at0x10000 and ordinary NVS12288B at0xd000 on both before writing
either candidate; compare protected bootloader/partition/OTA without writing them.
Freshly resolve identity/port before every mutation and operate sequentially.

Use the unchanged915MHz/BW125kHz/SF7/CR4:5/configured2dBm profile, max158B frames,
max16TX per role and60s invitation. Run one fresh evaluation handshake, compare
codes, hold/release BOOT on each, then attempt activation and four statuses each
direction. Preserve the controller and its in-memory key through automatic
original/state restoration, independent readback and reset. At most two explicit
restore-only recoveries are scoped if custody remains; no automatic second trial.
Normal-screen confirmation is a separate final check. Phones are not involved.

The result must be interpreted from the saved first rejection and time budget.
If the discriminator is missing, report the capture failure instead of another
theory or unchanged retry. This preparation earns no physical acceptance, V1
completion or public website status change. Changes remain local/uncommitted;
publication is pending separate authorization.

## Physical result - 2026-09-20

The [physical receipt](../../tests/benchmarks/crypto/OT-246-PHYSICAL-2026-09-20.json)
retains the allowlisted target trace and all 77 host timeline events. Both local
confirmations were accepted. Activation1 then refused at B/RFPOLL.

The actual first rejection is now established: `IndependentHandshakeEndpoint`,
layer3, reason7 `window_expired`; original sampled time69671ms exceeds deadline
69667ms by4ms. B accepted confirmation at37683ms, with31984ms remaining; its
button hold was823ms. The trace contains no activation-ready milestone.

The timeline is more specific than a slow activation: A completed the initial
RFCONTROL sequence, then B returned28 successful RFPOLL WAIT responses before
expiry. Activation1 command durations total34.293910s, including19.1690729s in
identity-guard callbacks and13.4778567s in serial reads. All72 commands invoke
5197 guards costing48.3695648s. The guard enumerates USB devices around each
single-byte read. Read duration includes waiting for target work; these costs
must not be added to concurrent target NVS time. Refusal command duration also
includes collecting diagnostics after the rejecting sample.

Source review found a distinct ordering defect: the radio driver finishes a
received frame with RX off; consuming it does not rearm. The transport services
before consuming, and the bridge returns immediately after the final handshake
receive. Confirmation ticks service only pending TX. The host then transmits
activation1 before B's next full poll rearms RX. This is a source-supported
explanation for the missing packet, not direct over-air proof. The existing host
peer double always delivers frames and omits receiver state. The next correction
must reproduce this receive lifecycle and preserve fully guarded rearming;
reducing host overhead alone is not an adequate fix.

Both original application spans (733184bytes) and saved NVS spans (12288bytes)
were restored, all protected/final spans verified, and both originals reset.
Controller/child exited0, custody closed, no recovery retry. Independent audit
passed10 capture hashes/sizes,6 protected descriptors,2 application prefixes and
2 fresh-namespace checks. The user confirmed both normal screens afterward.

Only B's terminal trace was obtained; protocol cleanup remains unverified because
A's response exceeded its capture budget. Original restoration is separately
verified. Final RX/loss/duplicate counters and over-air latency are unavailable;
four-status delivery and phone integration remain unaccepted. No V1 credit or
public website status change. The consumed proposal cannot authorize a retry.
