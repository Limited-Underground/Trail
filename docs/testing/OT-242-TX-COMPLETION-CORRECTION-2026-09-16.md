# OT-242 TX completion service correction

Started 2026-09-16; physical result recorded 2026-09-18. Software validation is
complete. The authorized corrected trial reached comparison, then failed during
activation. Both original applications and saved states are restored and verified.

## Observed boundary

The OT241 same-image retry retained fault 8 at handshake 3, A/RFPOLL: active TX
age 2060 ms, two attempts and one completion. This establishes a TX completion
service deadline failure. It does not uniquely identify which host pause or
hardware timing caused that deadline, or prove earlier uninstrumented failures.
Both OT241 trials restored originals and closed custody; the user confirmed both
normal Trail screens after each. See [OT241 scope](OT-241-DIAGNOSTIC-TRIAL-PREPARATION-2026-09-16.md).

## Correction boundary

A completion-only pump runs after cheap authority checks and before costly review
or command processing. It can finish an already active transmission without
starting queued TX, admitting RX or rearming radio. The two-second deadline is
preserved. Physical button logic retains its pre-pump timestamp so completion
work cannot lengthen an apparent press. Full guarded processing remains required
for protocol and authority transitions.

After TX completion, the host sends an explicit fully guarded RFPOLL before
advancing to the peer. A completion observation alone does not authorize peer
advancement or bypass expired/revoked authority. Synthetic 172 us/NVS-read cost
and a 1200 ms host gap reproduce the relevant service delay in host tests.

## Validation and remaining gate

Final validation on 2026-09-17 passed 43 suites: 4035 native groups, 26 scalar controls and 132 Python tests, including the maintained old-versus-corrected composition. Two initially absent ESP-IDF 6.0.2 candidate builds match all seven artifact pairs; app 527984 bytes SHA256 8d3032493b30eff5e5a387404d61d275dd65d886dafe149bbfd820d517034793. The affected older radio target also builds successfully. Frozen source checks and exact isolated controller preparation pass without device access or grant issuance.

The [bound host/build proof](../../tests/benchmarks/crypto/OT-242-TX-COMPLETION-HOST-2026-09-16.json)
owns exact commands, source closure, suite counts and artifact hashes. Host
simulation and builds do not establish corrected physical RF behavior. The one
authorized trial is closed; this record authorizes no additional attempt.
Successful activated fixed-status delivery, product
BLE/Android wiring, remaining lifecycle/admission evidence and V1 full testing
remain open. No V1 score or website change is earned by this software correction.

## Physical result - 2026-09-18

The [physical proof](../../tests/benchmarks/crypto/OT-242-PHYSICAL-2026-09-18.json)
records one authorized attempt. Both candidates passed write/readback and reached
the controller comparison prompt. The user confirmed matching codes and held and
released both BOOT/user buttons. The host then failed at activation step 1,
B/RFPOLL, with target refusal. No four-status exchange was reached.

The earlier TX completion deadline did not recur: A completed 3 of 3 attempted
transmissions and B completed 1 of 1, with zero active TX age in both snapshots.
A reported fault 3 (authority/clock), B fault 6 (session failure). These snapshots
identify failure boundaries; they do not prove the exact condition or ordering.
RX/loss/duplicate counts, end-to-end latency and exact bench distance remain
unavailable. This one trial does not establish RF reliability.

Both original application spans, full ordinary NVS and protected regions were
independently verified; both devices were restored and reset. All ten preserved
region hashes also pass offline verification. Controller and child exited 0,
custody closed, and no restoration-only retry was required. The user confirmed
both normal Trail screens after closure. No phone or publication actions occurred.

The subsequent host reproduction and diagnostic correction are recorded below.
Further device work requires a demonstrated correction and a separately scoped
attempt. V1 and public website status are unchanged.


## Diagnostic provenance follow-up - 2026-09-18

The [accepted host/build proof](../../tests/benchmarks/crypto/OT-242-FAULT-PROVENANCE-HOST-2026-09-18.json)
closes the diagnostic follow-up. A composed host case demonstrates that invitation
expiry inside a dispatch can surface as a generic session fault. It does not
uniquely establish what happened physically; protocol/authentication rejection
and durable-record mismatch remain possible causes of the same generic code.

Failure classification now consumes the actual rejecting invitation-window
check's result, propagated through the nested endpoint layers. An earlier
post-failure clock-resampling approach was rejected because it could mislabel an
unrelated protocol rejection. The final correction passes that independent
counterexample and retains the existing accept/reject decisions and deadlines.
It improves diagnostics; it does not make expired activation succeed.

Final validation: all 43 host suites pass with 1014 source pins verified. Both
USB and radio pair targets build, as does the enrolled target. Two clean enrolled
builds match all seven authoritative artifact pairs; the app is 528048 bytes,
SHA256 8077c1cbbbf92a3091053faf9716ade1bec8bae0a5828dff221b8eb5d0c997c3.
The byte-preserving line-ending correction passes the Git whitespace check.
No new hardware action occurred. Both boards remain on the previously verified
originals; normal screens were confirmed after the last physical trial.

Next is the separately planned OT-243 host-only measurement of idle/STATUS polling
and repeated NVS-validation cost in the activation sequence. Successful activation,
four-status delivery, the phone path and full V1 acceptance remain open.
