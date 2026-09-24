# OT-0238b leased target enrollment display

Date: 2026-09-23

Status: host/build-validated inactive display lease; BOOT and product composition remain open.

## Bounded result

The real Heltec bench display owner now supports an exclusive, revisioned review
overlay, and its actual OLED driver renders all eight rows of the accepted
21-column layout. The complete fingerprint or comparison code, full group,
role, purpose and instruction fit without clipping. The missing colon glyph
now renders the group separator. Neither pixels nor the lease approve enrollment.

No startup or packet route acquires this lease. BOOT sampling still belongs to
the existing reset input; this increment does not connect the trusted review
input port or activate enrollment. The accepted
[review-device contract](OT-0238b-REVIEW-DEVICE-PORT-2026-09-23.md) still requires
one serialized application owner for context, all input consumers, display,
reset preemption and fresh stable-release handoff. Existing reset overlays can
preempt a review lease, but physical reset/input arbitration is not established.

ACTIVE_PROJECT_ROOT: `C:/lu/OpenTrail`.
ACTIVE_WORKTREE_ROOT: `C:/lu/OpenTrail/.private/ot177-publication`.
Starting source: `46ca4af65d59377a3c068dfe69ae62182c99cbd9`.
The coordinating agent verified live OT-0238b revision 1 approval and In Progress
status before this bounded increment. No hardware operation is authorized here.

## Source preflight and ownership

The [firmware porting checklist](../firmware-porting-lessons.md) and lifecycle
review were applied before implementation.

| Preflight | Actual source boundary and result |
|---|---|
| Target | Existing Heltec V4 bench ESP32-S3; unchanged 16 MiB QIO/80 MHz flash, PSRAM selection, OTHP0/v1 partitions and application offset 0x10000. Existing board-revision/controller uncertainty remains. Standard and opt-in confirmation profiles are affected. |
| OLED | `heltec_v4_oled.cpp` keeps the existing 128x64 driver, SDA17/SCL18/reset21, Vext36 and 0x3C address. Complete review rows use the existing 5x7 glyphs in 6x8 cells; the final cell ends inside the panel. No new board compatibility assertion. |
| Display ownership | `heltec_startup_display.cpp` owns normal, PIN and reset writers. New exclusive leases are nonzero, increase without wrap and cannot be copied with their owner. Normal views remain current behind review; PIN cannot acquire its display. |
| BOOT/reset | `app_main.cpp` still polls `HeltecV4FactoryResetInput`, which samples active-low GPIO0 directly. No input code changed. Reset display takeover invalidates the review lease before rendering, including a failed reset render. Future review/reset gesture handoff is explicitly unwired. |
| Shared type | `enrollment_review_display.hpp` extracts the unchanged bounded layout data and adds glyph/cell validation without cryptographic dependencies. `enrollment_review_layout.hpp` still owns canonical identity/code layout generation and semantic validation. Target cells alone provide no trust authority. |
| Bytes and admission | New raw header is LF, no BOM, final newline. Existing raw/normal-text line bytes and the Test-Host BOM/mixed EOL are preserved. Source pins were frozen before final builds. Existing source lists remain unchanged; Test-Host adds the lightweight header include directory. |
| Affected closure | Conservative quoted-include traversal across 18 firmware targets reaches changed files only from `heltec_v4_bench`. Both profile builds must confirm actual compiler dependencies and linked renderer. |
| Concurrency/timing | Serialized application-task calls only; no new GATT callback work, task or locking guarantee. Physical display latency, stack high-water and full enrollment timing remain unmeasured. A successful SDK draw return is simulated transfer evidence, not proof a human saw the panel. |
| Storage/transport | No durable mutation, BLE/radio routing, console, USB, serial, deadline, security selection or reset-erasure change. Their physical/recovery gates are skipped because these paths are unchanged and no device operation is authorized. |

## Lifecycle and failure behavior

| Before / trigger | Required effect | Failure or stale action |
|---|---|---|
| Available normal display / acquire | One fresh lease, revision zero; no approval or retained fingerprint bytes | PIN, reset, unavailable display, active lease or exhausted counter refuses acquisition. |
| Current lease / next canonical layout | Entire 1024-byte frame reaches the driver; revision advances only after successful render | Wrong revision, malformed/unterminated rows, hidden trailing content, unsupported glyph, clock or draw failure invalidates lease, attempts concealment and latches display unavailable. |
| Active review / normal status update | Retain latest normal view without drawing over review | No stale normal view is restored merely because its earlier render was successful. |
| Active review / release | Invalidate lease, render latest retained normal view | Restore failure attempts concealment and cannot claim successful release; a stale lease never affects a newer lease. |
| Active review / reset overlay | Invalidate review before reset rendering; reset keeps display ownership | Late review render/release refuses without restoring over reset. Failed reset render attempts concealment and leaves no valid review lease. |

The driver clears its temporary pixel buffer after use and retains no layout in
the owner. Display status exposes only lease/revision and existing categorical
normal status, not review digits. Concealment uses the existing blank-frame,
panel-off and power-off attempts; software cannot guarantee physical concealment
if all hardware paths fail. Such a failure never yields a successful lease or
review release.

## Validation

Focused native compilation uses GCC 16.1.0 with C++17, warnings as errors and the
maintained Test-Host source sets. Actual display owner/driver code is compiled;
only ESP-IDF I/O is simulated.

- Startup owner: 18 groups passed, including exclusive/fresh leases, retained
  normal view, PIN exclusion, next revision, stale release, reset preemption,
  render/restore failure and malformed layout concealment.
- Actual OLED driver: 14 groups passed, including all eight rows, final column,
  every one of 64 fingerprint digit cells, full group and colon pixels, erased
  prior rows, missing NUL, hidden bytes, draw failure, clock failure, emergency
  power concealment and stale cleanup after reset.
- Existing target presentation mapping: 7 groups passed.
- Actual canonical enrollment layout: 21 groups passed, now also proving
  generated identity/code layouts pass the shared target cell validator.
- Actual review-device port: 36 groups passed. This remains a host input-port
  contract test, not a claim that GPIO is wired.
- `python tests/host/heltec_v4_bench_target_tests.py`: 17 groups passed.

The two affected security consumers were freshly compiled and linked using the
recorded compiler commands and unchanged supporting objects from the prior
successful security matrix. The 56-suite matrix was not repeated or relabeled
as a new current-source run; the shared change moves a data type and adds a
render-only validator, with direct consumers tested above. The focused command
records, outputs, dependency closure and frozen source hashes are retained in
private evidence. Independent source review found no blocking issue within this
serialized, inactive display boundary.

## Target builds and remaining gates

Both standard and confirmation profiles passed two builds in initially absent
directories with the proven explicit ESP32-S3 path, ESP-IDF 6.0.2, Xtensa
esp-15.2.0_20251204, CMake 4.0.3 and Ninja 1.12.1. Component downloads and
compiler cache were disabled. All eight raw A/B artifacts match per profile:
application BIN, ELF, map, bootloader BIN, partition table, OTA initialization
data, sdkconfig and sdkconfig.json. Compiler dependency records match and prove
the shared header plus real display owner/driver were compiled; the actual ELF
contains the OLED review renderer. No compiler warnings were found. Frozen
source pins still match after all builds and artifact inspection.

| Profile | Project version | Application bytes | SHA-256 |
|---|---|---:|---|
| standard | `ot0238b-display-v1` | 590016 | `aa83e92498fc623971b44c6389f2336bdd07395c34c4342df00dba66ad2f14b2` |
| confirmation | `ot216-ble-confirmation-v1` | 731392 | `45062c9889db6a9cd13720938d2f5d80a55e8400638604ba8b738fcadbe74ec9` |

Both retain ESP32-S3, application offset 0x10000, 16 MiB flash, Secure Connections,
NVS bond persistence and reproducible-build configuration. The standard host
stack remains 4096 bytes; the confirmation profile retains 8192 bytes. These are
configuration checks, not measured stack headroom or physical acceptance.

The initial sandboxed ELF-symbol inspection hit the installed Xtensa wrapper's
path-resolution panic (exit 101 / error 5). Its stderr is preserved as an
execution-environment failure. The same bounded read-only audit then passed
with approved local build-tool access; no firmware rebuild or source change was
needed. All four builds used that proven build-only access from the start.

Validation records reside in private `ot0238b-display` evidence: focused/native
commands and outputs, source pins, target include closure, four clean-build logs
and exit records, dependency inventories and the final artifact comparison.
`python tools/check_repository_docs.py`,
`python tests/host/repository_docs_tests.py` and `git diff --check` passed,
including 18 repository-documentation regressions.


Next composition gate: bind trusted context and actual BOOT observations to the
accepted device-port contract, inhibit reset reuse until a fresh stable release,
and serialize all display/reset/input actions in the real application owner.
Product request routing, complete enrollment storage/reset integration, lifecycle
timing and separately authorized physical visibility/gesture acceptance remain
open. No product enrollment, V1 credit, website status, physical compatibility,
firmware installation or publication acceptance is established by this increment.
