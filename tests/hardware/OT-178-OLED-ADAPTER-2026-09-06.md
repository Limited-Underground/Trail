# OT-178 OLED target adapter - 2026-09-06

## Firmware-porting preflight

Recorded before target integration against `docs/firmware-porting-lessons.md`.
This increment prepares and validates the existing Heltec target adapter on the
host. It performs no physical flash, serial, reset, radio or phone operation.
Build and physical results must be recorded separately from this preflight.

1. **Target boundary - verified from existing evidence and current source.**
   OT-103 admits the received OT-DEV-001 Heltec WiFi LoRa 32 V4.2,
   HTIT-WB32LAF high-band variant, ESP32-S3R2 revision v0.2, 40 MHz crystal,
   16 MB flash and 2 MB PSRAM; this remains experimented bench hardware.
   Current defaults select 16 MB QIO flash at 80 MHz and quad ESPPSRAM16 at
   80 MHz. Those are build settings, not new physical frequency measurements.
   Existing 128 x 64 display binding uses the IDF SSD1306-compatible driver,
   I2C address 0x3C at 400 kHz, SDA GPIO17, SCL GPIO18, reset GPIO21 and
   active-low Vext GPIO36. The exact OLED controller die is not newly verified.
   BOOT/USER is active-low GPIO0; normal hardware reset remains separate.
   Battery sampling uses ADC1 channel 0/GPIO1 with control GPIO37; GNSS uses
   enable GPIO34, reset GPIO42, RX GPIO39 and TX GPIO38 at 9600 baud.
   Existing battery estimates and GNSS observations do not establish battery
   calibration, charging state, location accuracy or new telemetry support.
   The retained high-band/US915 bench context is unchanged; no LoRa transmit
   path or radio configuration is added by this display work.
   OTHP0/v1 factory application remains at 0x10000, size 0x4F0000. OTA data
   0x9000/0x2000, NVS 0xD000/0x3000, OTA slots 0x500000/0x500000 and
   0xA00000/0x500000, and ot_state 0xF00000/0x100000 remain unchanged.
   The application build must link each added renderer/adapter/clock source
   explicitly; compilation-database and ELF inspection remain final gates.

2. **Reproducibility - established path selected, new results pending.**
   Reuse pinned ESP-IDF v6.0.2 commit
   `7101770dc6db2667b3c477cc31365dd1acd6db4e`, Xtensa GCC 15.2.0,
   Python 3.14.6, CMake 4.0.3 and Ninja 1.12.1 from OT-177's accepted builds.
   Disable ccache and the component manager; use two initially absent output
   directories and explicit version `ot178-oled-adapter-v1`. No managed
   dependency import or frozen historical evidence digest is changed.
   Compare BIN, ELF, map, bootloader, partition table, sdkconfig and resource
   reports from both builds; inspect the generated target and source linkage.
   The established Windows compiler launcher failed under the sandbox in
   OT-177 but succeeded under the normal user; this is an execution-context
   prerequisite, not evidence of compiler or target failure.

3. **Boot/USB/reset - preserved, physical gate deferred.** USB Serial/JTAG
   remains the INFO console, with existing boot/reset handling. Display output
   must not create a new serial control protocol or perform I/O in BLE callbacks.
   Fresh endpoint identity, explicit DTR/RTS handling, bounded runtime-return
   observation and normal-reset recovery are deferred because there is no
   hardware execution in this increment. Prior OT-177 reset/runtime evidence
   does not accept the new image's runtime behavior.

4. **Ordering/concurrency - implementation and tests required.** Keep OLED
   rendering in the application owner task. Snapshot publication must be
   coherent and fixed-memory; any NimBLE-owned facts need safe access rather
   than direct mutable-structure reads. Self-check/failure, reset confirmation
   or erasure, and pairing surfaces must retain exclusive precedence over
   ordinary status. Failed rendering must conceal sensitive content using the
   established fallback. A raw BLE connection must never become Ready wording.
   Unknown device name, region authority, group/location state, radio activity
   and clock sync must remain unavailable until their real sources exist.

5. **Persistence/cleanup - unchanged boundaries.** No ownership, bond, NVS,
   reset-domain, credential, protocol or storage-schema mutation is planned.
   Exact existing pairing/reset cleanup stays authoritative. Destructive
   persistence, power-loss and candidate-bond cleanup execution are deferred
   because this increment changes display presentation, not those authorities.
   Host tests must still prove display priority and concealment on their
   published transitions; unimplemented phone clock synchronization is not
   inferred from the host clock component.

6. **Composed validation - pending final evidence.** Run focused presentation,
   clock and actual adapter-mapping tests, then the complete affected host
   matrix once. Add source-linkage and priority-routing admission checks and
   build the exact target twice. Include unknown/stale/invalid inputs,
   rollback, format bounds, higher-priority interruptions and render failure.
   Pure renderer frames and host pixel bounds do not constitute physical OLED
   readability or phone-to-device synchronization acceptance.

7. **Hardware - explicitly deferred.** Before any later authorized write,
   freshly verify the exact target, installed application, partition/boot
   selection, image length/hash and erase-sector tail; preserve a verified
   application-only restoration route. The latest OT-177 installed artifact is
   563,776 bytes with SHA-256
   `9ACDC90EEA9D0489ABABDD9B6F4E3C7AFF97162D03DDC09F8E4965D5A1D12784`;
   that historical observation must be freshly checked before writing.
   Antenna/radio execution is inapplicable here because no radio operation is
   performed. Physical OLED inspection, concealment/failure injection, both
   V1 units, cold-power/reboot persistence and current-draw impact remain open.
   The installed application remains unchanged. A separately requested phone
   stay-awake correction is recorded in the private hardware tracker; it does
   not provide firmware acceptance evidence.

## Established build command

Use the existing ESP-IDF environment export, `CCACHE_DISABLE=1` and
`IDF_COMPONENT_MANAGER=0`. For each initially absent build directory, run:

```text
python <IDF_PATH>/tools/idf.py
  -C firmware/targets/heltec_v4_bench -B <fresh-build>
  -D SDKCONFIG=<fresh-build>/sdkconfig
  -D SDKCONFIG_DEFAULTS=<target>/sdkconfig.defaults
  -D PROJECT_VER=ot178-oled-adapter-v1 -D IDF_TARGET=esp32s3 build
```

Resolve every path against the isolated publication candidate. No `flash`,
`monitor`, provisioning or device-access command is part of this build path.

## Integrated behavior and focused evidence

The existing Heltec OLED port calls the host-tested presentation owner for
non-logo frames. Failure and factory-reset surfaces remain exclusive; the
startup asset and established large ephemeral pairing page stay on their
existing paths. Raw BLE connection does not grant Ready. Ordinary frames show
region required and radio TX disabled because authenticated configuration is
not bound. The existing footer bitmap is not reverse-engineered into telemetry. Ordinary
frames therefore no longer show the compact battery/GPS/BLE footer; they show
the region-required page until real configuration and typed metrics are bound.
Pairing retains its existing footer. This intermediate adapter is not final
normal-page feature parity.
No configured name, group, region, radio activity or phone clock source is
invented. The clock source is compiled but no phone synchronization is wired.

The live Snapshot authority reports radio unavailable and zero queued actions.
The exact 38-source target has no LoRa transmitter implementation or radio
hardware driver. TX disabled describes that software boundary, not measured
RF silence or a newly accepted regulatory enforcement mechanism. BLE remains
available under its existing authority.

Focused native C++17 warnings-as-errors validation passes four mapper groups
and seven actual-port groups. The latter compiles the production OLED and
StartupDisplayOwner sources against bounded ESP-IDF stubs, captures frame bytes,
and injects draw/panel failures, negative time and rollback. It verifies startup,
exclusive rendering, pairing clear/restoration and independent black/panel-off/
Vext-off concealment. The shared time gate covers logo, ordinary and pairing
draws, latching the port unavailable after rollback. These are host observations,
not physical panel observations. Target source-admission passes 16 groups.

Compiled review rejected initial builds: the duplicate pixel buffer made the
reachable application rendering frames total 3,872 bytes against a 3,584-byte
main-task stack, before deeper helpers. The failure path added another 1,024-byte
blank buffer. The correction draws the static logo or presentation pixels
directly and moves the immutable black frame to static storage. Rejected builds
are not installation candidates. Final compiled stack and reproducibility
results follow separately below.

## Final compiled stack review

The corrected C ELF uses a 1,248-byte render frame (previously 2,272) and
32-byte concealment frame (previously 1,056). The inspected main/application/
display-owner/mapper/presentation call chain totals 2,848 of the configured
3,584 bytes. The inspected synchronous display/I2C chain totals 2,736 bytes
before deeper internals. No remaining provable overflow was found in the
inspected paths. This corrects the concrete static allocation regression; it is
not a complete worst-case stack proof. Nested driver/RTOS and formatter/VFS
paths and actual runtime stack high-watermark remain physical acceptance gates.

## Reproducible firmware result

Two initially absent, cache-disabled output trees (`ot178-oled-adapter-c` and
`ot178-oled-adapter-d`) built successfully with the preflight toolchain and
explicit version `ot178-oled-adapter-v1`. All six artifacts match byte hashes:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `opentrail_heltec_v4_bench.bin` | 567376 | `2573610786010DA3C2F658AE94F4AAB3AF16CFF116F31C8B1D35D82850085177` |
| `opentrail_heltec_v4_bench.elf` | 8208276 | `68B04421A0486AFF6020A13AEDF72E895CC21F948D02BEDAE7EF43F4F0575B60` |
| `opentrail_heltec_v4_bench.map` | 6873641 | `D312A01904AB7A49414092385F1E3B2BB24B6C73E45C639E56C0653FD0BFAC52` |
| `bootloader/bootloader.bin` | 22480 | `96E83EBE4434CD6C9049A59F396B4F8BD06C159B40259DA573BDB701C571ECA5` |
| `partition_table/partition-table.bin` | 3072 | `F3372A1F30CBDD98D6FBCF7808C85C46DCAA249105BA9DA883EF21E05EFE90A4` |
| `sdkconfig` | 106889 | `E269012D644B443E3C0705A81F26609261FFB4A3FB65CCF05D863BEE4BF9A5DE` |

Both compilation databases admit the exact 38-source application and compile
panel, mapper, presentation and clock source once each. ELF inspection retains
the mapper, renderer and presentation-owner symbols. Unused clock symbols are
removed by the linker; no live clock integration is claimed. Partition and
sdkconfig defaults are unchanged from the published baseline; no dependency
lock is introduced and component-manager resolution remains disabled.

The 567,376-byte app leaves 4,609,968 bytes in the smallest 5,177,344-byte app
partition. ELF sections: IRAM text 80,291 plus vectors 1,028; DRAM data 17,323
and BSS 6,688; flash text 371,652 and rodata 96,656; thread data 16; RTC slow 36
and reserved 24 bytes. These are compiled resource observations, not runtime
heap/stack/current-draw measurements. No image was flashed or installed.

## Final host gate and disposition

`tools/Test-Host.ps1` completed with exit zero through the established normal-user
GCC path. The complete matrix includes the 6 clock, 6 presentation, 4 target
mapper and 7 actual-port groups, target admission, security/recovery, publication
safety, Windows loader and simulator checks. Simulator native UI passed 13/13.
The early structural check was corrected to recognize the shared time-admission
helper and direct framebuffer path before this final complete run.

Software integration and reproducible build evidence are accepted. OT-178 stays
partial and every V1 milestone remains unchanged (target 25, Android 60,
weighted exact 43.75/displayed 44). No physical readability, runtime stack,
current draw, two-device, time synchronization or normal-page data authority is
accepted. Website synchronization and deployment remain owner-deferred for the
bulk update; the public percentage and demonstrated hardware capability did not
change. Next prepare the authenticated configuration/time contract, preserving
current unknown states until real sources are safely bound.
