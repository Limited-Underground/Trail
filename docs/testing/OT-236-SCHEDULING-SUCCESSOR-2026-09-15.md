# OT-236 scheduling and diagnostics successor

## Reason and boundary

The [revision-2 trial](OT-236-INTERVAL-TRIAL-2026-09-15.md) stopped after1417
framing records. Watchdog backtrace interference at the final non-yielding Noise
batch is a source-supported hypothesis, not a recovered physical error line.
This successor addresses the independently identified scheduling hazard and the
missing failure detail without claiming that the prior physical cause is proven.

The additive target `firmware/targets/libsodium_capture_yield_eval` composes the
hash-pinned historical application with a single blocking tick before each
sample, before cold conditioning and outside its start/finish timestamps.
`taskYIELD()` alone would not guarantee a lower-priority idle task can run.
The watchdog remains enabled. This is revised benchmark methodology; its timings
must not be presented as a continuation of the historical measured series.

The receiver retains strict framing and reports only fixed rejection categories,
bounded counts and allowlisted context from the last framing-validated record.
It stores neither the rejected text nor a hash of private text. Framing context
does not claim whole-stream semantic validation or seven successful operations.

## Firmware porting preflight

| Checklist | Application to this successor |
| --- | --- |
| Target boundary | Existing Heltec V4.2 bench pair, ESP32-S3,16MB layout; application-only offset0x10000. New host build only. No radio, phones, display, GNSS, battery or peripheral behavior added. Fresh physical identity and originals remain mandatory before a trial. |
| Source/build identity | Historical application and library remain unchanged; exact composed source and scheduling test bind the insertion. New target/project version and initially absent build directories; two independent offline builds, cache disabled, exact artifact comparison. |
| Console and reset | Reuse bounded autorun capture with three-second initial delay, fresh endpoint open and strict post-header parsing. No START/READY handshake is added in this scheduling increment: the prior trial reached1417 frames, so initial readiness is not its observed failure. Physical successor capture remains a gate. |
| Concurrency | Blocking delay occurs outside crypto timing and before cold conditioning. A compiled test exercises the generated loop and ordering; actual idle-task/watchdog behavior still needs physical confirmation. |
| Persistence/cleanup | No target NVS writes. Reuse tested fresh-baseline interval and sequential original restoration. New namespace/package required; old grants remain consumed. |
| Cross-layer identity | Successor binary, protocol diagnostics, coordinator schema/namespace, caller and exact originals must be frozen together before any device action. |
| Unrelated gates | BLE ownership, radio regulatory/range, UI, phone lifecycle, security provisioning and field acceptance are not exercised; no claims or credit added. |

## Validation and limits

The [host/build proof](../../tests/benchmarks/crypto/OT-236-SCHEDULING-SUCCESSOR-HOST-2026-09-15.json)
records exact commands, source pins, tests and artifact hashes. The final matrix
passes123 host tests and9 strict parser groups; the actual generated C loop is
compiled and exercised in four cold/warm success/failure cases. Fourteen private
caller tests and its no-argument offline verification pass. Independent review
checked diagnostics through restoration, namespace isolation, caller bindings
and unchanged interval/authority schemas without finding a blocker.

Two fresh ESP-IDF6.0.2 builds using Xtensa15.2.0 match eight raw pairs: application
BIN/ELF/map, sdkconfig, bootloader, partition table, initial OTA data and generated
C. All733 library files match the retained manifest. The application is293216
bytes, SHA-256 `d5abc19bed29687a459b25e28f14b12cc0ac9d21ef142b9355c0cc61ccf65c61`,
version `ot236-libsodium-yield-v1`; ESP32-S3,16MB DIO80MHz. Both CPU idle-watchdog
checks remain enabled with a five-second timeout; the scheduler is100Hz.

Early sandbox attempts could not invoke the installed compiler. The first
unsandboxed configure exposed an IDF early-pass CMake ordering error; moving
source properties after component registration fixed it. Fresh final builds pass;
failed directories/logs remain preserved privately. No physical action is part
of this increment.
Any successor trial requires fresh reviewed caller/artifact binding and current
authorization, including restoration. No V1 milestone or public website status
changes. Source remains local pending separately authorized publication.


## Frozen successor and next physical gate

The private scheduling-v3 caller binds the new image, current sources and both
unchanged original images. Its manifest/package/caller pins are recorded in the
proof. Revision3 journal/capture paths and a new interval directory prevent reuse
of revision2 state; backend interval schema2 and authority schema1 are unchanged.
No preflight, interval baseline, grant, result or hardware action was created.

The concrete proposal is one sequential application-only trial: fresh identity,
static/original and full-NVS baseline admission; A candidate capture and verified
original restoration; then B candidate capture and verified original restoration;
independent validation of both1621-record captures and normal-screen confirmation.
Offsets and protected spans are unchanged. No phone, RF or NVS-write action is
included. Stop on failure or uncertain device/serial state and use the already
specified original-only restoration path. No automatic retry or consumed grant
reuse is permitted. Fresh authorization for this exact proposal is required by
the project hardware rules before physical execution.
