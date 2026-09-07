# Protected region selection integration

Status: single-pair region installation, save/readback and restart acceptance passed;
the separate OLED phone-status mapping remains open.

Decision 0110 allocates profile 0.3 and OTRC v1. All twelve shared catalog selections are configuration
intent only; radio transmission stays disabled. The owner requested multiple
region tests and authorized finishing the retained setup on US915. Website and cold-power remain
owner-deferred. The verified region firmware and matching APK are now installed.

## Target preflight

1. Reuse accepted OT-DEV-001 Heltec V4.2 HTIT-WB32LAF, ESP32-S3R2, 16 MB flash /
   2 MB PSRAM, existing pins/display/input/console and partition layout. Region is
   currently unconfigured; no radio driver/channel/PHY changes are in scope.
2. Reuse pinned ESP-IDF 6.0.2 commit 7101770dc6db2667b3c477cc31365dd1acd6db4e
   and esp-15.2.0_20251204 toolchain. Require two initially absent build trees,
   explicit version, offline/no-component-manager/no-ccache configuration and
   matching BIN/ELF/map/bootloader/partition/sdkconfig. No managed dependency lock
   applies because no external managed dependency is enabled.
3. Preserve the proven no-stub ROM identity/write/readback/reset path and bounded
   receive-only runtime observation. Cold-power is skipped by owner direction;
   warm restart and fresh name/region readback remain required.
4. Reuse the fixed request/result lane and NimBLE host-event indication delivery.
   No NVS work in callbacks. Test shared base/name/time/region sequencing, queue
   age, lifecycle loss, duplicate exchanges and negotiated-version rejection.
5. Add only ot_region_v1/record_v1. Require exact revision CAS/fresh readback,
   post-mutation uncertainty and verified whole-namespace reset cleanup. Existing
   owner/name records remain unchanged. The prior image is restoration-compatible
   only before the first committed region selection.
6. Focused codecs/owner/dispatcher/NVS and Android binder/GATT tests precede the
   final affected matrices and reproducible linked-target builds. Prior name/time
   tests do not establish new region acceptance.
7. Hardware execution remains gated on final artifacts, explicit region choice,
   exact fresh device/installed-image/partition/OTA checks and a bound recovery
   manifest. Current installed app is 582576 bytes, SHA-256
   40D0719FFEA879D744CD32CDC7302D8C3D8D61B99CDD22AAE39FA1B296D5FF32.
   Preserve bootloader, partitions, OTA selection and user data. No radio test is
   applicable: this task does not transmit or configure a physical RF driver.

## Baseline correction

The final prior documentation CI run 34047052979 failed a stale governance test
that still asserted target 25/Android 60. Its current expectations were corrected
to accepted target 30/Android 65 and exact 45.50/display 46. Historical scope-plan
baseline, weights, two-pair zero credit and release blockers remain unchanged.
Focused scope 16, Android admission 23 and raw-byte 291 checks pass. This is a test
expectation correction, not a regression in the installed hardware behavior.

## Software validation - 2026-09-07

The C++ region codec/owner passes six focused groups and all 843 shared profile
0.3 vectors. The unchanged profile 0.2 corpus passes all 431 cases. The composed
configuration dispatcher passes ten groups, including all twelve selections,
version mismatch, the shared operation slot, replay, uncertainty, stale authority
and deadlines. The actual NVS adapter passes malformed-record, uncertain-write
and fresh-handle tests. Target OLED mapping passes six groups, the renderer seven,
and target structural checks seventeen; factory-reset admission also passes.

Android passes 998 tests: protocol 51, debug 299, release 298 and V1-Test 350,
with zero failures, errors or skips. Review corrected the panel's unknown region
label to `Not read back`; `Not configured` requires a confirmed absent readback.
The final UI-only change passed all three lint variants, all four affected APK
builds and the unsigned-release artifact audit. The earlier protocol/state test
results remain applicable because those sources did not change.

The final V1-Test APK is 11,909,082 bytes, SHA-256
`BDA9F7AE310C696199083079A449C0E31087E7B09F38212D79BEEFA3769886D7`.
Its package/version and signing certificate match the established upgrade identity.
The final unsigned release artifact is 8,705,732 bytes, SHA-256
`3470F9A91299DFC6B7E0C8E16CA9E76D881DDFB5B3ED30E7D65258D4C5A16485`;
all fourteen diagnostic exclusions pass. These are build/audit results, not a
signed public release or installation result.

Two initially absent firmware build directories passed with explicit version
`ot170171-region-v1`. Application BIN, ELF, map, bootloader, partition table and
sdkconfig match byte for byte; all 372 recorded firmware/catalog input hashes
remain unchanged. The linked map includes the new region storage and owner.
The ESP32-S3 image validates as 16 MB flash / DIO / 80 MHz with ESP-IDF 6.0.2.

The application is 586,224 bytes, SHA-256
`7293D1ADDDD2C59022EB12B51E8F50FF15F7B6E3B7A0ABC61EC2DB4ABF29CEE1`.
Its ELF is 8,570,804 bytes, SHA-256
`E1681CD8F37B86CA8EA2C0162C174BBC6D3DB5BE5297F81E6CD08DF98700333A`.
Bootloader, partition table and sdkconfig also retain their accepted prior hashes.
The private installation manifest binds the current name-aware restoration image,
application offset 0x10000 and 589,824-byte sector-aligned application span.
The exact artifact/span/tail preflight self-test passes without device access.

The complete host matrix passes in reviewed segments. Its initial run stopped at
the Android source scan because one new region notice contained a Windows-encoded
0x85 ellipsis. Replacing that byte with ASCII dots repaired the exact source;
strict UTF-8 decoding now passes every Kotlin file. The full Android matrix,
lint/builds and release audit were rerun successfully against this correction;
the earlier APK was superseded without installation. A second stale governance
expectation in the future-concepts test was also corrected from historical
43.75/display 44 and Android 60 to the already accepted 45.50/display 46 and
Android 65. Historical records and unmeasured later release tracks are unchanged.
The exact remaining canonical host suffix then passed, including publication
safety, signature-vector verification, Windows loader checks and simulator UI
13/13. The firmware sources and matching builds were unaffected by both fixes.

## Physical acceptance - 2026-09-07

OT-DEV-001 remains the accepted Heltec V4.2 HTIT-WB32LAF, ESP32-S3R2,
16 MB flash / 2 MB PSRAM, connected to the retained SM-N986U Note20 on Android 13.
USB and battery remained connected. The owner-authorized final choice is US915.
No RF driver configuration or transmission was performed.

The explicit private identity registry matched the board before installation.
Installed partition table, blank factory OTA-selection sector and complete old
application/tail matched the bound baseline. The installer wrote only the
589,824-byte application span at 0x10000, then independently read back the exact
new image and erased tail. No retry or recovery write was needed. Bootloader,
partition table, OTA selection and user-data domains were not written. The current
region-aware image is retained for compatible recovery after region data exists.

Initial receive-only runtime observation passed with increasing heartbeats and
4,688 bytes minimum free application stack. The APK replacement used `install -r`,
retained app data and passed exact installed-byte readback. The saved owner reached
authenticated Ready in 1,538 ms from connection attempt; approximately 30-second
initial discovery is separate. Fresh name readback matched the retained value.
The initial region READ confirmed absence, following the correctly unconfirmed
`Not read back` UI state before that READ.

All twelve selections passed real UI WRITE/APPLIED and a separate fresh READ:
US915, EU868, AU915, EU433, CN470, AS923-1, AS923-2, AS923-3, AS923-4, KR920,
IN865 and RU864. A thirteenth write deliberately returned the selection to US915,
again followed by fresh readback. Every write had a fresh preceding READ. The
retained name stayed visible, and the UI explicitly retained TX-disabled status.
Clock synchronization succeeded after the matrix.

A full app-process restart preserved data. The existing visible service-start
flow recovered Ready in 1,720 ms from connection attempt, followed by fresh US915
and name readback. This is not zero-tap normal-launch acceptance. An identity-checked
warm board restart then performed zero flash writes and removed no power. It
recovered the existing phone session automatically in 2,565 ms from reconnect
attempt; fresh device reads again returned US915 and the retained name. Clock
synchronization succeeded again. The final receive-only observation had increasing
heartbeats and 3,440 bytes minimum free application stack.

The owner confirmed US915, TX OFF, the retained name and current time on the
physical OLED. Supplied photos support the region/name/time observation. The
owner's subsequent current observation was PHONE UNKNOWN while the protected app
session and readbacks worked. Source review traced this to the preexisting mapper:
raw BLE-connected maps to unknown and has no authenticated-Ready input. Configuring
a region exposed the previously hidden phone row. An earlier photo's DISCONNECTED
label cannot establish the relative timing of connection transitions.

Next, under OT-178, supply exact current phone authority to the presentation and
ensure authority-only changes cause redraw despite legacy frame/footer deduplication.
Test Ready, disconnect/reconnect and authority loss through the actual adapter,
preserving pairing/reset priority. This is a separately recorded display gap, not
acceptance of complete normal-page telemetry. Then connect the verified settings
to the unfinished guided onboarding flow under OT-171.

V1 remains exact 45.50 / displayed 46. This extends the already credited partial
settings pipeline; it does not close full target, onboarding or two-pair gates.
Cold-power, destructive reset/erasure, RF operation, multi-pair, field and release
acceptance remain open. Website synchronization/deployment stays owner-deferred.
The Note20 awake/unlocked preference was preserved without settings changes.
