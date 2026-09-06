# OT-177 firmware reconnect correction - 2026-09-06

## Correction and evidence boundary

The exact-indication callback now releases the physical response port and clears
its adapter tuple even when lifecycle completion rejects promotion. Full tuple
validation and the reentry guard precede cleanup. Failed authorization remains
failed; stale callbacks cannot release another indication.

The Heltec binding now assigns the provisional session nonce from its existing
boot-local increasing session counter. It rejects zero and values above
UINT32_MAX before entropy consumption or narrowing. Only successful resolution
advances the counter; the same exact connection generation returns its cached
binding. Boot and controller bindings remain secure-random. The normal-session
replay guard is unchanged, including lower/reused nonce and exhaustion rejection.
This restores the existing BLE_COMPANION_GATT_V0 contract and Decision 0009;
it does not introduce a wire version or make the nonce an authentication secret.

The original physical console established response_path_busy. The prior physical
nonce sequence was not captured, so these host-proven defects remain consistent
with that symptom rather than proof of its historical trigger. This increment
performs no device access, firmware installation or physical acceptance.

## Firmware porting preflight

Recorded before target edits and reconciled with docs/firmware-porting-lessons.md:

1. Boundary: existing Heltec V4 ESP32-S3 bench target, 16 MB QIO 80 MHz flash,
   quad ESPPSRAM16 80 MHz, USB Serial/JTAG INFO console. Existing partition
   OTHP0/v1 factory application offset 0x10000, size 0x4f0000. Board inputs,
   display, battery, GNSS and radio bindings are unchanged; no new hardware or
   frequency-plan claim. Only this target links the changed adapter source.
   Existing target pin/profile/source-linkage admission tests remain required.
2. Reproducibility: pinned ESP-IDF v6.0.2, explicit ot177-reconnect-v1 project
   version, existing dependency configuration with component manager disabled,
   ccache disabled; two initially absent build directories. Compare application,
   ELF, map, bootloader, partitions, configuration and resource reports below.
   Historical hash-authoritative evidence is untouched; no frozen digest update.
3. USB/reset lifecycle: no serial or device access. Unchanged console and reset
   code are build-inspected; physical handshake/reset testing is deferred.
4. Event ordering: exact completion releases transport independently of failed
   application promotion; repeated/stale callbacks remain rejected. Existing
   callback reentry, rollback, unsubscribe and timeout coverage remains active.
   Entropy failure precedes allocation; cached generation precedes exhaustion.
5. Persistence/cleanup: no persistent schema, bond deletion, reset domain or
   ownership mutation changes. Exact volatile indication cleanup is tested.
   Destructive cleanup/power-loss gates are out of scope because no such code
   or hardware operation is changed.
6. Composed validation: regression fails against the old adapter at the stranded
   port assertion. Corrected path completes sessions 2 -> 1 -> 3: sessions 2 and
   3 promote and complete Snapshot, session 1 still rejects its lower nonce, all
   transport slots clear, and duplicate terminal completion is rejected.
   Full affected host matrix and exact Heltec builds are recorded below.
   Target allocation ordering/exhaustion is source-admission checked; the
   board-dependent allocator is not executed natively. Existing host protocol
   tests execute normal-session UINT32_MAX and request-ID exhaustion.
7. Hardware preflight/acceptance: skipped because no hardware execution is in
   scope. Future installation requires fresh target/readback/recovery checks
   and separate authorization. Build success is not reconnect acceptance.

## Validation results

Two clean builds passed with ESP-IDF v6.0.2 commit
`7101770dc6db2667b3c477cc31365dd1acd6db4e`, Xtensa GCC 15.2.0,
Python 3.14.6, CMake 4.0.3 and Ninja 1.12.1. Both used:

```text
idf.py -C firmware/targets/heltec_v4_bench -B <fresh-build>
  -D SDKCONFIG=<fresh-build>/sdkconfig
  -D SDKCONFIG_DEFAULTS=<target>/sdkconfig.defaults
  -D PROJECT_VER=ot177-reconnect-v1 -D IDF_TARGET=esp32s3 build
```

`CCACHE_DISABLE=1` and `IDF_COMPONENT_MANAGER=0`; no managed component lock is
introduced because this target uses the pinned local IDF components.
Both build directories were initially absent. All six artifacts match exactly:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| opentrail_heltec_v4_bench.bin | 563776 | `9ACDC90EEA9D0489ABABDD9B6F4E3C7AFF97162D03DDC09F8E4965D5A1D12784` |
| opentrail_heltec_v4_bench.elf | 8116752 | `7E236C9514327ABF34B973E19599D988E2B34690AA9800A5763CFE10B663C0F0` |
| opentrail_heltec_v4_bench.map | 6864807 | `0B07C44A85FC13894A035BC66BC2CA5763DCD63A634E08C6D9F6C78F86ABF248` |
| bootloader/bootloader.bin | 22480 | `96E83EBE4434CD6C9049A59F396B4F8BD06C159B40259DA573BDB701C571ECA5` |
| partition_table/partition-table.bin | 3072 | `F3372A1F30CBDD98D6FBCF7808C85C46DCAA249105BA9DA883EF21E05EFE90A4` |
| sdkconfig | 106889 | `E269012D644B443E3C0705A81F26609261FFB4A3FB65CCF05D863BEE4BF9A5DE` |

Generated configuration confirms ESP32-S3, 16 MB QIO, quad PSRAM, Serial/JTAG,
reproducible application build and SC-only BLE. Both compilation databases link
the changed adapter and target binding sources. Resource JSON reports match:
Flash Code 369,380 bytes, Flash Data 95,632; DIRAM 88,914 / 341,760;
IRAM 16,384 / 16,384; RTC SLOW 36 and RTC FAST 24 bytes. Application partition
has 89% free. These reports are build observations, not runtime memory acceptance.

Sandbox build initially failed in the compiler launcher with Windows access
denied error 5 before project compilation. The same configuration under the
normal user produced both matching builds. The host matrix later hit current-user
DPAPI key_store_unavailable in the sandbox; the remaining matrix was resumed from
that exact test under the normal user and passed. These environment failures do not establish
sandbox corruption or a firmware defect.

Full `tools/Test-Host.ps1` matrix passed across the completed sandbox prefix and
exact normal-user resumed tail. Adapter regression: 15 groups; target admission:
16 groups; Windows loader: 59 groups; final simulator UI: 13/13. No passing prefix
was rerun. Independent reviewers found no blocking issue in cleanup, nonce
allocation, cache/exhaustion behavior or evidence claims. Additional governance
10, Android release-admission 23, publication-safety and diff checks passed.
All 101 recorded owner-worktree file hashes remained unchanged.

Every milestone completion value remains unchanged: Android 60%, V1 weighted
43.75% / displayed 44%. Active weights remain positive and total 100; future
tracks remain unpopulated. Website synchronization/deployment remains explicitly
owner-deferred. Next gate is a separately authorized application-only install
and saved-owner initial Snapshot/Ready, app restart, and repeated reconnect
acceptance, with fresh target/readback and verified restoration checks.
