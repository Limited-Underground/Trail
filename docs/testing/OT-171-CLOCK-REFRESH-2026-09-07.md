# OT-171 installed checkpoint and clock refresh regression â€” 2026-09-07

Status: corrected second-pair firmware installation and physical clock refresh
accepted. The approved protected second-only factory reset is app-verified;
removal of the old S24 bond and fresh pairing remain pending.

## Clock acceptance installation

The second Heltec now runs `ot171-clock-v1`, 587,632 bytes, SHA-256
`2FF001B30BB22A2E91F82D54D94BED20B8F1BEE406756A8AA760B0BC3B29A57E`.
The earlier `ot171-setup-v1` image, 587,456 bytes, SHA-256
`54E46380E038059A0F4C0AA280875CC8848751B93F046B9309B75F067A6083A1`,
was the exact prewrite/recovery artifact for this successor.
The one-use application-only installation verified the prior application,
bootloader, partition table and OTA state, wrote at `0x10000` within the unchanged
`0x4f0000` application partition, and read back the exact new image and erased
tail. The aligned write span was 589,824 bytes. No recovery, NVS write or device
registry update was needed. The original Heltec was excluded throughout.
Post-install runtime observations advanced from 30,907 to 35,927 ms with minimum
free application stack 3,920 bytes; observation wrote no serial data.

The S24 Ultra received V1-Test APK 12,500,293 bytes, SHA-256
`CC5D5FB60A58D7ECC58D78A84AA0BC3829CC2F732D0B0F33018CEBC52C7FF2F0`.
The exact prior APK was checked before the update and installed bytes were read
back afterward. Package `io.github.nbjelanovic.otclient.v1test` remains version
code 1 / `1.0.0-v1test`, minimum SDK 26 / target 35, with the unchanged debug
signer. The base Trail app, its data and phone settings were preserved.

The unchanged original Heltec/Note20 control retains `ot178-phone-v1`, 586,736
bytes, SHA-256 `43AC6DBC506C03FAA63AAE7A8E27195750598F06F3118BFFF1EF7AF84BC8D9F2`,
and APK 12,495,724 bytes, SHA-256
`CCC4F1EBC544F5678E623E6AFD5346553EE259F39A3CDF40319E5FBAB87B1A93`.
No original-pair installation or reset was performed for this increment; its clock
correction remains pending after the paired-control gate.

Retained-owner reconnect, protected US915 readback and automatic clock
synchronization acknowledgement were observed on the second pair. The owner's
report that it connected without a PIN is consistent with the retained bond;
it does not prove first-use discovery or fresh pairing. US915 remains a saved
selection, with no radio transmission or secure messaging result added.

## Clock regression and next gate

The owner then reported that the second OLED was about two minutes behind its
S24 and that the original OLED was behind by less. These are observed display
differences, not a measured oscillator-drift diagnosis. A clock synchronization
acknowledgement proves neither displayed time nor continuing minute refresh.

The regression was reproduced through the actual display-owner/OLED composition:
unchanged frame, phone and footer inputs suppressed rendering while clock, name
or region inputs arrived separately. Before the fix, the 10:00 to 10:01 test kept
the old rendered pixels and draw count. The narrow correction invalidates that
suppression when the OLED port has changed presentation inputs; Android is unchanged.

All ten actual OLED test groups pass, including 599 same-minute ticks with no
I/O, minute/name/region/format/validity changes, overlay restoration and failed
redraw containment. All thirteen affected native groups and seventeen structural
test sets pass. Two clean firmware builds pass with all six artifacts identical and 373 firmware
input hashes unchanged. The owner confirmed that time matches the S24 and keeps advancing through two
further minute changes without pressing Sync.

The [hardware preflight](../../tests/hardware/OT-171-CLOCK-REFRESH-PREFLIGHT-2026-09-07.md)
passed the applicable host, artifact, identity and readback gates. The fresh
second-only installer completed with no retry or recovery. Retained-owner Ready,
automatic synchronization and protected US915 readback passed before the owner
accepted the advancing display. This adds observed refresh acceptance beyond a
synchronization acknowledgement, not a long-duration clock accuracy measurement.

The approved second-only protected factory reset then resumed. The app displayed
Factory reset verified and Remove old Android pairing. The old S24 bond removal
is pending; no fresh pairing has occurred. Continue the
[first-use setup-label gate](OT-171-SETUP-LABEL-2026-09-07.md) from that point.

The prior setup-label computer validation remains recorded separately: full host
matrix, 1,059 Android tests, builds/lint and artifact audit, six-artifact firmware
reproducibility, and successful source CI run 34129302309. Those results do not
validate the newly reported clock behavior. V1 Companion remains exact 45.50%
(displayed 46%); website updates and cold-power disassembly remain deferred.

## Corrected firmware artifact gate

Version `ot171-clock-v1` was built twice from initially absent directories with
the same pinned toolchain recorded in the preflight. Independent esptool 5.3.1
inspection validates the image checksum/hash, ESP32-S3 target, version, IDF 6.0.2
and DIO 80 MHz / 16 MB profile. Bootloader, partition table and SDK configuration
are byte-identical to the installed setup-label build. Application offset remains
`0x10000`, and its rounded erase span remains 589,824 bytes.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| opentrail_heltec_v4_bench.bin | 587632 | `2FF001B30BB22A2E91F82D54D94BED20B8F1BEE406756A8AA760B0BC3B29A57E` |
| opentrail_heltec_v4_bench.elf | 8597744 | `516CA301A2689F875CC87467EE743085B62099E46F8050C432FDA0E8FCD5AC73` |
| opentrail_heltec_v4_bench.map | 7003241 | `C81CDB8E6C769B451714802606A64ABEC1BA6F207FC7B53F0CF1F3EBF9380A73` |
| bootloader/bootloader.bin | 22480 | `96E83EBE4434CD6C9049A59F396B4F8BD06C159B40259DA573BDB701C571ECA5` |
| partition_table/partition-table.bin | 3072 | `F3372A1F30CBDD98D6FBCF7808C85C46DCAA249105BA9DA883EF21E05EFE90A4` |
| sdkconfig | 106877 | `5519CBF48461633E814CC7D1608D01BEB283D3A2D7C45829877D2EB2BEA7A71E` |

The final affected host gate reuses the exact definitions and compile/run loop
from `tools/Test-Host.ps1`: thirteen native suites and all seventeen structural
preflight sets. Android and Windows code are unchanged; their prior accepted
matrices are reused. Scope-admission and publication-safety checks pass.

## Subsequent first-use gate

Clock acceptance above remains valid. Following the app-verified reset, the owner
observed generic DEVICE after pairing-window timeout. A separate unowned label
fallback correction is now under validation; its
[preflight and reset cleanup follow-up](OT-171-SETUP-LABEL-2026-09-07.md#post-timeout-setup-label-correction-and-preflight)
record the remaining gates. The second is now unowned; reset erased US915. The
owner is manually removing confirmed S24 Trail test bonds. No fresh pairing or
restored US915 readback after reset is claimed.

The subsequent installed `ot171-label-v1` firmware retains this clock correction
and adds the unowned status label after pairing timeout. See the
[current installation and remaining first-use gates](OT-171-SETUP-LABEL-2026-09-07.md#current-second-device-installation).
