# Name and time integration toward first hardware acceptance

Status: first single-pair name/clock installation, visible display and warm-restart acceptance passed.

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

## Final build and first physical installation

Both initially absent final build directories produced identical application,
ELF, map, bootloader, partition table and sdkconfig bytes. ESP-IDF 6.0.2 is pinned
to `7101770dc6db2667b3c477cc31365dd1acd6db4e`; the Xtensa toolchain is
`esp-15.2.0_20251204`. Component management and ccache were disabled. A managed
dependency lock is not applicable: no external managed dependencies are enabled.
The linked application includes the real dispatcher, name NVS adapter and clock
owner, with version `ot170178-live-v1`.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Application | 582576 | `40D0719FFEA879D744CD32CDC7302D8C3D8D61B99CDD22AAE39FA1B296D5FF32` |
| ELF | 8486788 | `3DE4D8EDB3D2D57F09BAB3DA92F012C3AEA46AA1A9046A256511F4D9E062BBA2` |
| Map | 6968355 | `F2722C9E3A2589703280155E2F8F27D8AFE681C970177832AC502C45EC3CA4FB` |
| Bootloader | 22480 | `96E83EBE4434CD6C9049A59F396B4F8BD06C159B40259DA573BDB701C571ECA5` |
| Partition table | 3072 | `F3372A1F30CBDD98D6FBCF7808C85C46DCAA249105BA9DA883EF21E05EFE90A4` |
| sdkconfig | 106877 | `5519CBF48461633E814CC7D1608D01BEB283D3A2D7C45829877D2EB2BEA7A71E` |

Published source `f77bfda980593b9d995dd0aea90fc07ded352bcb` passed continuous
GitHub validation run `34043867586`.

The first installer attempt stopped before any erase/write because importing the
identity module from the isolated checkout selected its absent default private
registry. The successor explicitly loaded the owner's existing registry, checked
enrollment before ROM access and matched the physical device. No enrollment or
registry mutation occurred. Six additional synthetic cases exercise the real
identity validator and prove early registry rejection and prewrite identity checks.

On OT-DEV-001, the successor independently verified the old application, erased
tail, partition table and factory OTA selection. It wrote only the application
sector span at `0x10000`, 585728 bytes, and independently read back the exact new
image plus 3152 erased tail bytes. Identity matched again before the successful
hard reset. No write retry or restoration was required. Bootloader, partitions,
OTA selection and NVS were not written. Receive-only observation recorded two
increasing heartbeats at elapsed 25717/30717 ms and minimum free app-task stack
4688 bytes. This is startup margin, not post-configuration acceptance.

The matching V1-Test APK listed above was installed on the retained SM-N986U,
Android 13, using a data-preserving replacement; installed bytes matched exactly.
No uninstall, data clear, pairing reset or phone setting change occurred.

The first live returning-owner connection reached protocol discovery, MTU and
indication subscription, then rejected the post-authorization protocol read with
`PROTOCOL_INFO_FAILED`. The real Android operation gate did not admit an Info read
from its subscribed READY stage. Fake GATT runtime tests had not exercised that
gate. Name writes and clock synchronization were not attempted during this failed
connection. Correct the gate, validate the actual transition, update only the APK
and repeat live acceptance; the firmware remains installed and running.

The Android correction permits one protected Info read from a subscribed READY
gate and returns to READY after completion. It blocks overlapping reads/writes,
duplicate subscription and completion after close. The runtime name/time test now
uses the real operation gate, and the trace records accepted authorization before
the separate protected reread. Independent review found no blocking issue. Focused
policy/runtime/trace tests pass 73 cases; the final Android matrix passes 979
tests (47 protocol, 294 debug, 293 release, 345 V1-Test), all lint/build variants
and the release exclusion audit. The corrected APK is 12431153 bytes, SHA-256
`598C249CB6761B18AFBC3A486F96AEEADA22DC52AFDF69B080BBD101197661AA`.
Its signer matches the installed application. A second data-preserving replacement
and exact installed-byte check passed. Firmware bytes did not change.

With that corrected APK, process generation 13 reached accepted authorization,
Snapshot and Ready in 1827 ms after connection attempt (the preceding 30-second
discovery is excluded). The live configuration card appeared. Its Read device
button then exposed a second integration defect: LocalBinder inherited the
configuration interface's false defaults instead of forwarding the three new
methods. The UI stayed unchanged and no name write or time request was attempted.
This is separate from the now-corrected GATT gate failure.

The owner's physical photograph during this check confirms the actual Heltec OLED
shows REGION REQUIRED, RADIO TX DISABLED, DEVICE and TIME --:-- while the Note20
shows the connected configuration panel. This accepts the initial visible
placeholder/warning surface only; it is not evidence of name persistence or sync.

The binder correction adds exactly three thread-guarded forwards to the attached
session owner. Composed Activity/owner lifecycle tests and a source-level binder
admission check pass; no Android framework execution is claimed by those JVM
tests. The full Android matrix then passed 985 tests (47 protocol, 296 debug,
295 release, 347 V1-Test), all lint/build variants and the unsigned release audit.
The final V1-Test APK is 12431215 bytes, SHA-256
`6F435F22FAA9AB85F2DFA0CFAE75CF99F4EEDD572BEDD49B83BA995245F68B72`.
Its verified signer matches the prior installation. The data-preserving upgrade
and exact installed-byte check passed. No firmware bytes changed.

The real UI then completed the actual binder path: Read device returned an absent
name, Apply name saved the synthetic label Trail Bench and showed both
"Device name saved and read back." and "Last device readback: Trail Bench".
Sync display clock returned "Display clock synchronized.". Process generation 14
reached fresh authorization/Snapshot/Ready in 1505 ms after connection attempt,
excluding the preceding 30-second discovery. Receive-only serial observation after
the requests showed increasing heartbeats and 3408 bytes minimum free app-task
stack. App restart, warm board restart and final OLED confirmation remain separate
acceptance observations below.

## Accepted restart and display observations

The owner confirmed the physical display shows Trail Bench and the current time
matching the Note20, with both region/TX warning rows retained. After a real app
process stop and relaunch, process generation 15 completed fresh authorization,
Snapshot and Ready in 1803 ms after connection attempt (30-second discovery
excluded). A new Read device operation returned Trail Bench; no draft value was
used as device evidence.

A separate identity-checked warm restart used only ROM read-mac/reset commands:
zero flash writes, no power removal, no bond/owner/data clear. The board returned
with increasing heartbeats and startup minimum free stack 4688 bytes. The owner
explicitly confirmed Trail Bench remained visible while time returned to --:--.
The still-running app automatically reconnected, with fresh authorization,
Snapshot and Ready in 2883 ms from its observed reconnect attempt. A fresh device
read returned Trail Bench, and a new clock challenge/sync again reported success.
Final receive-only observation showed increasing heartbeats and minimum free
app-task stack 3456 bytes; the lowest observed margin across this sequence was
3408 bytes. This is not a long-duration resource, heap or power measurement.

These observations accept the first real single-pair name persistence, volatile
clock synchronization and saved-owner restart/reconnect slice. Firmware target
partial credit changes 25 to 30; Android companion changes 60 to 65. Other
milestones remain unchanged: weighted V1 exact 45.50%, displayed 46%. These small
increments are evidence-based milestone judgments, not completion of OT-170,
OT-178 or V1. Cold-power, destructive reset/erasure acceptance, radio-region and
group onboarding, two-pair operation, radio/field and release gates remain open.
Website synchronization/deployment remains explicitly deferred to the owner's
bulk update. No radio transmission or region selection occurred.

The installed firmware is now the name-aware 582576-byte application listed above.
After the accepted name write, the older reconnect image is not an accepted
whole-user-data reset/recovery implementation; retain the verified name-aware
artifact for a future compatible recovery decision. The initial restoration
manifest remains historical evidence of the pre-name installation boundary.
