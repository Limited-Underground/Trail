# OT-241 Bounded diagnostic trial preparation

2026-09-16. Software preparation and two separately authorized physical attempts
are closed. The retry measured a TX completion deadline failure; both normal-screen checks are confirmed. This document grants no further trial authority.

## Purpose and boundary

OT240's corrected physical attempt retained a third-handshake-stage refusal but
not the exact sender/receiver operation or the device's first fault/timing state.
This increment adds bounded evidence to discriminate that failure. It preserves
the accepted GPIO-edge/command/idle correction and both failed physical records;
it does not claim the physical cause resolved. See [OT240 evidence](OT-240-INTEGRATED-ENROLLMENT-2026-09-16.md).

The host freezes the exact role and argument-free subcommand before an exchange
can fail, and retains that first point through cleanup. Target instrumentation
collects coarse review/storage/radio timing in fixed RAM with saturating counters.
It neither supplies cryptographic authority nor performs an extra storage/radio
operation to obtain a diagnostic snapshot. No per-byte logging is added.

## One terminal snapshot

The version 1 record is `OTENROLL1 DIAG 1` followed by exactly ten unsigned fields:

| Field | Meaning and limit |
| --- | --- |
| fault | First visible fault category, 0..12; zero means no observed fault |
| tick_last_us / tick_max_us | Last and maximum measured review-tick duration; last cannot exceed maximum |
| nvs_reads / nvs_read_us | Count and accumulated duration of observed SDK reads before the snapshot |
| tx_queue_age_ms / tx_age_ms | Elapsed queued/current-transmission age at the snapshot; zero is not proof that no work occurred |
| service_gap_ms | Maximum observed driver-service gap, including the current gap at the snapshot |
| tx_attempts / tx_completed | Bounded transmission counts, completed<=attempts<=16 |

NVS and driver faults latch at their originating boundary before containment can
clear queues or add cleanup work. Session failures visible only at the generic
owner boundary use category 6; this is deliberately not represented as a precise
inner crypto diagnosis and may include lower-owner cancellation work before
that visible boundary. The first frozen snapshot cannot be replaced by a later
cleanup failure. On successful close it records the state before cleanup.

One bounded diagnostic write is attempted after cleanup and immediately before
the terminal CLOSED/REFUSED line. It is never an acknowledgement of restoration.
A diagnostic write failure does not retry, recurse into failure handling or
prevent independent handle closure and original restoration. Earlier startup
failures may have no usable USB output; absence remains unknown.

## Host admission and privacy

The parser requires an exact version, field count, canonical unsigned syntax and
ranges, including timing/count coherence. It accepts one snapshot only in its
terminal context and does not extend the existing exchange deadline. Wrong role,
extra keys, Booleans, signed values, duplicate lines and unsupported commands are
rejected. Host role/subcommand comes from the selected local operation, never
from target text or raw command arguments. The fixed UNKNOWN command category
cannot carry an arbitrary string.

Sanitized persistence admits only fixed failure categories, the exact role/
subcommand structure and the ten-field snapshot. It never serializes exception
messages, device identity, key material, signed invitations or ciphertext.
Diagnostic-file open/write/flush/fsync failures emit a fixed failure category and
continue child draining/reaping. Missing or malformed diagnostics cannot turn a
failed exchange into success or bypass custody checks.

## Validation and remaining gate

The [host/build proof](../../tests/benchmarks/crypto/OT-241-DIAGNOSTICS-HOST-2026-09-16.json)
binds the final checked sources, tests, artifacts and prepared controller. The
fresh affected matrix passed all 42 suites: 4018 native C++ groups, 26 scalar
controls and 130 Python tests. This includes the actual C++ diagnostic serializer/
Python terminal parser and independent two-process cryptographic status exchange.
Actual target input-loop/terminal-output tests retain the OT240 pacing/button
correction. Simulated SDK/radio/time faults remain host evidence.

Two clean ESP-IDF 6.0.2 target builds match all seven raw artifact pairs. The
application is 527664 bytes, SHA256
`80b59d532929c337d342857b23ef4c4874c877f0790acfa7883554b15a493fc2`.
The bound proof supplements the generic runner's source inventory with explicit
target and host-tool pins; final target closures and controller checks are
separate evidence, not assumed coverage from the runner. This instrumentation is
a new image; the earlier OT240 physical outcomes remain tied to their old images.

Independent private controller review passed 196 strict structured validation
cases, 15 persistence outcomes and 102 agreement cases per host validator. The
reviewed delta contains only fixed diagnostic validation/persistence and proposal
metadata; approval, grant, restoration and custody operations are unchanged.
Exact preparation passed without serial enumeration, hardware access or grant
issuance. It binds the current controller, operator/runtime and candidate images;
it is not physical acceptance.

The preparation checkpoint above issued no execution authority. The subsequent
explicitly authorized first attempt reached the comparison prompt, then refused
at **wait_local_confirmation / A / STATUS** while the user was away. The user
requested a fresh same-image retry after returning. It refused earlier, at
**handshake_3 / A / RFPOLL**, with fault 8, TX age 2060 ms and two attempted
transmissions but only one completion. B recorded fault 0 and one completion.
The user observed a code appear briefly and disappear; the retry controller did
not reach its physical-comparison prompt. This is not another missed button window.

Both attempts ended with protocol cleanup acknowledgement unverified and handles
closed, followed by independent original application/NVS verification and reset
for both roles. All ten captures per attempt were independently rehashed; custody
closed and controller/child exited 0, with one candidate and no recovery attempt
each. The user confirmed normal screens after the first attempt; both normal screens were also confirmed after the retry. See the [sanitized physical proof](../../tests/benchmarks/crypto/OT-241-PHYSICAL-2026-09-16.json)
for full snapshots and restoration records.

The same candidate passed the first handshake and encountered the measured TX
deadline on retry. This narrows the observed failure but does not prove why radio
completion was missed or the cause of earlier uninstrumented attempts. OT242 is
implementing a completion-only TX pump, supported by an actual-path host
reproduction with synthetic 172 us per NVS read and a 1200 ms host gap. Correction
validation/builds remain in progress; no further physical retry is authorized.
No BLE/Android wiring, V1 credit, website change or publication is established.
