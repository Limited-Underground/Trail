# OT-0247c: complete host radio exchange correction

## Scope and evidence boundary

The owner approved hosted task revision 2 after accepting OT-0247a profiling.
This correction removes redundant radio rearm commands while preserving the
existing invitation window and fresh authorization checks. It is host/build
evidence; no firmware was flashed, radio transmitted, or physical performance
accepted. V1 progress and public website capability claims do not change.

Canonical repository: C:/lu/OpenTrail. Active checkout:
C:/lu/OpenTrail/.private/ot177-publication. Existing unrelated changes are retained.
The preceding [read-cost audit](OT-247-END-TO-END-RADIO-AUDIT-2026-09-20.md)
remains the baseline, including both failing sensitivity cases.

## Source-supported correction

The bridge previously issued a separate receive-rearm poll after every admitted
packet and a sender-rearm poll even when the sender was already receiving. Each
poll repeated the complete durable admission operation. Removing receiver polls
alone was insufficient; the approved revision also permits eliminating proven
redundant sender work.

- An admitted handshake, activation control or status now rearms reception inside
  the same transport operation. Its existing final durable check runs **after**
  the driver callback, so mutation during rearm cannot produce a success response.
- The dedicated driver hook checks lifetime, clock and ownership; it neither runs
  generic service nor starts TX, consumes an IRQ, or discards buffered data. Active
  or queued TX and unconsumed RX refuse it. An already armed receiver needs no SDK
  call. An RX-start failure remains a failure.
- The new evaluation-host `RFREADY` query adds an actual RX-readiness bit to the
  existing five radio counters. `RFSTAT` retains its original five-field format.
  The bridge skips the sender poll only when completion and actual readiness both
  hold. Completion during completion-only maintenance leaves readiness false and
  retains the guarded poll before advancing the peer.
- No durable authorization read is cached or removed from an admitted operation.
  No deadline is extended, failure suppressed, packet count changed, or TX moved
  into send. The over-the-air protocol is unchanged.

The bridge and candidate must be used together: an older candidate without
`RFREADY` refuses the operation. It must not be treated as compatible through a
guessed readiness value. Existing cleanup retains `RFSTAT` compatibility.

## Complete operation and expected failure behavior

| Stage | Expected successful behavior | Failure boundary |
| --- | --- | --- |
| Setup | Fresh enrollment/session state, bounded authority and explicit radio arm | Invalid state, storage or authority refuses before traffic |
| Queue send | Queue exactly the intended frame | Queueing never starts radio TX |
| Service sender | Guarded poll services TX; RFREADY reports completion and RX state | No completion/readiness guess; error counters refuse |
| Complete sender | Skip redundant poll only when ready; otherwise guarded rearm | Delayed completion retains fallback; malformed readiness refuses |
| Receive | Authenticate/admit frame, rearm within operation, then final durable check | Rearm failure, expiry, context or durable mutation returns no success |
| Confirmation | Retain both local comparison/confirmation requirements | Confirmation and invitation checks are unchanged |
| Activation/status | Same activation messages and eight fixed-status deliveries | No skipped stages or accepted malformed/authentication failures |
| Cleanup | Both sessions close, radio stops, counters and secrets checked | Cleanup is part of the passing gate, not inferred from delivery |

Independent review caught a preliminary placement after the transport operation;
the final implementation relocates the callback before its existing final fresh
check. A fault injected during the callback independently verifies refusal and
secret cleanup. This adjustment adds no synthetic read-cost savings.

## Timing results

The fixture executes production session, transport, cryptography and storage
logic with simulated radio/scheduling, SDK read costs and host command gaps.
These figures are modeled complete exchanges, not physical latency measurements.

| Profile | Retained baseline | Corrected final flow |
| --- | --- | --- |
| 172 us/read, no host gap | 51.498912 s; eight statuses and cleanup | 37.625392 s; eight statuses and cleanup |
| 209 us/read, 100 ms/command | Refused at 60.028013 s after five statuses | 50.909924 s; eight statuses and cleanup |
| 209 us/read, 200 ms/command | Refused at 60.114025 s after three statuses | 58.509924 s; eight statuses and cleanup |

The successful baseline uses 106 commands and 234296 post-initialization SDK
gets. The corrected flow uses 76 commands and 153636 gets: 30 redundant commands
and 80660 gets removed. The pre-cleanup invitation margin in the slowest corrected
profile is 2344 ms on each role; full elapsed time and per-role invitation margin
have different origins and must not be subtracted interchangeably. Initialization
is separately accounted for; cleanup is included in the complete elapsed figure.
The modest synthetic margin does not establish physical readiness by itself.

## Validation and reproducibility

The final frozen-source matrix passes all 44 suites. The freshly rebuilt
composition executable also passes default, `--cost-209-host100` and
`--cost-209-host200` with the exact figures above. Dedicated driver and session
suites pass 64 and 36 groups respectively; the 40-test Python bridge suite and
actual C++ bridge interoperability pass. Repository documentation checks and all
18 documentation regression tests pass.

Both affected targets build under ESP-IDF 6.0.2, ESP GCC 15.2.0_20251204,
CMake 4.0.3 and Ninja 1.12.1, with component downloads and compiler cache disabled:

| Target | Final artifact | SHA256 |
| --- | --- | --- |
| heltec_v4_enrolled_eval | 532640-byte app BIN | 4d82424d30d0b35d99eed82eb14609c344984682e9a07413e42183e7063bdff7 |
| heltec_v4_pair_radio_eval | 510640-byte app BIN | 84e1881d2810a639e37e45522e98d511b368b0cd542f1b0199cda1840e36eae0 |

The shared radio-driver contract affects the pair-radio target; plain USB pair
evaluation does not include that radio contract. Two fresh enrolled builds match
all seven raw artifacts: app BIN/ELF/MAP, bootloader, partition table, initial OTA
data and sdkconfig. Each enrolled closure covers 2888 source/dependency files,
1209 translation units and 112 libraries. Required final application/driver,
bounded generated RadioLib sources, configuration and protected partition guards
are verified. The retained target version label is unchanged; the exact hash,
not that label alone, identifies this candidate.

Private evidence: `.private/ot0247c-correction/manifest.json` binds the correction,
commands, output hashes and review; `.private/ot0247c-final-matrix-v2/result.json`
and its command ledger bind the full frozen source run;
`.private/ot0247c-firmware/target-build-result.json` and `pair-radio-audit.json`
bind firmware reproducibility and source/configuration closure. No artifacts
are signed or released by this evidence.

Regression coverage includes already-armed/consumed/buffered RX, queued and active
TX, startReceive failure, clock expiry before and inside the SDK call, reentry,
context mutation and durable corruption during rearm, delayed TX completion,
malformed readiness, and failure stopping the bridge before the next stage.
Existing rejection and cleanup controls remain mandatory.

The first full matrix completed its suites but rejected its aggregate because
source changed during its run. It is retained as intermediate evidence and is
not the final passing matrix. The final frozen-source run is separate. An initial
sandbox compiler-launch failure is an execution-environment failure, not a
firmware test failure; the controlled build retry is separately recorded.

## Remaining gate

Owner review of this exact correction precedes separately scoped physical
validation. OT-0247d must be checked against the final candidate, bridge,
preflight, timing capture, comparison/button steps and restoration procedure
before execution. Host success does not authorize a flash or establish phone,
field-radio, production support or V1 acceptance. Git publication is pending
separate authorization. Existing flow-review and project-control skills cover
this workflow; no additional skill is needed for this bounded correction.
