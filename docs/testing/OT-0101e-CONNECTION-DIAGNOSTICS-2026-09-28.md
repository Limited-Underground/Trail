# OT-0101e protected connection diagnostic preparation

Date: 2026-09-28. Evidence: **HOST-VALIDATED / BUILD-ONLY**, subject to the
validation results below. The physical cause is still unproven. This increment
prepares observation and a controlled comparison; it does not repair or accept
the device connection, sustained clock, or full GNSS lifecycle.

## Boundary and known evidence

Canonical repository: `C:/lu/OpenTrail`. Active checkout:
`C:/lu/OpenTrail/.private/ot0101e-gnss`, branch `codex/ot0101e-gnss`, base
`cd1ce52801bb3e8fa566f724c1df6ed4dd8c587a`. Existing GNSS/display changes and
private recovery evidence are preserved. OT-0101e revision 1 was approved and
In Progress in the hosted checklist; the owner separately authorized this
host-only preparation. No new hardware, app installation, Git publication,
or deployment was performed.

The [previous trial record](../../tests/hardware/OT-0101e-2026-09-25.md)
records a candidate GPS fix while the S24 protected ProtocolInfo operation
failed after about 5.1 seconds and the clock remained unknown. The app timer
is 15 seconds. Neither a phase label nor a generic platform failure proves
which device/platform check failed. The original firmware and saved state
were restored and independently verified; the owner confirmed the usual screen.

Private source comparison at `.private/ot0101e-config-comparison-2026-09-28.md`
and the actual display probe show repeated redraws when sensor timestamps
change without a visible change. The original/candidate SDK configuration,
Bluetooth and NVS libraries, and protected service permissions matched.
The candidate GAP callback frame was larger with the same 4096-byte BLE
stack. These are useful hypotheses, not proof of starvation or stack overflow.

## Complete flow and expected observations

1. Start with the saved, authorized phone/device relationship. Arm the serial
   collector and receive its explicit ready indication before starting the
   saved-device phone connection, so the first failure is not missed. Discovery
   and system pairing must not be replaced by a fresh Add Device/reset path.
2. GATT discovery selects the exact protected ProtocolInfo characteristic.
   The phone records request initiation, platform initiation result, and a
   process-local connection/request counter without a device identifier.
3. Security and ATT authorization remain active. Firmware records the relevant
   GAP entry, lock acquisition, authorization result, security refresh,
   protected read entry/exit and encoded length. Platform permission/encryption
   rejection can happen before the firmware application access callback.
4. The phone records callback origin, raw bounded status, returned byte count,
   current/stale GATT and exact-characteristic flags, request age, dispatch
   delay and handler duration. A read-admission ACCEPT is not protected Ready. A local request counter labels
   the pending read; Android supplies no token that can prove the origin of a
   delayed duplicate callback on the same GATT and characteristic.
5. Existing protocol decoding and authorization must succeed before Ready and
   clock sync. Timers, authorization checks, response bytes and accept/reject
   decisions are unchanged. A failed read must not be described as a clock fix.
6. The collector retains ordered sanitized boot, event, sample and fixed
   panic/reset categories. Missing callback/sample/boot evidence stays explicit.
   Disconnect, service change, lease-close request, host reset and truncation are
   observations, not reasons to erase the preceding failure.
7. A future device trial must use exact approved firmware/APK hashes, exclusive
   serial custody, current identity checks, and the established independent
   original application/NVS restoration and owner screen confirmation.

## Controlled firmware comparison

| Profile | Sensor acquisition | Battery/GPS on OLED | Bluetooth/security |
| --- | --- | --- | --- |
| A | Existing GNSS and ADC acquisition | Existing candidate display | Ordinary 4096-byte BLE stack and existing checks |
| B | Identical acquisition | Mask only the copied battery/GPS display fields | Same as A |

Both opt-in diagnostic profiles have the same logging. Masking display fields
is forbidden without the diagnostic option. Phone readiness, clock, name,
region, pairing PIN, reset overlays, freshness and display error handling keep
their existing paths. Profile B is an experimental isolation tool, not a
proposed product fix. A/B differences can test display involvement; they do
not separate sensor acquisition from every other candidate change.
The original firmware must first reach Ready with the diagnostic app. Profile A
must then reproduce the failure before a different result in B is treated as
evidence for display involvement. If A succeeds, the earlier failure remains
unexplained; do not declare the product fixed or expand into repeated trials.

The diagnostic event ring has 64 static entries, one consumer and a producer
try-acquire that drops on contention/full capacity. There is no application
retry/wait, UART logging, dynamic allocation or GATT-lock acquisition inside
`record()`. Atomic operations remain implemented by the pinned SDK; this is
not a guaranteed wait-free claim. The app drains at most eight records per
loop and samples BLE minimum free stack and display counters once per second.
Normal service time is resampled after diagnostic draining. Static stack-use
reports record 160 bytes for the ProtocolInfo access function, 48 for the GATT
GAP wrapper, 416 for the runtime GAP handler, and 48 for the recording helper.
These are individual frames, not measured call-chain headroom.

| Event kind | Meaning of a / b |
| --- | --- |
| 1 / 2 / 3 | Protected read entry / lock acquired / security-refresh result |
| 4 | Protected read return code / encoded byte count |
| 5 / 6 / 7 | GATT GAP wrapper entry / lock / wrapper result; a is SDK event type |
| 8 | Exact protected ProtocolInfo read flag / authorization response |
| 9 / 10 / 11 | Connect status / encryption status / disconnect reason |
| 12 | Exact protected-read flag / authorization security-refresh result |
| 13 | NimBLE host reset reason |

`max_us` covers the complete display-service expression, including snapshot
construction and result observation; it is not isolated I2C time. Device uptime
is low 32 bits. Preserve raw order and boot markers; do not derive durations
across wrap/reset boundaries.

## App and capture limits

V1-Test log format 4 retains formats 1 through 3 on read, 512 rows / 64 KiB on disk,
and adds at most 16 diagnostic rows per protected read. Only closed enums,
bounded numbers and ownership flags are admitted. Out-of-range status/length
values are explicitly unavailable. Production has no recorder provider.
Observer exceptions are contained; unrelated or stale callbacks cannot finish
or replace the outstanding operation's diagnostic correlation. Logs contain
no PINs, keys, coordinates, addresses, UUIDs, packet bytes or message contents.

`tools/connection_diagnostic_capture.py` is passive and requires existing,
authorized OT-DEV-002 custody. It never writes/resets/retries/reopens the port.
It binds current USB identity and flushes `CONNECTION_CAPTURE_ARMED` to stderr
only after successful opening and a deadline check, before the first read.
Final sanitized JSON remains on stdout. API readiness callback failure closes
the port with `readiness_error`; callback time remains inside the original
deadline. It keeps fragmented lines across read timeouts,
drops entire overlong lines, and caps collection at 600 seconds / 1 MiB input /
4096 records. A caller process watchdog is still needed for a driver that
ignores timeouts. The parser admits only complete fixed-format lines and
fixed startup categories; no raw panic text or backtrace addresses are retained.

Instrumentation itself adds callback/atomic, logging and stack-scan cost. The
boot marker follows startup initialization; early output can be missed.
A blocked/contained app task cannot drain its ring, and a reset can lose queued
events. No callback record does not prove that the request never arrived.
A successful diagnostic run cannot retroactively validate the old image.

## Firmware-porting preflight

| Required gate | This increment |
| --- | --- |
| Real target boundary | Existing Heltec V4.2 ESP32-S3 bench target; no pin, partition, radio region, pairing or storage layout change. Future physical work is Bench 2 only; identity is rechecked then. |
| Reproducible bytes | Installed ESP-IDF 6.0.2 commit `7101770dc6db2667b3c477cc31365dd1acd6db4e`, Xtensa GCC 15.2.0; ordinary configuration retains BLE stack 4096. Exact final results below. |
| Boot/USB/reset lifecycle | Host-tested passive capture and existing custody operator retained. New physical lifecycle validation deliberately not claimed; no device access authorized in this increment. |
| Concurrency/order | Host tests exercise simultaneous producers, queue capacity/order/reuse and dropped-event accounting. Independent source review covers synchronous GAP callbacks and teardown. |
| Persistence/cleanup | No durable state or cleanup authority change. Recorder failures stay observational; serial handle closes on success/error. Exact restoration remains mandatory for a later authorized trial. |
| Composed validation | Actual display owner/presentation are tested with both profiles, phone status, overlays and render failure; existing security/runtime and capture tests are included. |
| Hardware execution | Deferred: fresh readiness/authorization and exact candidate/app/restore binding are required. Nothing is flashed by these build/test scripts. |

## Validation and continuation

The final firmware-side affected gate passed 13 native suites and 6 Python
suites. It covers the two display profiles, actual startup/OLED rendering,
GNSS, OLED clock/admission, protected GATT session and authorization, runtime
ownership, target source admission, pinned NimBLE ordering, stack observation,
startup parsing and GNSS custody. The final capture suite passes 27 tests, including readiness-before-read,
failed/late opening, callback failure/cleanup and elapsed-time bounds.

| Diagnostic profile | Application bytes | Application SHA-256 | Independent builds |
| --- | ---: | --- | --- |
| A: candidate display | 593,200 | `4351080e407ae8bf33370737e9313ecfb8ec6865e5bb1ed076d2fd094bc85c72` | All seven artifacts identical |
| B: masked sensor display | 593,232 | `6db05a3453870dbcf6c4c9583cd66ef5c64f25bc5b06a7b4eb9b5ae74d161388` | All seven artifacts identical |

Both pairs compare BIN, ELF, map, bootloader, partition table, sdkconfig and
generated config header. Each second build starts in a new directory and
matches its first build after the documented source/environment corrections. Between A and B, configuration, bootloader, partitions,
Bluetooth/NVS archives and 47 of 48 main object files match byte for byte. Only
`app_main.cpp.obj`, which applies the display switch, differs. Both profiles
retain the ordinary 4096-byte BLE stack. The artifacts remain BUILD-ONLY.

Final Android gate: 125 V1-Test and 97 release test executions passed with
zero failures, errors or skips; both variants compiled and both lint reports
reported no issues. Nine changed Android files match the recorded source
hashes. The security-policy and connection-runtime source files match HEAD.
No APK was assembled, signed or installed; on-device export remains unverified.

The existing ordinary and confirmation-evaluation configurations also build
successfully with diagnostics disabled. The evaluation build keeps its separate
8192-byte BLE stack and 24576-byte main-task stack; those settings are not used
by the A/B comparison. No radio or evaluation operation was executed.

| Regression build | Application bytes | Application SHA-256 |
| --- | ---: | --- |
| Ordinary (compile only) | 591,536 | `7aaa8f503b92dc4cb841306aabe6fe49c064cf21d689bd7b99e56be6254ec92a` |
| Evaluation (compile only) | 732,928 | `22d10fd64d633fc8966c177e265dde3ffffa8fdd9e24bbd10f249d9ec3ab2238` |

Private commands/evidence: `.private/Test-ConnectionDiagnostics.ps1`,
`.private/connection-diagnostic-host-tests-final.log`,
`.private/build-connection-diagnostics.ps1`, each build's `diagnostic-build.json`,
`.private/connection-diagnostic-reproducibility.json`,
`.private/connection-ab-comparison.json` and
`.private/ot0101e-android-diagnostics/`. Build tooling uses the existing installed
SDK and offline Android dependency cache. The evaluation dependency was reused
from the existing local checkout only after its pinned inventory and all 731
files matched. No library was downloaded or its accepted hashes changed.

The first compile exposed a misplaced evaluation-only include and an invalid
assumption that the SDK's C++ atomic trait would report always-lock-free. Both
were corrected without changing platform atomics; host concurrency checks and
final builds were rerun. Independent review moved the GATT wrapper completion
marker after configuration cleanup and made app teardown observations explicitly
mean close-requested. Early environment/dependency setup failures remain in
the private logs, separate from product behavior.

The next preparation increment assembled the diagnostic V1-Test APK offline,
using the unchanged existing test certificate. Final package, manifest,
permissions, signer and DEX checks passed; 151 Android inputs remained unchanged.
The exact APK and state/ownership/recovery sequence are in the
[bounded trial plan](OT-0101e-CONNECTION-TRIAL-PLAN-2026-09-28.md). The diagnostic
observer is not a ContentProvider endpoint; its existing bounded typed log is
available through the debug package or the established support export UI.

The new bounded operator preserves the maintained custody/restoration path and
separates diagnostic request/grant schemas from the old GNSS observation grant.
104 affected host checks passed, including three real Windows process-lifetime
probes without devices. The capture must be armed before the phone attempt;
collector termination/reaping and the durable job-release check precede ROM
restoration, including recovery from an interrupted controller.

Next, use the exact operator binding for a separately approved Bench 2
comparison, first checking the original firmware's saved-device Ready/clock.
Record raw bounded errors during A before interpreting B. Retain successful
restoration and screen confirmation as the final gate. No new physical cause,
V1 completion credit, public website status or publication is claimed.
