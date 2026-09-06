# Name and time integration toward first hardware acceptance

Status: software validation accepted; final reproducible builds and hardware acceptance pending.

The owner authorized consolidation of the remaining dispatcher, persistence,
Android and target work through the first installation and live test. Website
updates and cold-power testing remain deferred. This record does not increase
V1 completion until the affected acceptance gates pass.

## Firmware preflight before target integration

1. Target boundary: reuse the accepted Heltec V4.2 HTIT-WB32LAF,
   ESP32-S3R2, 16 MB flash / 2 MB PSRAM configuration and partition layout from
   OT-177 reconnect and OT-178 OLED adapter evidence. Application offset remains
   `0x10000`. Exact private device identity and installed image require fresh
   prewrite verification. No radio transmission or region capability is added.
   Existing display, inputs, battery, GNSS and console pins remain unchanged.
2. Reproducibility: reuse pinned ESP-IDF 6.0.2 and the existing offline build
   runner, with an explicit new project version and two initially absent build
   directories. Final image/ELF/map/bootloader/partition/config comparison and
   source linkage inspection are pending final integration.
3. Boot/USB/reset: reuse no-stub ROM identity, independent readback and bounded
   runtime-return observation. No serial protocol change. Cold-power execution
   is skipped by owner direction because battery disconnection needs disassembly;
   this does not waive ordinary restart/reconnect validation.
4. Concurrency: protected authorization is prerequisite to profile selection.
   GATT work must reserve fixed request/result capacity and queue execution to
   the app owner; persistence must not run in NimBLE callbacks. Exact generation,
   session, exchange, indication completion and reset/disconnect ordering require
   composed behavioral tests before hardware execution.
5. Persistence: new name data uses isolated `ot_name_v1/record_v1` whole OTNCv1
   snapshots. A failed mutation remains uncertain. Exact fresh-handle readback
   precedes confirmation; reset must erase and verify this entire namespace in
   addition to owner, raw state and bonds. No owner-schema or partition migration.
6. Composition: focused protocol/storage/dispatcher/Android tests, full affected
   host and Android matrices, exact linked firmware builds, and resource review
   remain mandatory final gates. Prior isolated codec results do not satisfy them.
7. Hardware: currently retained restoration image is the accepted reconnect
   application, 563776 bytes, SHA-256
   `9ACDC90EEA9D0489ABABDD9B6F4E3C7AFF97162D03DDC09F8E4965D5A1D12784`.
   Its local bytes were reverified; installed bytes remain a prewrite gate.
   A new one-use manifest must bind both images and exact erase span. Installation
   must preserve bootloader, partition table, OTA selection and user storage.
   Real name/readback, clock display, Ready and reconnect checks remain pending.

Fresh read-only phone observation: one authorized SM-N986U / Android 13, awake,
USB powered, stay-awake enabled and unlocked. No settings changed. Initial
sandboxed Windows board enumeration was inconclusive because CIM access was
denied; no claim of a missing or identified board follows from that result.

## Focused evidence

The real target name-storage adapter passes compiled fake-NVS boundary tests for
absent/corrupt/unsupported records, strict size/kind/revision validation, fresh
handles, pre-mutation failure and mutations that fail after changing bytes.
Factory-reset static admission includes whole-name-namespace erase and fresh
absence verification. These tests do not prove physical NVS persistence.

The composed dispatcher suite passes eight groups using real profile/name/time
codecs, name/time owners, the existing base request coordinator, and the target's
fixed-memory lane predicates. It covers exact shared exchanges, stale completion,
queue-age admission, the 5000-ms name boundary, the 2000-ms time challenge,
uncertain commit reconciliation and disconnect retention. Existing name-owner
12 groups still pass. Target static admission passes 17 groups and the pinned
NimBLE callback/stop-order check passes. Actual stack-level callback timing remains
a physical acceptance boundary.

Final Android validation passes 978 tests: protocol 47, debug 294, release 293 and
V1-Test 344, with zero failures, errors or skips. All affected lint/build variants
and the unsigned release audit pass; all 14 test-only diagnostics remain excluded.
The final V1-Test APK is 12431060 bytes, SHA-256
`079EE792F54F71731470E76512377A2F3D73CEEDAA3A6A885F70555F58F73A98`.
Its package, versionCode 1 and signing certificate match the installed V1-Test app,
permitting a data-preserving replacement. It has not yet been installed.

Firmware compile smoke passes with a 582576-byte application. It links the name
namespace and the privacy-safe app-stack diagnostic. This is not the final
reproducibility result. Main task stack is explicitly 8192 bytes; the pinned IDF
API reports minimum free stack in bytes. Physical margin must be observed after
the accepted request and reconnect sequence.

The first complete host attempt stopped at the raw-byte guard because the reviewed
stack setting changed a pinned input and the edit had mixed line endings. The
baseline was independently reconstructed and verified; the sole semantic change
is `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`. Its normalized CRLF form is 1617 bytes,
46 CRLF, no bare LF/BOM, final CRLF, SHA-256
`c4cdce2c204165d07c23fdabb428ff868bdfe8ebab1c3e111c4cedea8874bcca`.
Only the current guard registry entry changed. Historical receipts and digests
remain intact. All 291 raw-byte inputs pass. The complete local host matrix subsequently passed
in reviewed segments: the unchanged prefix passed109 entries, then the remaining62
entries and final tools passed after correcting the obsolete OLED mapper expectation.
The corrected mapper proves unknown-time placeholders and typed name/time display
without granting region/Ready authority. No production change was made at that
splice. Windows loader and simulator UI13/13 passed. Continuous CI remains separate.

The actual private successor installer's control flow passes seven synthetic-I/O
cases: larger/smaller/equal application spans, partial-write restoration in each
shape, and an unexpected installed image causing no erase or write. Candidate and
restoration erase operations are bounded to the exact application sector span.
These simulations neither access hardware nor prove physical restoration.
