# OT-171 matching first-use setup labels

## Scope - 2026-09-07

The owner requested distinguishable device names during first connection when
several people are setting up nearby devices. The live scanner previously used
`Nearby compatible device 1` and subsequent scan-order numbers. The accepted
Decision 0104 setup-label model had not reached the real BLE/OLED paths.

The candidate implements [Decision 0111](../decisions/0111-match-first-use-setup-labels.md).
An unowned Heltec generates one random six-character code during BLE host sync,
before opening the existing pairing window. The actual pairing OLED renders
`Trail-XXXXXX`; the D1 complete local name carries exactly the same six ASCII
characters. The label is stable during the unowned boot and may change after
restart. The saved editable device name is separate. D0 owned advertising has
no setup name. Primary advertising uses 29 bytes; the reset-receipt scan response
and protected GATT identities remain unchanged.

Android parses the current raw advertisement, never the cached Bluetooth name.
Missing or malformed first-use names do not become generic choices. Duplicate
labels on distinct observed endpoints, or a changed/invalidated known label,
invalidate the scan and clear its endpoint bindings. The user receives a specific
rescan instruction. Selection still uses an opaque endpoint and requires the
existing secure pairing and authorization. Names grant no authority.

## Preflight and focused evidence

The mandatory [firmware preflight](../../tests/hardware/OT-171-SETUP-LABEL-PREFLIGHT-2026-09-07.md)
was recorded before firmware edits. The existing pinned ESP-IDF/toolchain,
board definition, partitions and application-only offset are retained. No
persistent schema, NVS mutation or radio transmission is added.

Focused firmware checks pass: 14 startup-display groups, 8 actual OLED-port
groups and 17 target-admission groups. These cover entropy readiness/partial
failure, no uncertain retry, immutable boot code, D1/D0 visibility, exact
advertised/display equality, all twelve rendered label glyphs, and byte-exact
restoration of the previous display after pairing concealment.

Focused Android validation passes 76 tests with no failures, errors or skips.
Independent review found and closed a stale-name case: a previously admitted D1
primary advertisement that loses its name now invalidates the scan. Response-only
records are distinguished by the existing D1 UUID policy. A fixture combines the
actual 29-byte primary advertisement, 31-byte reset response and zero padding.

The first full Android build caught an exhaustive V1-Test diagnostic mapping
missing the two new scan failures. Both now map to the existing categorical scan
failure reason, with no trace-schema change. A recorder regression covers every
runtime failure through persistence/reload/export and checks that the display
label and endpoint never enter those records. The corrected complete matrix is
the final gate; the failed first build is not acceptance evidence.

Two initially absent firmware builds pass with all six artifacts byte-identical;
373 firmware/catalog input hashes stayed unchanged during the builds. Both used
ESP-IDF 6.0.2 commit `7101770dc6db2667b3c477cc31365dd1acd6db4e`, Xtensa
`esp-15.2.0_20251204`, component-manager disabled and cache bypassed, with explicit
project version `ot171-setup-v1`.

| Artifact | Bytes | SHA256 |
| --- | ---: | --- |
| Application BIN | 587456 | `54E46380E038059A0F4C0AA280875CC8848751B93F046B9309B75F067A6083A1` |
| ELF | 8593268 | `EEBCFA25C189394D081706FEC0F45CD0FFEA833ABB61CE9D58C291DCE18629F5` |
| Map | 7002501 | `B6C81F88B37FF928DB175816D92EDB628ED0AD5E720577C67A092289E1ED3D1C` |
| Bootloader | 22480 | `96E83EBE4434CD6C9049A59F396B4F8BD06C159B40259DA573BDB701C571ECA5` |
| Partition table | 3072 | `F3372A1F30CBDD98D6FBCF7808C85C46DCAA249105BA9DA883EF21E05EFE90A4` |
| SDK configuration | 106877 | `5519CBF48461633E814CC7D1608D01BEB283D3A2D7C45829877D2EB2BEA7A71E` |

Independent image inspection validates its checksum/hash, ESP32-S3 target,
version, IDF version and DIO/80 MHz/16 MB profile. The ELF contains setup-label
state and the runtime call that binds it to the actual startup display. The
bootloader, partition table and SDK configuration also exactly match the accepted
installed firmware's build. Application offset remains `0x10000`; its rounded
erase span remains 589824 bytes. These are offline artifact results, not a flash.

The corrected complete Android matrix passes with 1,059 reported tests:
protocol 51, Debug 319, Release 318, V1-Test 371; zero failures, errors or skips.
All variant lint/build gates, including Debug AndroidTest assembly, pass. Gradle
reuses unchanged task results through its normal dependency checks.

The unsigned release audit passes, including exclusion of all fourteen V1-Test
diagnostic classes and temporary-file cleanup. Its first invocation under Windows
PowerShell 5 stopped on the expected unsigned-signature stderr; rerunning only the
audit under the proven PowerShell 7 runtime passed the exact existing assertions.
This was an invocation mismatch, not sandbox corruption or a changed signature
requirement. Release APK: 8722116 bytes, SHA256
`EA6B513EB5FA37017E53BEE1E00378C8D8DD68022CCE1286AB09BE16BA7C1383`.

Candidate V1-Test APK: 12500293 bytes, SHA256
`CC5D5FB60A58D7ECC58D78A84AA0BC3829CC2F732D0B0F33018CEBC52C7FF2F0`.
Offline apksigner/aapt inspection verifies the existing signer, package
io.github.nbjelanovic.otclient.v1test, version 1 / 1.0.0-v1test, minimum SDK 26,
target SDK 35 and V1-Test launcher. No production signing is claimed.
The previously installed APK's exact bytes were retained before its build-output
path was overwritten, preserving the current recovery reference.

The complete `tools/Test-Host.ps1` matrix passes across the original run and an
exact continuation. The restricted-account run completed the native and preceding
repository checks, then stopped at the existing CurrentUser DPAPI round-trip test
with `key_store_unavailable`. The unchanged test passes under the normal account;
the exact remaining script suffix was run there, including signature vectors,
Windows Loader and Windows Simulator validation. No tests or assertions were
removed to accommodate the account limitation. Simulator UI acceptance is 13/13;
it is separate from physical Heltec/phone acceptance.

Final commands use the existing host matrix, Gradle protocol plus Debug/Release/
V1-Test test/lint/build tasks, `Test-AndroidUnsignedReleaseArtifact.ps1` under
PowerShell 7, and two initially absent ESP-IDF build directories with explicit
`PROJECT_VER=ot171-setup-v1`. Canonical progress/scope and publication-safety checks
are repeated after the records are updated. Source publication is a separate
checkpoint; it does not install these artifacts.

## Physical acceptance gate

Before the second-only reset, the Heltec ran the verified clock correction `ot171-clock-v1`,
587,632 bytes, SHA-256
`2FF001B30BB22A2E91F82D54D94BED20B8F1BEE406756A8AA760B0BC3B29A57E`,
which includes the setup-label implementation. It supersedes the earlier installed
587,456-byte `ot171-setup-v1` artifact recorded above. Its S24 V1-Test APK remains
12,500,293 bytes, SHA-256
`CC5D5FB60A58D7ECC58D78A84AA0BC3829CC2F732D0B0F33018CEBC52C7FF2F0`.
Exact application readback, healthy runtime, retained-owner Ready and protected
US915 readback passed. The owner confirmed matching time and two subsequent minute
changes without Sync; see [clock evidence](OT-171-CLOCK-REFRESH-2026-09-07.md).
The original pair remains unchanged and still needs the clock correction after
serving as the paired control.

After clock acceptance, the explicitly approved second-only protected factory
reset resumed. The app displayed Factory reset verified and Remove old Android
pairing. Owner-directed removal of old S24 Trail test bonds is pending. The approved
reset erased US915; restore it after fresh pairing. No fresh pairing, OLED/list
label match or complete reset-domain cleanup acceptance is claimed yet.

Continue from Android bond removal to the OLED/scan label match, exact selection,
normal PIN pairing, protected Ready, automatic name/time readback, D0 concealment
and restart/reconnect. Restore US915 through protected settings and verify readback.
Firmware rollback cannot restore user data erased by a factory reset.

Two simultaneously unowned physical devices, physical duplicate-label behavior,
full reset-domain erasure acceptance, secure radio messaging, field tests and
signed release remain open. Website updates and cold-power disassembly remain
owner-deferred; no V1 score increase is claimed.

## Post-timeout setup label correction and preflight

After the verified reset, the owner observed generic DEVICE when the 60-second
pairing window expired. The actual composed OLED test reproduced that output
before correction. The proposed correction retains the valid boot `Trail-XXXXXX`
label as the normal unowned status fallback when no durable name exists. Admission
uses only exact lock-protected `closed_unowned` owner status; owned transition
removes the fallback, and a saved name wins. It changes neither D1 advertising nor
the pairing deadline, PIN, persistent identity or NVS schema.

Focused validation passes fifteen startup groups, eleven actual OLED groups and
seventeen target-admission groups. Independent final review passes. The complete
affected gate passes thirteen native suites and seventeen structural sets. Two
initially absent builds match all six artifacts with 373 firmware input hashes
unchanged. Image inspection and exact application installation pass. Owner-observed
post-timeout label acceptance remains pending at this checkpoint. This is not a fresh-pair
acceptance result.

| Preflight gate | Applied boundary / remaining requirement |
| --- | --- |
| Pre-install artifact and recovery | The installation used `ot171-clock-v1`, 587,632 bytes, SHA-256 `2FF001B30BB22A2E91F82D54D94BED20B8F1BEE406756A8AA760B0BC3B29A57E`, as its exact recovery baseline. The current installed 984E image is recorded below. Original 43AC/CCC4 control and S24 CC5D APK are unchanged. |
| Target/toolchain | Reuse Heltec V4-family ESP32-S3, pinned IDF 6.0.2 commit `7101770dc6db2667b3c477cc31365dd1acd6db4e`, Xtensa `esp-15.2.0_20251204`, DIO 80 MHz / 16 MB, unchanged GPIO/USB, bootloader/table/SDK configuration. |
| Presentation authority | Require exact closed-unowned admission, composed timeout/label restoration, clock refresh and owned concealment regressions. Preserve overlay priority and dirty-minute invalidation. |
| Build/artifact gate | Affected matrix and two initially absent `ot171-label-v1` builds pass; 373 source hashes stayed unchanged and six artifacts match. Image inspection and unchanged bootloader/table/SDK configuration comparison pass. |
| Installation gate | Fresh exact second-only selector excluding the original, ROM identity/table/OTA/current full-prefix checks, final artifact/current 2FF recovery binding, application-only offset `0x10000` and calculated aligned span. No hardware write before these pass. |
| Physical gate | Exact postwrite readback and healthy unowned runtime passed in the installation recorded below. Owner-confirmed label after timeout, fresh OLED/list match, normal PIN/Ready and restored US915 readback remain open. |
| Unchanged/deferred work | No protocol, radio, GNSS, battery, power, entropy or encryption changes; reuse accepted evidence for those unchanged boundaries. No radio transmission. Cold-power/disassembly and website updates remain deferred. |

## OT-168 Android bond cleanup follow-up

Duplicate NimBLE names in Android settings made manual bond cleanup ambiguous.
The current app clears its exact-peer reference before directing manual cleanup.
The owner confirmed all S24 NimBLE entries are Trail test leftovers and is removing
them manually; this owner-specific confirmation is not a general bulk-removal rule.

A follow-up should preserve the exact peer across reset cleanup. The future API 36
path should evaluate supported `CompanionDeviceManager.removeBond` with a
pre-established companion association; older APIs need guided exact-peer cleanup.
No automatic Android bond removal is implemented or accepted here. Fresh pairing,
complete reset-domain cleanup and two-pair acceptance remain open.

## Post-timeout label artifact gate

The final `ot171-label-v1` image retains the accepted clock correction. Independent
esptool 5.3.1 inspection verifies its checksum/hash, ESP32-S3 target, IDF 6.0.2
and DIO 80 MHz / 16 MB profile. Application offset remains `0x10000`; the aligned
erase span remains 589,824 bytes. Bootloader, table and SDK configuration match
the pre-install clock build exactly.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| opentrail_heltec_v4_bench.bin | 587968 | `984E241DC72FAD956D34B5E83F04FD307DCB95E0F18C629AC46D21DA9D96D87B` |
| opentrail_heltec_v4_bench.elf | 8602624 | `790C4539D5241A3F34089A95C0A94F22A0DDCFB57DDFBF595575D8BDC31F8F39` |
| opentrail_heltec_v4_bench.map | 7004598 | `254535F27348F3AC3CFC5DED0C4B187C66FD1EF00C978E2A7050672EB9372E0C` |
| bootloader/bootloader.bin | 22480 | `96E83EBE4434CD6C9049A59F396B4F8BD06C159B40259DA573BDB701C571ECA5` |
| partition_table/partition-table.bin | 3072 | `F3372A1F30CBDD98D6FBCF7808C85C46DCAA249105BA9DA883EF21E05EFE90A4` |
| sdkconfig | 106877 | `5519CBF48461633E814CC7D1608D01BEB283D3A2D7C45829877D2EB2BEA7A71E` |

## Current second-device installation

The second Heltec now runs `ot171-label-v1`, 587,968 bytes, SHA-256
`984E241DC72FAD956D34B5E83F04FD307DCB95E0F18C629AC46D21DA9D96D87B`.
The reviewed one-use installer verified current 2FF clock firmware and exact
bootloader/table/factory-selection state before writing. It read back the new
image and erased tail exactly, reported no transport write retry, and restarted
successfully. No NVS write, registry update or additional factory reset occurred.
The device remains unowned from the earlier approved, app-verified reset.
Receive-only runtime observation saw advancing elapsed times 35,707 and 40,707 ms
and minimum free application stack 4,672 bytes. No serial data was written.

The S24 APK remains CC5D; original Heltec/Note20 remains 43AC/CCC4. The original
still needs the clock correction after serving as control. The earlier owner-
confirmed clock result was on 2FF before reset; new fresh-pair clock synchronization
and US915 restoration remain to be observed. Physical post-timeout label and
phone-list matching are not inferred from these build/readback/heartbeat results.
