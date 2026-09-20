# OT-240 Integrated enrollment and fixed-status evaluation

2026-09-16. Both the original and corrected physical attempts failed before
comparison. Latest originals are restored, custody closed and both normal Trail
screens user-confirmed. Integrated host reproduction and a targeted correction
now pass; physical cause remains unproven and no new physical proposal was issued.
This combined batch connects durable fresh sessions, real target storage, enrolled
endpoint traffic, a bounded radio driver and the existing four companion status
semantics. It does not implement the protected BLE/Android receive path or select
production cryptography or a production wire format.

## Implemented composition

The durable allocator consumes a new generation before use and gives each
attempt isolated boot, role, transmit, receive and activation storage. Signed
public enrollment and membership remain retained; old traffic keys never resume.
The candidate permits at most four generations without recycling or an automatic
erase fallback. Existing NVS occupancy can cause earlier refusal.

The additive `heltec_v4_enrolled_eval` target uses the ESP32-S3 NVS backend and
independent endpoint owner. Its `OTENROLL1` interface provisions evaluation trust,
accepts a signed invitation, requires separate local physical-button decisions,
and exchanges authenticated activation controls before status traffic. USB and
RF paths cannot be mixed within an attempt. Explicit `RADIO` arms the otherwise
inert radio: 915 MHz, 125 kHz, SF7, coding rate 4/5, configured 2 dBm, maximum 158-byte
frames and 16 transmission attempts per node within a signed window at most 60s.
No retry or delivery acknowledgement is implemented.

The companion bridge admits only the existing four fixed status meanings. Invalid
semantics cannot advance transmit state; only authenticated receive/replay
acceptance produces typed status output. This is reusable semantic glue, not an
Android message event or a delivered receipt. The passive host bridge relays
opaque records, waits for both local decisions, and verifies explicit closure
and radio statistics. Its operator preparation binds exact artifacts and
independent restoration. This software preparation preceded the physical attempt
recorded below; its accepted evidence is retained unchanged.

## Validation

The [bound host/build proof](../../tests/benchmarks/crypto/OT-240-INTEGRATION-HOST-2026-09-16.json)
owns exact hashes and commands. The fresh affected matrix passed 4006 C++ groups,
26 scalar controls and 67 Python tests, including actual independent C++ processes
through the Python enrollment/status bridge. Supplemental operator suites passed
40 tests (21 coordinator and 19 operator). They are separately recorded rather
than represented as part of the earlier matrix invocation.

Coverage includes generation consumption/interruption/refusal, actual NVS API
seams and retained reconstruction, two fresh confirmed sessions, encrypted
activation/status, packet admission, GPIO/display and driver failure containment,
USB/RF isolation, replay, expiry, late authority changes and cleanup. Host fake
radio and SDK seams are not physical radio or power-interruption evidence.

Two fresh ESP-IDF v6.0.2 / Xtensa ESP32-S3 builds produced byte-identical application,
ELF, map, bootloader, partition table, OTA initializer and sdkconfig pairs. The
application is 525744 bytes, SHA256
`42ca0e38f68dbec3e9c029a0e2203cc70cdcb74ab4fb6eb99fed89335c259dad`.
Both use 16 MiB QIO/80 MHz flash configuration and a 24576-byte main-task stack;
physical stack headroom remains unmeasured.

Each compiler-derived closure records 2886 source/dependency files, 1209 translation
units, 112 linked libraries and 20 license/notice candidates. The sole source
inventory difference is a generated certificate-bundle assembly comment containing
the A/B build path; independent review confirmed equal compiled objects and final
artifacts. The raw inventory manifests are not identical. License candidates and
source pins do not by themselves constitute final license or Phase3 admission.

## Physical attempt and restoration

The separately authorized [physical attempt](../../tests/benchmarks/crypto/OT-240-PHYSICAL-2026-09-16.json)
ended as `trial_failed` before the comparison/button prompt. Both roles have
candidate-write and boot intents; these do not establish successful application
runtime. The exact failed command was not retained. No comparison, local
confirmation, encrypted activation, RF status delivery or phone-path acceptance
was established.

The controller exited 0 after both original applications and full NVS were restored,
readback-verified and reset. Independent audit rehashed all ten original captures,
verified protected capture descriptors and both restoration event chains, and
confirmed closed custody/bridge. One candidate attempt and zero recovery attempts
were consumed. The user-visible normal Trail screen check was pending at that checkpoint;
the later corrected-trial restoration has its own confirmation below.

Review confirmed a source pacing defect: the 100 Hz target loop reads one USB byte
then yields a tick, so the 521-byte signed BEGIN command cannot fit the host's
five-second exchange budget under that path. Since the failing command was not
recorded, this is not proven attribution of the physical failure. The validated
software correction below is a separate successor, not a successful physical retry.

## Pacing and diagnostic correction

The [correction proof](../../tests/benchmarks/crypto/OT-240-PACING-CORRECTION-2026-09-16.json)
binds the applied input-loop change and stage diagnostics. The target now yields
after a bounded completed command or an idle read rather than after every byte;
maximum line length, startup quarantine and authority/button polling remain
bounded. One actual-source extracted-loop regression passes. Its original-source
negative control reproduces 5200 ms for BEGIN and 7000 ms for the maximum line
at 100 Hz and fails the corrected deadline assertion. These are modeled scheduler
costs with mocked command work, not measured device/crypto response latency.

Diagnostics pass 57 focused tests plus actual two-process interoperability.
Fixed stage/reason categories preserve the first failure separately from cleanup;
private controller persistence writes only allowlisted categories. Injected
persistence failures do not prevent child draining/reaping. No raw exception,
identity, key, invitation or ciphertext is added to public failure output.

Two clean corrected builds match all seven raw artifact pairs. The corrected
application is 525760 bytes, SHA256
`d8b520b7bced68b6232574d814d44328a6ce649cc5880678acc5ee62ada5618b`.
The earlier 525744-byte image, complete matrix and failed physical record remain
historical evidence for their exact source/artifact boundary; they are not
relabeled as tests of this successor.

Exact successor preparation passed with no serial enumeration, hardware access
or grant issuance. Its controller/binding/proposal are pinned separately. No
physical approval or retry occurred during that software-correction checkpoint.
The subsequent separately authorized physical outcome follows.

## Corrected physical attempt and closed restoration

The [corrected-image trial](../../tests/benchmarks/crypto/OT-240-PACING-PHYSICAL-2026-09-16.json)
retained `enrolled_failure_handshake_3_target_refused` and
`enrolled_cleanup_protocol_unverified_handles_closed`. This identifies the third
handshake stage, not the exact sender/receiver operation or root cause. No
comparison/button prompt, authenticated activation, fixed-status delivery or
phone-path acceptance was established. Handle closure is distinct from the
unverified protocol cleanup acknowledgement.

Both original applications/full NVS were restored, readback-verified and reset.
Independent rehashing verifies all ten captures, protected descriptors and both
complete restoration event chains. Controller and child exited 0, custody is
closed, and the active lock is absent. One candidate attempt and zero recovery
attempts were consumed. The user explicitly confirmed both normal Trail screens.

Three separate host probes (24 interleaved-session groups, 11 NVS-session groups
and 47 actual-driver groups) expose durable-read amplification and a radio-service
timing mechanism. They do not establish the physical cause. The integrated
actual-driver/storage-cost reproduction below follows those probes. The earlier
pacing fix remains a narrow scheduler correction, not proof of bounded NVS or
cryptographic runtime.

## Integrated review-tick correction

The [review-tick proof](../../tests/benchmarks/crypto/OT-240-REVIEW-TICK-CORRECTION-2026-09-16.json)
binds 18 integrated host cases using the actual extracted input loop, concrete NVS
adapter, real cryptographic review session and actual radio-driver state machine.
At a synthetic 200 microseconds per SDK read, each review tick makes 792 reads.
The old per-byte tick path takes 2.6928s over 17 ticks and refuses transmission
completion; the current path completes in 0.7136s over 4 ticks, including idle.
This proves the injected-cost mechanism, not measured device latency or attribution
of the physical refusal. Serial command dispatch uses a driver-service seam;
it is not a complete replay of the physical session. The private integrated
experiment is distinct from maintained CI coverage.

The first command/idle-only proposal reduced read cost but falsely accepted a
50ms button press during a 9ms-per-byte input drip. It was rejected in review.
The applied correction cheaply samples GPIO transitions while reading bytes and
runs the durable session tick on an edge, complete command or idle read. The
final cases reject the 50ms press and accept a 600ms hold; these button cases use
zero injected storage latency to isolate sampling. Partial input followed by idle
still ticks, and expired/revoked authority stops command dispatch. The maintained
actual-loop regression passes, including release observation within 70ms under
the controlled drip case.

A stable partial line can defer full session cleanup until idle, newline or
overflow; guarded commands still reject expired/revoked authority. Neither the
loop nor its clock context claims immediate USB-disconnection detection.
Membership, entropy, freshness, durable replay and physical confirmation checks
were not weakened.

One clean affected ESP-IDF 6.0.2 target build passed with 41 source pins unchanged
during the build and protected partition/configuration guards intact. Its
application is 525872 bytes, SHA256
`9d4216e440ac9ecaa7a5471afd220f4090ffbf518f00c43e71fd8dde5028a044`.
This is one clean build, not a new reproducibility pair. The intermediate build
stopped during configuration after the button-review finding and remains
superseded. Earlier paired builds and both physical proofs retain their exact
historical boundaries. No device access, new physical proposal or physical
resolution is claimed by this correction.

## Remaining acceptance

Review the integrated mechanism evidence and remaining target-timing limits
before any decision to prepare another physical proposal. Physical decisions, encrypted
activation/status and target lifecycle acceptance remain open. Retained
restart/rekey, exact-target entropy-source failure and interrupted persistence
remain within the existing [eight-gate assessment](../security/OT-237-CURRENT-ADMISSION-2026-09-16.md).
Owner-deferred cold-power work and historical comparison-trace custody retain
explicit limits; unchanged successful primitive/control benchmarks need no repeat.

The current reset operation persists refusal tombstones; it is not complete
product reset, BLE-bond deletion or fresh enrollment recovery. The target clock
context does not independently detect USB disconnection. The bounded deadline
still applies. Product trust bootstrap, final source/license/corpus binding,
independent Phase3 admission and explicit selection remain open.

The normal protected BLE action handler and Android receive UI are not wired to
this endpoint. Existing Messages templates remain local text. Full two-phone/
two-device V1 testing therefore remains pending. The physical attempts above add
no V1 completion or public website status change. No app installation or Git
publication occurred.
