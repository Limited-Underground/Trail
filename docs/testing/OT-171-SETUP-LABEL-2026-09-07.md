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

No hardware was flashed or reset for this candidate yet. Both retained Heltecs
are owned, so they do not enter first-use D1 discovery. Current installed firmware
remains `ot178-phone-v1`; both V1-Test apps remain the previously accepted automatic
clock/name build. First-use display/list matching is not physically accepted.

After artifact validation, the proposed test preserves the original Heltec and
Note20 as a paired control. It needs explicit approval to factory-reset only the
second Heltec, erasing that device's saved configuration and ownership. Test the
current OLED/scan label match, exact selection, normal PIN pairing, protected
Ready, automatic name/time readback, D0 concealment and restart/reconnect. Restore
US915 through protected settings and verify its readback. Firmware rollback cannot
restore user data erased by a factory reset.

Two simultaneously unowned physical devices, physical duplicate-label behavior,
full reset-domain erasure acceptance, secure radio messaging, field tests and
signed release remain outside this candidate's computer validation. Website
updates and cold-power disassembly remain owner-deferred; no V1 score increase
is claimed for the setup-label implementation.
