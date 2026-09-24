# OpenTrail Project Status

As of 2026-09-23. This page summarizes accepted behavior and the next work; dated
history belongs in [PROGRESS_LOG.md](PROGRESS_LOG.md). The complete prior status,
including assumptions and older decision checkpoints, is preserved in the
[2026-09-08 archive](history/PROJECT_STATUS_BEFORE_CLEANUP_2026-09-08.md).
See [archive navigation](history/README.md) for moved headings, [Architecture](ARCHITECTURE.md)
for design and the [backlog](../tasks/BACKLOG.md) for every task identifier.

## Current accepted capabilities

[OT-0300 Android retention/sharing reconciliation](testing/OT-0300-RETENTION-RECONCILIATION-2026-09-23.md) is owner-accepted. Later accepted ownership, group/block, consent and support-export requirements now have explicit policy precedence; historical release-plan approval is not current-candidate admission. No new storage or sharing permission is introduced. [OT-0301 ordered setup](testing/OT-0301-ONBOARDING-2026-09-23.md) now binds name/region steps to fresh protected readbacks; missing public-profile commands and the OT-0302 prerequisite order still block complete onboarding. Physical UI acceptance remains open.


OT-0247d corrected physical trial is owner-accepted. [OT-0238b reconciliation](testing/OT-0238b-PRODUCT-ENROLLMENT-RECONCILIATION-2026-09-23.md) identifies unresolved product bootstrap and retained-identity/exact-epoch semantics before product integration. Durable fresh-session storage already exists and should be reused. The owner accepted the [product design](security/PRODUCT_ENROLLMENT_REKEY_DRAFT_V1.md) on 2026-09-23. [Host identity binding, local review and durable preparation](testing/OT-0238b-LOCAL-PREPARATION-2026-09-23.md) are candidate components. [First-enrollment host composition](testing/OT-0238b-FIRST-ENROLLMENT-2026-09-23.md) now connects provisioned identity, fresh possession, consumed local review, durable invitation/activation and membership gates. [Retained pairing and fresh rekey](testing/OT-0238b-RETAINED-REKEY-2026-09-23.md) now have host composition and interruption coverage. Matching committed peers can start a fresh exact-next-epoch session; uncertain-state repair and physical target adapters remain unfinished. [Trusted screen/button adapter and full display layout](testing/OT-0238b-REVIEW-DEVICE-PORT-2026-09-23.md) now have host coverage; actual target ownership remains unwired. [Target-boundary reconciliation](testing/OT-0238b-TARGET-INTEGRATION-BOUNDARY-2026-09-23.md) preserves the accepted V1 physical-access limitation; hostile flash rollback protection is not a new V1 gate. [Dormant target identity storage/reset binding](testing/OT-0238b-IDENTITY-NVS-2026-09-23.md) adds exact NVS ownership, uncertain-write refusal and stale-instance invalidation during reset. Startup still does not construct or provision this adapter; live owner retirement and the remaining target composition stay open. [Leased target display and complete OLED layout](testing/OT-0238b-DISPLAY-LEASE-2026-09-23.md) add serialized review ownership, reset preemption, full-frame rendering and failure concealment. The path remains dormant: actual BOOT arbitration and product routing are not wired. This is not product-ready enrollment. No new device work is required for this host increment.

[OT241 diagnostic trials](testing/OT-241-DIAGNOSTIC-TRIAL-PREPARATION-2026-09-16.md)
closed with both originals independently restored. The first reached comparison
while the user was away; the same-image retry measured a TX completion deadline
at handshake 3, A/RFPOLL (2060 ms, two attempts/one completion). The user saw a
brief code on retry, but no controller comparison prompt occurred. Normal screens
were confirmed after the first attempt; both normal screens were also confirmed after retry restoration.
[OT242 TX completion correction](testing/OT-242-TX-COMPLETION-CORRECTION-2026-09-16.md)
passes its host matrix and reproducible builds. Its single authorized physical
trial reached matching-code comparison and both user button confirmations, then
failed at activation step 1, B/RFPOLL. The earlier TX deadline did not recur;
A completed 3/3 transmissions and B 1/1. A reported authority/clock fault 3 and
B session fault 6; the exact cause remains unproven. Both originals and saved
states were independently restored, verified and reset, custody closed, and the
user confirmed normal screens. The subsequent host reproduction and diagnostic
provenance correction are accepted with a passing 43-suite matrix, all affected
target builds and reproducible enrolled artifacts. This identifies expiry at its
rejecting check without resampling afterward; it does not prove the physical
cause or fix activation.
[OT243 polling/NVS correction](testing/OT-243-NVS-POLLING-COST-2026-09-18.md)
now preserves fresh durable reads while eliminating repeated size queries for
previously validated slots. The full host exchange passes within its unchanged
window, with 33.2% fewer SDK gets; 43 suites and reproducible enrolled builds
pass. This is host/build evidence, not physical latency or activation acceptance.
The [OT244 controlled two-Heltec trial](testing/OT-244-CONTROLLED-RADIO-TRIAL-2026-09-18.md)
reached both local confirmations, then failed at activation1 B/RFPOLL with
authority/clock fault3 on both nodes. Original applications/state are verified
restored and custody closed; the user confirmed both normal screens. Exact clock guard
and timing remain unproven. The [OT245 independent-idle replay](testing/OT-245-INDEPENDENT-IDLE-REPLAY-2026-09-18.md)
shows only107-133ms additional elapsed time in the tested10s-delay cases; a47s
synthetic delay reproduces the failure with idle on/off. It does not establish
physical cause. The [OT246 failure capture](testing/OT-246-FAILURE-CAPTURE-2026-09-18.md)
is now implemented and validated: original rejecting samples, target button/phase
times and host command/transport costs. The 44-suite matrix, three affected target
builds, reproducible enrolled artifacts and final Python follow-up pass. One exact
instrumented trial is now closed: B rejected at the exact invitation-expiry check,
4ms past deadline, after accepting confirmation with31.984s remaining. A sent the
first activation control, but B returned28 WAIT polls before expiry. Source review
found the receiving radio is not rearmed after the final handshake before that
transmission. [OT247 complete-flow review](testing/OT-247-END-TO-END-RADIO-AUDIT-2026-09-20.md)
corrects guarded receive rearming and per-byte host reads;97 Python tests and
the updated composed flow pass. Its baseline sensitivity cases expire after5 or3
statuses. The [OT-0247c host correction](testing/OT-0247c-RADIO-WORK-CORRECTION-2026-09-21.md)
now passes all eight deliveries and cleanup in both modeled sensitivity profiles;
owner acceptance is recorded. [OT-0247d host preparation](testing/OT-0247d-HOST-PREPARATION-2026-09-21.md)
bound the corrected candidate. The [single physical trial](testing/OT-0247d-PHYSICAL-2026-09-22.md)
completed activation and seven of eight statuses, then expired in B's sender-side
RFPOLL. Both original states are restored and normal screens confirmed. Actual
confirmation polling and delayed-completion rearm costs exceed the modeled command
path. The [OT-0247e actual-bridge replay](testing/OT-0247e-HOST-REPLAY-2026-09-22.md)
completes eight deliveries but does not reproduce physical expiry; its synchronous
serial adapter omits measured empty/partial-read guard work. Quantified residual
evidence is owner-accepted. The [OT-0247f asynchronous model](testing/OT-0247f-ASYNC-SERIAL-MODEL-2026-09-22.md)
now closely matches aggregate reads/guards but still completes eight deliveries.
Matched activation/status phases remain slower physically; production correction
and physical acceptance remain open. OT-0247f evidence is owner-accepted. The
[OT-0247g correction](testing/OT-0247g-GUARDED-COMPLETION-2026-09-22.md) is implemented
locally: nine nominal paired replays deliver eight statuses, with measured savings.
Its report corrects the earlier asynchronous immediate-button model and nominal
result claim. All 44 final host suites, both affected builds, enrolled reproducibility and
independent lifecycle review pass; OT-0247g is owner-accepted;
the [approved corrected-candidate trial](testing/OT-0247d-G-PHYSICAL-2026-09-23.md)
now physically passes three handshakes, four activations and eight statuses with
zero faults/receive errors and stopped radios. Both original firmware/state spans
were independently restored/read back, both reset, custody closed without recovery
retry, and the owner confirmed normal screens. Owner task acceptance is recorded. Phone
integration, retained restart/rekey and V1 completion remain open; no public website
status or weighted V1 credit changes from this trial alone.

The [integrated enrollment candidate](testing/OT-240-INTEGRATED-ENROLLMENT-2026-09-16.md)
combines durable fresh-session allocation, retained signed enrollment, actual
NVS storage, independent activation and fixed-status exchange over USB or
explicitly armed evaluation radio. The combined host matrix, supplemental
operator tests and reproducible target builds pass. The original physical attempt
failed before comparison. The pacing correction then passed focused validation
and matching builds, but its physical trial refused at handshake stage 3 before
comparison. Exact operation and root cause remain unknown. Both originals were
restored/readback-verified/reset, custody is closed, and the user confirmed both
normal Trail screens. Integrated host reproduction now demonstrates the injected
storage-cost/driver timing mechanism; GPIO-edge plus command/idle ticking passes
18 cases, maintained input-loop regression and one clean affected build. Physical
cause remains unproven; no new physical proposal or RF fixed-status acceptance
is established.
The companion semantic bridge is implemented, but protected BLE action routing
and Android receive events/UI are not wired. Product trust/bootstrap, complete
reset/recovery, exact-target interruption/entropy evidence and final admission
remain open. Full V1 testing is not ready; no completion or website credit.

The [OT-236 scheduling trial](testing/OT-236-SCHEDULING-TRIAL-2026-09-15.md)
passed on both Heltecs: eight operations and1621 retained records each, with
independent strict capture/provenance audit. Both exact original applications
were restored and independently verified with protected/full-NVS checks before
release. The user confirmed both normal Trail screens on2026-09-16. The bounded capture gate
is passed; prior watchdog interference remains a hypothesis because its failed
line was not retained. Revised sampling timings remain separate from historical
measurements. The [current admission assessment](security/OT-237-CURRENT-ADMISSION-2026-09-16.md)
binds the completed corpus and identifies the remaining target/product lifecycle
gates. Libsodium remains the evaluation recommendation; Phase3 and production
crypto/wire selection remain withheld. No V1 credit.

The [OT-235 host candidate](testing/OT-235-PEER-TRAFFIC-INTEGRATION-2026-09-15.md)
composes independent local confirmation, authenticated peer activation, durable
activation readback and encrypted fixed-status exchange. Fresh-only receipt and
traffic guards reject replay, invalid records, expired or changed authority and
uncertain persistence without publishing output. Its exact validation is linked
in the scope document. Trusted product provisioning, retained membership/rekey,
production crypto/wire selection and the phone-to-phone path remain open; this
host candidate is not wired into deployable firmware and adds no V1 credit.

The [OT-234 isolated radio target](testing/OT-234-PAIR-RADIO-TARGET-2026-09-14.md) now has bounded physical acceptance: one two-Heltec attempt completed the three-message radio handshake, matched full transcripts and registered both independent physical-button confirmations. Final TX attempted/completed/RX/error counts were A2/2/1/0 and B1/1/2/0, with confirmed stop on both. Both originals/full NVS/protected spans passed restoration/readback/reset; the independent audit verified ten capture hashes and closed custody, one candidate and zero recoveries. The user confirmed both normal Trail screens after restoration. See the [physical proof](../tests/benchmarks/crypto/OT-234-PAIR-RADIO-PHYSICAL-2026-09-15.json). This single close-range bench result establishes evaluation handshake/local decisions, not product trust, membership, application traffic, phone integration, range or V1 acceptance. OT-235 below separates the new host activation/traffic composition from the remaining product provisioning gate. OT-229 through OT-234 were published through PR #38; no website or V1 credit changes.

The [OT-233 host transport increment](testing/OT-233-INDEPENDENT-TRANSPORT-2026-09-14.md) completes host integration of the independent-endpoint adapter over the maintained RadioTransport interface and experimental probe codec, with frames at most 154 bytes. Routing identifiers add no cryptographic authority; loss/expiry and invalid input fail closed without retries or membership/traffic APIs. 39 focused actual transport groups, independent review and the final 896-group matrix plus 26 controls, 47 Python tests and actual interoperability pass. All 954 source pins reverify; both fresh target regression builds match seven artifact pairs. The regression image is distinct from the accepted OT-232 physical image. Target wiring is now covered by OT-234 software evidence above; this earlier checkpoint adds no physical radio or V1 acceptance.

The [OT-232 physical USB attempt](testing/OT-232-PAIR-STARTUP-FRAMING-2026-09-14.md) returned healthy diagnostics on both roles, matching full transcripts and separate local confirmations (B then A), supported by user-confirmed matching OLED codes and physical BOOT hold/release on both devices. Both device session cleanups, passive-handle closure, original readback/restoration and resets completed; custody is closed with one candidate attempt and zero recoveries. Independent evidence audit passes all ten original capture hashes and authority/source pins; original-screen user confirmation remains pending. The earlier startup refusal did not recur in this single trial. This establishes the USB evaluation handshake/local decisions only, with no LoRa, phone, application-traffic, membership, V1 or website claim.

The [OT-231 physical diagnostics](testing/OT-231-PAIR-STARTUP-DIAGNOSTICS-2026-09-14.md) report stage 19/error 15/cleanup 1 on both roles, with matching user-observed OLED codes. The first board was observed before bridge open while the second was being written; the second board was later reported to match. This identifies invalid USB input rejected by the normal application loop before HELLO; the exact byte and emitter remain unknown. Independent restoration audit passes all ten span hashes; both original resets are verified and custody is closed, with one candidate attempt and zero recoveries. [OT-232](testing/OT-232-PAIR-STARTUP-FRAMING-2026-09-14.md) adds a bounded pre-HELLO byte quarantine and one guarded host LF. Final validation passes 857 C++ groups including 30 actual-startup cases, 26 controls, 47 Python tests and actual two-process interoperability with matching source pins. Both fresh builds and isolated preparation pass; independent source/runtime/dependency and scope audit passes. Independent raw-byte comparison confirms all seven artifact pairs match. This software preparation preceded the OT-232 physical confirmation observation above. No physical resolution or new authority is claimed.

The [two-device USB bench attempt](testing/OT-230-TWO-DEVICE-BENCH-2026-09-14.md) failed before comparison: both OLEDs showed `ROLE ? / REFUSED`, the bridge failed, and no human confirmation was requested or reached. One grant was consumed; no retry occurred. Both original applications/full NVS and protected spans are readback-verified, both original resets are verified, and custody is closed; phone Ready/running UI was not independently observed. Cause remains unknown. Passing software evidence remains: 48 new C++ groups, 9 bridge tests with separate-process real-crypto interoperability, 17 coordinator tests, matching seven-artifact firmware pairs, and the final 827-group matrix plus 26 scalar controls and 40 Python tests. Those host tests did not exercise actual target startup. The [OT-231 diagnostic increment](testing/OT-231-PAIR-STARTUP-DIAGNOSTICS-2026-09-14.md) adds fixed stage/error OLED and DIAG evidence, collection from both roles before provisioning, and actual `app_main.cpp` SDK-fault tests. The final 852-group compiled matrix plus 26 controls, 45 Python tests and actual two-process interoperability pass. Both fresh firmware builds match all seven artifact pairs, and isolated preparation passes without enumeration, hardware access or grant issuance. Those software results preceded the OT-231 physical diagnostic observation above. No radio/phone, product membership, V1 or website acceptance is added; OT-229 and OT-230 remain one publication-pending batch.

The [v2 endpoint integration](testing/OT-229-INDEPENDENT-ENDPOINT-INTEGRATION-2026-09-14.md) completes real handshake and separate local confirmations with independent boot histories and clocks. 50 new host groups pass, including late-expiry cleanup and direct session encrypted records. This closes the host endpoint binding gate; trusted target provisioning, radio/phone integration and physical acceptance remain open.


The [version-2 invitation contract](testing/OT-228-INDEPENDENT-INVITATIONS-2026-09-14.md) validates separate boot contexts and local clock windows while retaining one common signed Noise prologue and durable one-use admission. 93 new host groups pass. This removes shared boot/time requirements from the contract; the successor endpoint binding is now host-validated in OT-229; real-device provisioning and radio/phone integration remain open.


The [independent evaluation endpoints](testing/OT-227-INDEPENDENT-HANDSHAKE-ENDPOINTS-2026-09-14.md) now complete actual cryptographic handshake and separate local confirmation over bounded host transport. They own independent session/storage/Ready state and expose no membership or traffic authority.38 endpoint groups and the full affected security matrix pass. The frozen invitation still requires compatible local boot contexts and clock windows; different retained boot generations refuse. Real provisioning, radio/phone integration and complete lifecycle acceptance remain open.


- Two Heltec/Android pairs have protected BLE authorization, Snapshot/Ready,
  saved-owner reconnect, name and region readbacks, and automatic clock sync
  evidence. These are bench-development results, not field or release acceptance.
- The second Heltec/S24 pair has accepted fresh pairing, a recognizable setup label
  retained after timeout, an editable first-use name suggestion, durable Trail
  Bench 2 name and US915 restoration, and warm/app-only recovery. US915 selection
  does not enable radio transmission.
- The latest S24-only app update preserves data/base app and passed exact installed
  readback. It prioritizes authorization controls, removes a misleading mode switch,
  and persists fresh-setup navigation after exact reset verification. Pending reset
  verification wins; authenticated Ready clears the marker. It does not delete
  Android bonds or reconstruct resets performed by an older app.
- The bounded Noise XK radio trial and corrected five-operation mbedTLS comparison
  passed on both Heltecs, with independent restoration of both original images and
  protected regions. These are crypto benchmark results, not product messaging.
- Corrected mbedTLS matched resources, independent deterministic Noise proof and
  an additive entropy lifecycle guard are now host-validated. The Noise proof also
  produced a narrowly corrected adapter for a real-library nonnull contract issue.
  See the [consolidated admission evidence](security/CRYPTO_ADMISSION_BATCH_2026-09-10.md).

## Last verified bench configuration

| Pair | Firmware | Android app | Evidence boundary |
| --- | --- | --- | --- |
| Original Heltec / Note20 | `ot178-phone-v1`, 586,736 bytes, SHA prefix `43AC6DBC` | V1-Test SHA prefix `CCC4F1EB` | Retained paired control; still needs the newer clock correction. |
| Second Heltec / S24 Ultra | `ot171-label-v1`, 587,968 bytes, SHA prefix `984E241D` | V1-Test 12,505,942 bytes, SHA prefix `5FD10EF0` | Trail Bench 2 / US915; latest app-only Ready observed in 31.015 seconds with protected name/region readback and clock-sync acknowledgement. |

These are recorded observations, not a fresh live-device inventory. Full hashes,
setup, and results are in [pairing-flow evidence](testing/OT-171-PAIRING-FLOW-2026-09-08.md)
and [setup continuation](testing/OT-171-SETUP-CONTINUATION-2026-09-07.md).
The latest Android matrix passed 1,104 tests with no failures/errors/skips, all
variant lint/build tasks and unsigned-release audit. The release APK remains
unsigned. Physical fresh post-reset behavior of the latest navigation marker and
normal/large-font candidate visibility remain untested by that update.

## Next meaningful capability

The durable invitation authority is now integrated with isolated target NVS and
trusted startup. The [actual target host proof](testing/OT-208-INVITATION-TARGET-HOST-2026-09-12.md)
passes 130 groups; two [final firmware builds](../tests/benchmarks/crypto/OT-208-INVITATION-TARGET-BUILD-2026-09-12.json)
match all 7 artifact pairs after the documented stack configuration rebuild.
The [SDK inventory](security/OT-209-SDK-LICENSE-INVENTORY-2026-09-12.md) binds allocated
composition and supplied notices to the exact candidate, with supplier/prebuilt
provenance and license limits retained. It does not provide legal clearance.

The [exact-image operator binding](testing/OT-210-INVITATION-OPERATOR-2026-09-12.md)
passes 300 affected tests across 17 suites, two isolated runtime probes
and synthetic package admission. It preserves one-use capture and independent
restoration, and rejects retained state in all eight physical namespaces.
The [audited OT-211 trial](testing/OT-211-INVITATION-TRIAL-2026-09-12.md)
has status `evaluation_failed`. A: evaluation `capture_failed`; strict receipt accepted `false`; BEGIN observed `true`; matching receipt observed `true`; host read bytes `65`; capture at deadline (reported elapsed ms `30000`, clamped); inner endpoint error `endpoint_read_late`; durable stage/error `send_return/none`. B remained untouched by the candidate.
Candidate-touched originals and protected regions were independently restored/verified
and restarted; untouched roles were guarded-released. Custody is closed and grants consumed.
This attempt does not confirm the invitation candidate on both boards. A durable stage or successful send alone cannot substitute for strict receipt acceptance.
The [corrected OT-212 operator](testing/OT-212-DEADLINE-OPERATOR-2026-09-13.md) passes 335 tests in 18 suites, two isolated runtime probes and synthetic package admission. Both deadline crossings now finish observation without another raw read; empty, malformed, late and trailing output remain refused. The [audited OT-213 trial](testing/OT-213-DEADLINE-TRIAL-2026-09-13.md)
has status `pass`. Both roles accepted matching BEGIN markers and strict pass receipts for the invitation candidate through the corrected OT-212 host operator.
Candidate-touched originals and protected regions were independently restored/verified
and restarted. Per-role restoration/release is audited; custody is closed and grants consumed.
This confirms the bounded normal-path invitation evaluation independently on both boards. Its accepted receipt depends on the target's durable boot/role consume, reconstruction refusal, authenticated exchange, duplicate refusal, cancellation and retirement checks.
The [phone confirmation plumbing](testing/OT-214-GROUP-CONFIRMATION-2026-09-13.md) now connects the existing Group screen to exact one-use service-owned offers. Its production adapter remains unsupported; no product join or device acceptance is added.
The [device confirmation owner](testing/OT-215-DEVICE-CONFIRMATION-2026-09-13.md) now replaces direct automatic confirmation in an additive evaluation target with exact pending offers and durable confirm/cancel effects. Owner/actual-target host tests and twin firmware builds pass; transport and human inputs remain synthetic.
The [protected BLE evaluation integration](testing/OT-216-BLE-CONFIRMATION-2026-09-13.md) now connects that owner to the Android coordinator through an opt-in profile. 1,213 Android tests, 120 new C++ groups, affected regressions, two matching evaluation builds and a default-control build pass. Known host-reset/exit faults revoke admission immediately. The counterpart remains locally synthesized; no physical BLE or two-node product acceptance is added.
The [physical trial preparation](testing/OT-217-BLE-TRIAL-PREPARATION-2026-09-13.md) verifies exact inputs and the required 733184-byte original-app recovery span. Both connected phones have different, signature-compatible V1-Test builds; SM-N986U/Android13 is selected for the first case. No installation or board operation occurred.
The [physical BLE attempt](testing/OT-219-BLE-PHYSICAL-TRIAL-2026-09-13.md) installed the exact OT-216 APK in place and exercised one candidate write/readback/boot command. Candidate Ready was not reached; confirmation remained unrequested. Originals were independently restored and the same updated app/bond reached protected Ready again. A pre-custody ADB stdin collision was corrected. The [OT-221 diagnostic operator](testing/OT-221-STARTUP-DIAGNOSTICS-2026-09-13.md) captured successful self-check/runtime startup and heartbeats followed by a restart sequence. Full originals were restored and custody closed; the same phone/app/bond reached protected Ready after restarting the app process and its Bluetooth service. [OT-222](testing/OT-222-RESET-REASON-2026-09-14.md) captured software CPU reset0x0C, but its caller remains unknown. Originals and phone Ready were recovered. [OT-223](testing/OT-223-CORRECTED-CAPTURE-2026-09-14.md) returned zero capture bytes with the proposed no-reset release. That release change was reverted and the redundant-reset claim withdrawn. Original-only verified hard reset recovered same-session phone Ready. [OT-224](testing/OT-224-CRASH-EVIDENCE-2026-09-14.md) now positively identifies an `ot_ble_host` stack overflow followed by software CPU reset. Matched-ELF decoding confirms the FreeRTOS overflow hook; the corrupted backtrace does not identify the full overflowing call chain. Originals were restored and the same phone/data/bond reached protected Ready. [OT-225](testing/OT-225-BLE-STACK-CORRECTION-2026-09-14.md) increases the evaluation host stack to 8192 bytes and adds owner-safe runtime headroom measurement. Affected software checks and two matching evaluation builds plus the ordinary control pass. The approved corrected-image trial reached protected Ready and displayed successful local confirmation. Startup samples retained 5916 bytes of BLE-task headroom. OT-225 retained a host reporting timeout; the fixed host reporting window was then tested without changing device offer expiry. [OT-226](testing/OT-226-POST-CONFIRMATION-STACK-2026-09-14.md) closes the missing measurement using the unchanged firmware: protected Ready, local confirmation and the complete combined recorder pass succeeded. The post-confirmation window retained 3980 bytes of minimum-free BLE-task stack. Full originals and same-session protected Ready were independently recovered; custody is closed. This accepts the bounded local evaluation correction, not remote membership or the two-node product lifecycle.
No V1 completion or public website status changed.

The additive security policy evaluation binds signed invitation and transcript
confirmation to real Noise, durable transmit counters and authenticated receive
replay admission. Actual-source host tests and two matching ESP32 builds pass;
retained or inconsistent state refuses fresh-only admission. This is a same-chip
evaluation, with synthetic trust and confirmation inputs, not product provisioning,
same-key resume, full rekey or phone-to-phone radio acceptance. See the
[consolidated evidence](testing/OT-187-SECURITY-POLICY-EVALUATION-2026-09-10.md).

The executable one-use capture/recovery composition now passes host tests for
source and payload binding, strict serial capture, durable interruption barriers
and exact original application/full-NVS restoration. See the
[capture and recovery evidence](testing/OT-188-SECURITY-CAPTURE-RECOVERY-2026-09-10.md).
That checkpoint established software preparation; the later bounded trial is recorded below.

An isolated private operator runtime now passes external startup-file checks,
parent/child import verification and poisoned-environment probes. Its ROM child
independently checks journal phase, storage spans and exact write payloads. See the
[runtime and physical scope](testing/OT-189-OPERATOR-RUNTIME-PREFLIGHT-2026-09-10.md).
The reviewed PowerShell verifier and host OS remain a trusted boundary.

The initial-backup controller now has host-tested capture, typed reset-only
release and one-use execution handoff. Private application/NVS custody binds the
expected originals and runtime; original reset invalidates executable NVS freshness.
Interrupted no-write trials can be reconciled without fabricated execution events.
See [backup custody evidence](testing/OT-190-BACKUP-CUSTODY-2026-09-10.md).

Live backups subsequently passed on both roles. In the bounded nonradio trial,
role A's candidate image verified and its reset completed, but no solicited
receipt was accepted.
Its original application/full NVS restored and readback-verified before reset;
role B was never flashed and passed guarded original readback/reset. No active
locks remain. The old snapshots are stale after reset; no security pass is claimed.
See [the trial outcome](testing/OT-191-BACKUP-LAUNCH-2026-09-10.md).

An additive receipt observer now has 32 passing differential tests and eight
passing execution/operator composition tests. It records bounded, allowlisted
stages and counts while preserving the frozen endpoint and restoration behavior.
A matching receipt observed is distinct from an accepted receipt; this software
work does not establish the historical failed capture's physical cause.
See [receipt diagnostics](testing/OT-192-RECEIPT-DIAGNOSTICS-2026-09-10.md).

The complete host matrix and successor isolated-runtime probes pass. A fresh
controlled diagnostic trial then received zero bytes across 94 reads/30 seconds
after the transport accepted 63 command bytes. This does not prove firmware
received or executed the command. A's original application/full NVS restored and
verified before reset; B was never flashed and passed guarded readback/reset.
Both active locks are absent and snapshots are stale after reset. The initial
prelaunch inventory refusal was reconciled before the sole actual trial.
See [diagnostic trial evidence](testing/OT-193-DIAGNOSTIC-TRIAL-2026-09-11.md).

Host lifecycle tests now compose the unchanged application, command control,
persistence and policy session with real scalar crypto and simulated device
services. Three cases also link the actual console. Controlled faults distinguish
silent input/startup exits, cleanup and receipt output failure, while strict
capture retains its rejection rules. The retained build/source audit found no
selected startup/configuration contradiction. See the
[lifecycle evidence](testing/OT-194-POLICY-LIFECYCLE-2026-09-11.md).

The additive durable-stage target, bounded NVS decoder and restoration observation
seam now pass composed tests and matching fresh builds. The stage record is
independent of the console and remains separate from a strict policy receipt.
See [stage diagnostic preparation](testing/OT-195-DURABLE-STAGE-DIAGNOSTICS-2026-09-11.md).
The executable successor now binds that image to fresh-backup custody, distinct
runtime-bound authority, one-use observation, ROM-child admission and verified
restoration. Composed tests, isolated runtime probes and the complete host matrix
pass. See [operator package evidence](testing/OT-196-STAGE-OPERATOR-2026-09-11.md).
The controlled trial now has a captured `input_result / input_refused` marker
from A, while its receipt capture received zero bytes. A restored exactly and B
remained unflashed and passed guarded original readback/reset. Both originals
returned; snapshots are stale and grants consumed. See
[stage trial evidence](testing/OT-197-STAGE-TRIAL-2026-09-11.md).
The additive input diagnostic target now distinguishes the actual refusal branch
in one U64 record and reports bounded host reset-return/open/RUN-intent timing.
Focused suites, two matching warning-free builds, composed operator/isolated
checks and ordinary/hostile runtime probes pass. The typed package is sealed;
actual-image composition used synthetic devices and retained originals were
checked offline. Required GitHub checks and publication passed. See
[input diagnostics](testing/OT-198-INPUT-DIAGNOSTICS-2026-09-11.md).
After explicit exact-image approval, the fresh input trial recorded
`input_result / invalid_length` on A. Its host accepted all 63 command bytes but
received no reply. Exact originals and protected regions restored/readback-verified;
A restarted. B was not flashed and passed guarded original readback/reset.
Host and firmware specify the same 63-byte frame. The wrong accumulated length's
cause remains unknown.
Host reset-return-to-RUN timing is not device boot timing. See
[trial and restoration evidence](testing/OT-199-INPUT-TRIAL-2026-09-11.md).
An additive input synchronization candidate now handles bounded discarded input
and retains exact first-frame and terminal reasons before stage advancement.
The image-selected decoder rejects inconsistent records and distinguishes an
interrupted durable prefix from a completed stage. The complete affected host
matrix and two identical firmware build tuples pass. This is a validated software
candidate. Its image-bound executable observation/restoration package now passes
the complete affected host matrix and actual isolated runtime probes. The approved
fresh physical trial on A accepted the command after discarding 26 startup bytes
and reached send-return without a recorded send error. Host capture instead refused
an oversized read (129 bytes across two reads); no strict receipt was accepted.
A restored exactly and restarted; unflashed B passed guarded release. The retained
USB-output mechanism now reproduces that rejection through the actual writer and
endpoint. A successor image adds a challenge-bound BEGIN marker; bounded host
preamble scanning preserves strict post-marker receipt and silence checks.
Two identical builds, the affected host matrix and the exact isolated package
pass. The approved OT-204 trial is now audited: pass.
Both nodes accepted matching BEGIN markers and strict pass receipts.
Original application/full NVS and protected regions were verified and originals
restarted; custody is closed and all grants consumed. Earlier raw byte contents
remain unknown. See the [physical confirmation](testing/OT-204-RECEIPT-TRIAL-2026-09-12.md),
[receipt-boundary correction](testing/OT-203-RECEIPT-BOUNDARY-2026-09-12.md),
[physical outcome](testing/OT-202-SYNC-TRIAL-2026-09-12.md),
[synchronization evidence](testing/OT-200-INPUT-SYNC-2026-09-12.md) and
[validated operator package](testing/OT-201-SYNC-OPERATOR-2026-09-12.md).
The historical byte contents remain unknown. A bounded policy receipt does not close all security gates. The [eight-gate plan](testing/OT-163-CRYPTO-INTEGRATION-GATES-2026-09-10.md) retains
physical entropy, interrupted persistence and remaining admission boundaries.

Historical raw-capture custody remains unestablished for the older libsodium and
Monocypher measurements. Preserve their receipts and seek exact original captures
or a separately authorized successor capture. Do not reconstruct raw transcripts
from aggregate results. Current mbedTLS canonical captures are retained.

Libsodium remains the eight-operation recommendation. Complete applicable Phase 3
admission and explicitly select the library/suite/handshake/KDF/wire before
connecting protected phone messages to authenticated direct LoRa. Current phone
Messages screens do not yet provide that product radio path. The original pair's
pending clock correction can follow its use as the retained control.

The [complete-flow read-cost audit](testing/OT-247-END-TO-END-RADIO-AUDIT-2026-09-20.md)
quantifies why receiver-only savings are insufficient. The approved
[complete correction](testing/OT-0247c-RADIO-WORK-CORRECTION-2026-09-21.md) removes
redundant receiver and already-ready sender polls while retaining final durable
checks and delayed-completion fallback. Host evidence is owner-accepted;
physical activation/status acceptance remains open.

## Remaining acceptance and decisions

Post-release concepts remain separately scoped in the
[future-concepts register](FUTURE_CONCEPTS.md).
[Decision 0036](decisions/0036-post-v2-public-lane-and-assistance-direction.md)
records the post-V2 public-lane and assistance direction; it adds no current
release commitment or completion credit.

- Real protected message commands/events, peer/key provisioning, secure packet
  framing, persistent counters/replay protection, LoRa TX/RX and delivery ACKs.
- Complete reset-domain cleanup, cross-pair denial/isolation, physical rotation
  and large-font flows, automatic production launch and release packaging/signing.
- [Target configuration audit](testing/OT-0101c-TARGET-CONFIGURATION-AUDIT-2026-09-21.md): experimental identity/PHY baseline only; target-specific radio/regulatory, antenna, power/battery and GNSS evidence;
  calibrated battery behavior, sustained clock accuracy and field measurements.
- Protected storage/rollback and selected cryptography remain distinct from the
  already working local BLE ownership/name/region storage.
- Multi-region radio operation is not accepted merely because all twelve region
  choices are represented in settings. Emergency features remain safety aids,
  not guaranteed rescue. Map licensing/offline-provider and later platform work
  retain their architectural gates.

The [canonical V1 record](V1_PROGRESS.json) owns completion. This batch changes
accepted evidence and remaining gates, with no score credit or public website
capability change. Bulk website/IIS synchronization was completed earlier; further
editorial work follows the accepted cadence. Numeric-keypad work and cold-power
disassembly remain owner-deferred.
