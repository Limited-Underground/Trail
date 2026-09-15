# OT-230 Two-device USB bench preparation

## Current evidence

OT-230 is in progress. An additive target, USB provisioning/handshake bridge and
two-role custody coordinator are implemented for a controlled two-device
evaluation. Initial focused software checks and two fresh firmware builds pass;
all seven authoritative artifact pairs are byte-identical. The final source/dependency admission and fresh affected matrix pass: 827 C++
groups, 26 scalar controls, 40 Python tests and actual two-process interoperability.
One physical attempt has now failed before the comparison prompt. Both OLEDs
showed `ROLE ? / REFUSED`; no human confirmation was requested or reached.
Both original applications/NVS and protected spans have now been readback-verified,
and both original resets are verified. Custody is closed. The cause remains
unknown, and physical handshake/confirmation acceptance is not established.

| Evidence layer | Observed result | Remaining gate |
| --- | --- | --- |
| Actual C++ session | 48 deterministic real-crypto groups pass | Physical target behavior |
| Python USB bridge | 9 tests pass; interoperability with two independent C++ node subprocesses also passes | Actual USB devices and human input |
| Two-role custody coordinator | 17 focused tests pass; actual trial restoration and resets complete on both roles | Final independent custody/capture audit passed |
| Firmware builds `ot230-pair-c` and `ot230-pair-d` | Both fresh builds pass; all seven artifact pairs are byte-identical | Physical boot and behavior |
| Physical trial and restoration | Both devices refused before comparison; both originals restored/readback-verified and reset; custody closed | Diagnose actual target startup before another trial; no handshake/confirmation acceptance |

The C++ development run reuses previously validated scalar crypto objects.
The final matrix rebuilds its crypto objects from the verified dependency. The
[complete host/build evidence](../../tests/benchmarks/crypto/OT-230-PAIR-BENCH-HOST-2026-09-14.json)
records the final source pins and results. Fourteen additional operator tests
and actual isolated source/dependency/runtime import verification pass. Earlier
build attempts exposed sandbox toolchain access and a GPIO enum compile error;
both are resolved in the accepted builds. A build grants no hardware authority.

## 2026-09-14 physical attempt: refusal before comparison

One exact-image trial consumed one grant. Both actual OLEDs displayed
`ROLE ? / REFUSED` before a comparison code appeared, and the host bridge failed.
The operator did not request human confirmation, and neither local confirmation
was reached. No retry was performed.

`ROLE ?` is not evidence that failure occurred before `INIT`. The target assigns
its display role only when it renders a review, so the same label can precede
review after startup, provisioning, handshake or another refusal. The current
target collapses startup and runtime failures into the same fixed refusal
presentation. No retained stage/error record distinguishes those branches;
the actual cause is unknown.

The controller finished with exit code 0, `restored=true`, observation
`trial_failed`, `custody_held=false` and closed custody. Both original application
and full NVS images, plus all protected spans, passed readback verification;
both original resets were verified. The record contains one candidate attempt
and zero recovery attempts. Exit code 0 records completed cleanup, not a
successful handshake. Original phone Ready or a running original UI was not
independently observed in this trial.

The passing host tests did not cover the actual target startup integration.
The C++ node processes exercised the real session core with fake platform
services, not the deployed `app_main.cpp` startup sequence. Those software and
build results remain valid within their stated boundaries; they did not predict
this physical refusal.

After restoration, the next coherent correction is a fixed first-failure
stage/error diagnostic, visible locally and available through a bounded `DIAG`
query after a startup fatal error, plus deterministic SDK-fault tests that run
the actual `app_main.cpp`. The diagnostic must preserve existing refusal,
one-use, deadline and cleanup rules and expose no raw protocol contents. It is
proposed work, not implemented by this report. Validate the actual startup and
fatal-query paths before preparing another exact-image trial; the consumed
grant cannot authorize another attempt.

## Reproducible candidate

Both initially absent build directories use ESP-IDF v6.0.2, Xtensa
15.2.0_20251204, CMake 4.0.3 and Ninja 1.12.1 through
[Build-PairEvaluation.ps1](../../tools/Build-PairEvaluation.ps1), with component
manager and compiler cache disabled. The retained build comparison checks raw
bytes as well as length and SHA-256. Application BIN/ELF/map, bootloader,
partition table, initial OTA data and generated SDK configuration all match.

| Artifact | Bytes | SHA-256 |
| --- | --- | --- |
| Application BIN | 459,296 (`0x70220`) | `a8c33bb9988ad0e57631703c8c5d76450f72dd00a6088aaaca941309ff2a678c` |
| Application ELF | 6,288,728 | `202f6e5e9b6b7a044ce96f2aefa63790ec62aae94c965800e4cff722031b82d9` |
| Application map | 6,727,795 | `45ba5cc0143dfeb826759fc8a328df78f65ab010659b2c8868b072188343f2cf` |

The configured main-task stack is 24,576 bytes. This target does not run a
NimBLE host or its callbacks. Physical minimum-free stack remains unmeasured;
configuration, compilation and individual frame sizes do not prove complete
call-chain headroom.

## Implemented boundary

Each device runs its own `IndependentHandshakeEndpoint`, identity, durable boot
authority, role-consumption record and TX/RX stores. The
[session control layer](../../firmware/components/security_evaluation/include/opentrail/pair_bench_session.hpp)
accepts bounded `OTPAIR1` commands through the
[new target](../../firmware/targets/heltec_v4_pair_eval/README.md). It contains no
synthetic counterpart. Existing accepted targets and endpoint versions are
preserved.

This trial uses USB only. The
[host bridge](../../tools/pair_bench_bridge.py) relays the three exact Noise
messages between two separately admitted serial handles. It compares the full
authenticated transcript and reports each local result separately. There is no
LoRa transport, phone application, BLE host connection, product membership or
application traffic interface in this target. The Bluetooth controller is used
only by the reused entropy lifecycle; it is not a pairing or messaging service.

Provisioning trusts the explicitly authorized physical USB operator and exact
device custody. A temporary host signing key signs both generated identities,
boot contexts and independently sampled local-clock windows in the v2
invitation. The private signing key is not exported or persisted. USB `INIT`
supplies the evaluation signer pin, and `INV` supplies the intended peer pin;
this is not a production authenticated USB enrollment protocol or a selected
product trust policy. Different boot generations and local clocks remain valid.

The LF-delimited ASCII protocol accepts at most 700 bytes before LF and emits
at most 768 bytes. Hexadecimal tokens are uppercase; numbers and token counts
are canonical. Commands are `HELLO`, `INIT`, `TIME`, `INV`, `SEND`, `FRAME`,
`REVIEW`, `STATUS` and `CLOSE`, each prefixed by `OTPAIR1`. No host `CONFIRM`
command exists. Invalid input, stale authority, clock regression, expiry or
callback reentry closes the one attempt and suppresses staged output.

Startup remains idle while the second candidate is prepared. The first valid
`HELLO` or `INIT` starts a nonrenewable 120-second overall session window;
repeated `HELLO` cannot extend it. The signed invitation limits each device to
its own 60-second window, beginning at its sampled local time, so the human
confirmation interval is the remaining portion of that window. Expiry still
applies after local confirmation until explicit `CLOSE`. `STATUS` is an
observation, not traffic or membership authority.

## Intended local action and physical acceptance sequence

The firmware-porting preflight is recorded in the target README and governed by
[the porting lessons](../firmware-porting-lessons.md). The exact candidate,
source/runtime binding, current device identities, storage spans and recovery
inputs must be verified before any mutation. No transient device identifiers,
private keys or complete wire captures belong in this public report.

| Porting requirement | Applicable preflight or explicit limit |
| --- | --- |
| Exact target boundary | Candidate Heltec WiFi LoRa32 V4.2 / ESP32-S3, 16 MB QIO/80 MHz, no PSRAM assumption; target README records pins, partition layout, GPIO0 and OLED. Actual received-unit identity remains a physical admission gate. |
| Source and build reproducibility | Two fresh builds match all seven raw artifact pairs. The build compile database includes the target sources, the final matrix pins its source closure, and isolated operator admission verifies 11 source files plus 145 dependency files and the retained runtime. Prior physical acceptance is not transferred to this image. |
| Boot, USB and reset lifecycle | Startup READY and explicit HELLO delimit application control. LF framing, size limits and deadline/refusal behavior have host coverage. Real boot-noise handling, reopen after reset and actual display/button operation remain untested. |
| Concurrency and ordering | One app task owns the endpoint, storage, display and raw button; reentry closes the attempt. NimBLE host callback tests are inapplicable because there is no host connection service. Physical main-task stack headroom remains open. |
| Persistence and cleanup | Four distinct NVS namespaces, fresh-only traffic storage, durable role consumption and actual RX retirement are implemented. Host coordinator checks closure before independent original restoration. Physical interruption remains unproved; this trial independently verified original-byte restoration and resets on both boards. |
| Composed validation | 48 actual-crypto session groups, Python bridge/node interoperability and coordinator tests pass. The final affected matrix and exact executable preparation pass; a fake node's simulated gesture is not physical input. |
| Hardware execution gate | One physical attempt refused before comparison; both original readbacks/resets are verified and custody is closed. Diagnose the actual startup path before another trial. Any later attempt still requires fresh exact-image authority, both local decisions and independent restoration. |
| Radio, phone, GPS, battery and field operation | Skipped for this target: no LoRa transport, phone/BLE host, GPS or battery behavior is exposed. This does not close their product or physical acceptance gates. |
| Dependency and legal admission | Reuses the admitted SDK/crypto source path; previous candidate inventories do not constitute a new-image inventory or legal clearance. No crypto selection or release claim is made. |

1. Capture and verify both original application and full NVS images, plus the
   protected bootloader, partition and OTA regions. Fresh-original admission
   rejects the four `ot230_boot`, `ot230_role`, `ot230_tx` and `ot230_rx`
   namespaces. Use fresh, one-use authority for this exact two-role attempt.
2. Write and read back A's candidate, then B's, sequentially with independent
   identity checks. Never flash both simultaneously. A stays idle while B is
   prepared; this phase does not start A's session deadline.
3. Open independently admitted passive USB handles. Provision the two endpoint
   identities and signed local windows, then relay all three handshake messages.
   The bridge must verify full transcript equality before prompting for input.
4. Compare the eight hexadecimal digits shown on both OLEDs and verify the
   displayed A/B roles. If either display is missing or the codes differ, do
   not confirm. These eight digits are a visual aid; the bridge checks all
   32 transcript bytes.
5. With each review visible, release GPIO0 so the device observes a fresh
   release. Press and hold that device's GPIO0 button for 500–3000 milliseconds,
   then release it before its remaining signed deadline. A roughly one-second
   hold is within the accepted range. Repeat independently on the other device.
   A button held at boot or through review does not confirm. A hold longer than
   three seconds after arming refuses the attempt.
6. Require both local confirmations. The display says `LOCAL CONFIRMED` and
   `NOT GROUP JOINED`; one local success does not imply the other. `CLOSE` must
   report successful retirement and secret cleanup independently on both.
7. Positively close both passive serial handles before entering ROM restoration.
   Restore each candidate-touched original application and full NVS, verify all
   saved/protected spans independently, and restart each original. A failure on
   one role must not skip cleanup of the other. Ambiguous serial closure keeps
   custody held for the separately authorized recovery path.

The [custody coordinator](../../tools/pair_confirmation_trial.py) preserves
one-use grants, durable capture/write intent and independent restoration. Its
recovery path needs the retained originals and admission evidence, not candidate
bytes or another handshake capture. The final physical report must record actual
per-role outcomes, closure, restored readbacks and original restart; software
tests cannot supply those observations.

## Test scope and limitations

The [48-group C++ suite](../../tests/host/pair_bench_session_tests.cpp) exercises
real signed v2 provisioning and three Noise messages, distinct boot histories
and clocks, exact transcript presentation, malformed input, no remote confirm,
startup waiting, nonrenewable expiry, button sequencing, callback reentry,
post-confirmation expiry and durable RX retirement. Its optional `node` mode
uses fake storage/entropy/time and a clearly marked simulated local button for
Python interoperability; it is not physical confirmation evidence.

The [bridge tests](../../tests/host/pair_bench_bridge_tests.py) and
[coordinator tests](../../tests/host/pair_confirmation_trial_tests.py) cover
their software boundaries. The maintained
[current-source runner](../../tests/host/security_current_source_ci.py) includes
the new session and Python composition suites. Target SDK/USB/OLED behavior,
physical entropy, interrupted writes, complete lifecycle/rekey and radio/phone
integration require their own evidence.

OT-229 and OT-230 remain one combined publication-pending batch. No commit,
push, PR, public website update or V1 completion change is established by this
report. The failed physical attempt earns no additional acceptance or credit.

The owner subsequently confirmed that both normal Trail screens returned. This
is user-observed original UI recovery, not a fresh protected-phone Ready check.
