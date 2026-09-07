# OT-178 second device and phone baseline

Date: 2026-09-07

Status: second-device application installation, exact readback and runtime return
passed. The S24 Ultra's installed V1-Test app reached authenticated Snapshot/Ready
using its existing bond, then passed name/US915 readback and clock synchronization.
Full app restart recovered protected Ready and fresh US915 readback. Physical
OLED text, cross-pair isolation and
secure radio messaging are not yet accepted.

The owner authorized second-device firmware installation, second-phone APK
installation and inter-device testing while away. This checkpoint prepares the
second pair for the first real secure message path; it does not claim that path
already exists. Website publication and cold-power disassembly remain deferred.

## Target and exact recovery preflight

The second bench device passed a unique endpoint selection independent of the
original enrolled board, followed by matching ROM identity checks. ROM facts show
ESP32-S3 revision 0.2, 16 MB flash, 2 MB embedded PSRAM and 40 MHz crystal. These
facts do not independently identify every board or radio component. The retained
Heltec V4 bench application and known partition layout were inspected offline.

A complete 5,177,344-byte factory-application partition was retained and hashed.
Its ESP32-S3 image has a valid checksum and validation hash, DIO/80 MHz/16 MB
configuration, project `opentrail_heltec_v4_bench`, version `8a2573c-dirty`, ESP-IDF
6.0.2 and secure version 0. Partition-table and bootloader regions were retained;
the factory OTA selection was all erased bytes. The readback performed no flash
or registry writes. An identity-checked reset and subsequent receive-only
increasing heartbeats established return to the old runtime before installation.

| Bound artifact | Bytes | SHA256 |
|---|---:|---|
| Complete captured factory partition | 5177344 | `33C78D2850E00AE1ED93DDE2A92C5A47A1168E31371898EC548D4246FC1DD89A` |
| Exact original application prefix for recovery | 589824 | `647CDA50A48FB0B949ED7976DD403B333CA6039D5EB58CB84E850332DF5CB2D8` |
| Candidate `ot178-phone-v1` application | 586736 | `43AC6DBC506C03FAA63AAE7A8E27195750598F06F3118BFFF1EF7AF84BC8D9F2` |

The touched range is exactly 589,824 bytes beginning at `0x10000`, within the
`0x4f0000` factory partition. Recovery before the candidate's first boot restores
the captured prefix byte for byte; it does not substitute the original board's
firmware or invent erased padding. Bytes beyond the touched span are not written.

## Firmware preflight and reused validation

The applicable [firmware-porting checklist](../firmware-porting-lessons.md) is
applied as follows:

1. **Target and layout:** verify the second board independently, the known table,
   factory OTA selection, bootloader and complete touched prefix before writing.
   Existing target pin assignments, OLED, USB, battery and GNSS interfaces are
   unchanged. No radio configuration or TX authority is enabled by installation.
2. **Build reproducibility:** reuse the exact accepted `ot178-phone-v1` artifact
   and its two matching fresh builds. No source, build option or dependency
   changed for the second board, so another duplicate build is unnecessary.
   Pinned ESP-IDF 6.0.2, toolchain and six-artifact identities remain recorded in
   [the phone-status evidence](OT-178-PHONE-STATUS-2026-09-07.md).
3. **USB and reset:** reuse esptool 5.3.1, no-stub, 115200 baud and fresh identity
   checks before operations. Every entered-device exit attempts an identity-
   checked runtime reset. Observe the runtime through a fresh receive-only handle.
4. **Concurrency and authority:** no new callback or state logic is introduced.
   Reuse the complete affected host matrix and focused Ready/redraw/expiry/reset
   regressions from the same firmware. Second-pair physical authority remains an
   independent gate after installation.
5. **Persistence and recovery:** installer writes only the application span.
   Bootloader, table, OTA selection, NVS and application state partitions are not
   directly written. Ordinary firmware boot reconciliation is permitted; no
   factory-reset request or marker creation is part of this operation. An old
   application readback alone does not prove the contents of existing NVS.
6. **Execution gate:** verify fresh prewrite bytes, exact candidate plus erased
   sector tail, and runtime return. A failed preboot write/readback attempts exact
   prefix restoration. A retry or failed gate does not count as acceptance.
7. **Skipped physical cases:** cold-power is owner-deferred because battery
   removal requires disassembly. OLED text confirmation awaits the owner. Radio
   tests and secure messages are not exercised by this application-only step.

## Second-phone APK result

The S24 Ultra, model SM-S928U with Android 16, received the same accepted isolated
V1-Test APK used on the Note20. The final successful attempt completed at
07:12:05 UTC and verified the installed bytes exactly:

- Package: `io.github.nbjelanovic.otclient.v1test`.
- APK: 11,909,082 bytes; SHA256
  `BDA9F7AE310C696199083079A449C0E31087E7B09F38212D79BEEFA3769886D7`.
- Signer SHA256:
  `bc60cc64be586444a0ce181e426e586ec74d55e764268089e3aa4e8f1dbfac06`.

The base Trail package and its installed path were preserved. No uninstall or app
storage clear occurred. Phone lock/display settings were unchanged; the two
required Bluetooth runtime permissions were granted for V1-Test. The original
phone was not mutated. Two earlier preparation attempts stopped before accepted installation;
the final exact readback, not those failed attempts, establishes this result.
Installing this APK does not itself prove bonding, protected Snapshot or Ready.

## Second-device installation and runtime result

The one-use application installer completed successfully at 07:16:07 UTC. Fresh
table, factory selection, bootloader and old-prefix comparisons passed. The
candidate and exact erased sector tail matched independent readback; the verified
589,824-byte span has SHA256
`7297971E2734C04D043E274A49B3A8043C63F4648F938EB559544CD0C816C36C`.
No write retry or recovery was needed. The identity-checked reset succeeded.
The original board was untouched and the identity registry was not modified.

Subsequent receive-only observation recorded increasing elapsed heartbeats from
25,798 to 30,828 ms and 4,688 bytes minimum free application stack. This proves
runtime return after the verified installation; it is not physical OLED reading
or authenticated second-phone acceptance.

## Second-phone protected session and configuration

The S24 Ultra's required Bluetooth permissions were confirmed. Restarting the app
resolved its stale permission state before the connection attempt. With the
existing bond and no new PIN, GATT connection, protected ProtocolInfo, MTU,
subscription, authorization and initial Snapshot all passed. Ready took 853 ms
from connection attempt, after a separate 30,025 ms discovery interval.

The device-name read returned `Device name read back.` with a blank name. Region
initially read as `Not configured`; after explicitly choosing and applying US915,
a fresh read returned `Region read back. Radio TX remains disabled.` and saved
US915. The UI also confirmed `Display clock synchronized.` These are second-pair
protected configuration observations, not physical OLED text confirmation.

The original Note20 trace remained Ready with no subsequent disconnection at this
checkpoint. Concurrent healthy sessions do not prove cross-pair access isolation.
A full S24 app-process restart then completed a separate 30,020 ms discovery
interval and reached a new authenticated Snapshot/Ready 1,142 ms after connection
attempt. The region initially showed `Not read back`, then an explicit fresh read
returned saved US915. This accepts the bounded second-pair existing-bond restart
and region readback; the original pair remained untouched.

A final receive-only second-device observation after the configuration and restart
checks recorded increasing elapsed heartbeats from 552,038 to 557,058 ms and
3,952 bytes minimum free application stack. The earlier 4,688-byte value remains
the initial post-install observation; this later value records the exercised
runtime without relabeling the earlier checkpoint.

Next diagnose the OT-163 `restart_ack_a` boundary and bind any corrected secure-
radio attempt to current per-device recovery images. Pair isolation remains a
separate acceptance gate. No new-PIN pairing, cold-power, physical OLED text,
radio transmission, secure message, field readiness or V1 completion increase is
claimed by this checkpoint.
