# Trail hardware integration and troubleshooting runbook

Use this reference to reproduce established engineering methods, not to authorize
device operations. Current tasks and grants select the exact devices, artifacts
and permitted effects. Never reuse consumed grants or a historical COM number.
This is not a release qualification or a replacement for the
[porting preflight](firmware-porting-lessons.md), [inventory](../hardware/INVENTORY.md)
or [current handoff](PROJECT_STATUS.md).

For Trail hardware integration or troubleshooting tasks, use the relevant sections
as the starting reference and reuse unchanged evidence. Unrelated work does not
require rereading this runbook.

**Evidence labels:** **Proven** means physically observed on the stated setup;
**Host-tested** means code/build/synthetic validation; **Proposed** means not
executed; **Requires Physical Validation** means the relevant device result is
still missing. None means universal compatibility or production acceptance.

## Find the right procedure

| Need | Existing authority |
|---|---|
| Windows COM access denied in Codex | [AppContainer limitation and authorized hardware-access route](#windows-serial-access-from-codex) |
| Hardware libraries and porting references | [Existing dependencies, upstream references and adaptation boundaries](#established-libraries-and-reference-implementations) |
| Firmware target differences and known integration mistakes | [Porting lessons](firmware-porting-lessons.md) |
| Hardware roles and board provenance | [Inventory](../hardware/INVENTORY.md) |
| Heltec application and radio console | [Bench target](../firmware/targets/heltec_v4_bench/README.md), [radio diagnostic target](../firmware/targets/heltec_v4_radio_diag/README.md) |
| Saved-owner connection lifecycle | [Returning-owner mapping](testing/ANDROID_RETURNING_OWNER_RECONNECT_MAPPING.md) |
| Historical milestones | [Progress log](PROGRESS_LOG.md); retain its chronology rather than copying it here |
| Operator documentation/release reconciliation | [OT-0089f](https://github.com/orgs/Limited-Underground/projects/1?pane=issue&itemId=270977521) |
| Frozen V1 comprehensive physical campaign | [OT-0089a](https://github.com/orgs/Limited-Underground/projects/8?pane=issue&itemId=286119770) |

## Windows serial access from Codex

**Proven on the retained Windows bench, 2026-10-30:** COM10 enumerated correctly,
but the Codex AppContainer denied `QueryDosDeviceW` with Windows error 5
(`Access is denied`); the integrated writer stopped at `serial_open` with a
normalized `SerialException`. The identical namespace query outside AppContainer
succeeded under a non-admin token. One exclusive COM10 open/close then passed in
4.441 ms, with Windows error 0 and no serial I/O, configuration or control-line
commands. This proves the recorded execution-context limitation and successful
access at that time; it does not establish firmware health or historical port
ownership. The failed writer's original Windows exception text was not retained.
Reuse these results; do not repeat the connection test merely to rediscover them.

### Authorized hardware-access route

For authorized serial operations on this bench, launch the exact currently
approved guarded parent/operator through `exec_command` with the tool's
outside-sandbox approval mechanism:

```json
{
  "cmd": "<exact command from the current approved physical procedure>",
  "workdir": "<approved Trail checkout>",
  "sandbox_permissions": "require_escalated",
  "justification": "Run the owner-authorized <operation> on <bound device/COM> within <grant limits> outside AppContainer."
}
```

Wait for tool approval before dispatch. The successful test used a non-admin
Windows token; do not change global sandbox settings, Windows ACLs, drivers or
firmware to address this known restriction. Keep the existing source/runtime
leases, artifact pins, target binding and guarded operator intact. Tool approval
does not grant ROM entry, reset, readback, programming or RF authority: each
operation still needs its applicable physical grant. An attempt consumed in the
wrong context stays consumed; changing context does not authorize a retry.

PnP enumeration alone does not prove access. Error 5 or `SerialException` alone
also does not prove a device fault or another process's ownership. Classify the
execution context first. If the approved hardware-access route fails, retain the
complete exception, Windows error code, operation phase and close result, then
investigate the actual endpoint, owner or configuration without automatic retries.
A rejected outside-sandbox request stops the dependent operation; report the
stated reason rather than substituting an unapproved route.

Existing chats must explicitly reread this section and the updated `AGENTS.md`
before their next hardware action. Other worktrees do not automatically inherit
these edits; follow the current authoritative checkout instructions.

## Heltec ESP32-S3 workflow

### 1. Identify before opening a port — Proven method

Enumerate Windows USB/PnP and COM state, resolve the privately registered physical
role and board revision, and establish exclusive port ownership. Re-enumerate at
an authorized operation boundary when device/port association may have changed.
Public labels, matching firmware hashes and a COM number alone are not identity.
Keep USB serials, MAC addresses and raw device inventories private.

Read-only host discovery examples (not executed by writing this document):

```powershell
Get-PnpDevice -PresentOnly -Class Ports | Select-Object Status,FriendlyName,InstanceId
[System.IO.Ports.SerialPort]::GetPortNames()
```

The first command exposes identifiers locally: do not paste its raw output into
public reports. A listed port proves enumeration, not firmware or radio health.

### 2. Connect to ROM with the admitted operator — Proven, exact profile only

The historical successful retained-pair procedure used esptool 5.3.1, 115200 baud,
no stub, scoped ESP32-S3 native USB handling and a reviewed entry/return sequence.
Use its frozen operator, not an improvised generic esptool command. ROM connection
can reset or alter volatile state and is not a passive host inventory check.
An entry counter is an attempted entry, not a successful SYNC or target admission.

The current repository's [ROM adapter](../tools/enrollment_candidate_rom_adapter.py)
is an implementation reference, not permission to substitute it for a separately
frozen trial successor. September's [adapter preparation](testing/OT-0238c-USB-ADAPTER-2026-09-30.md)
was host-only; later successful operators retain their own exact source pins.

### 3. Bind image, partition and touched erase span — Proven

Check MCU/board compatibility, image descriptor, partition table, selected boot
slot, image length/hash, flash geometry and the restoration image for that role.
Read the actual required bytes; do not infer the running slot solely from a
factory capture. Hash a local candidate with `Get-FileHash -Algorithm SHA256`.
Preserve recoverable originals and verify newly written backup data. Reuse valid
existing captures rather than making another backup solely because identical
bytes are being used; rebind the actual endpoint and operation prerequisites.

Application-only means the complete sector-rounded erase/write range is inside
the authorized application range. It does not permit bootloader, partition-table,
OTA-selection, NVS or security-state writes. Never restore a larger source file
just because its prefix supplies the intended restoration bytes.

**Historical examples, not reusable grants:**

- [September 6](../tests/hardware/OT-177-RECONNECT-ACCEPTANCE-2026-09-06.md):
  application at `0x10000`, sector span 565,248 bytes; verify the candidate and
  its correct erased tail. The smaller replacement intentionally lengthened
  the FF tail; old prefix hashes were not valid replacement-image checks.
- October 30 diagnostic trial: padded candidate 217,088 bytes (`0x35000`),
  sole touched range `[0x10000,0x45000)`. Restoration wrote only that original
  prefix, then verified the full 587,968-byte original application. Writing the
  whole 587,968-byte capture would round its erase span to 589,824 bytes and
  exceed the admitted prefix. These values apply only to that frozen trial.

### 4. Install, verify, then return — Proven on retained Heltecs

Mutate one identified device at a time. Complete the authorized uncompressed
application transfer, remain in ROM, independently read the exact range and
compare bytes/SHA-256 before the guarded normal return. Do not send `FLASH_END`
on this no-stub ESP32-S3 profile. A successful write acknowledgement is not
readback; a successful reset command is not application boot.

On mismatch or ambiguous cleanup ownership, preserve the first failure and stop
further device effects. Do not add retries, resets, a full erase or restoration
under a consumed grant. The approved failure/recovery branch controls what follows.

### 5. Observe diagnostic runtime and USB health — Proven, bounded

Observe the actual console: ESP32-S3 native USB Serial/JTAG, nominal 115200,
not an assumed external UART0 or USB-OTG console. Acquire observation early enough
for startup, or use an explicitly authorized fresh challenge/session health
exchange. BOOT text is useful context; a matching run/session, expected profile,
RX readiness, idle permits, zero counters, matching session end and closed handle
prove much more than USB enumeration. The radio diagnostic does not promise OLED
output. Opening a serial port can change line state or clear queued input; use the
maintained owner of that port, not a competing terminal.

### 6. Test RF, restore originals, verify final health — Proven, bounded

October 30's accepted diagnostic trial installed and verified A, then B, passed
fresh non-RF health checks, and observed A-to-B DATA/B-to-A ACK and the reverse.
Four attempts and four matching valid receptions passed the frozen counter checks:
each node attempted=2, sent=2, rx_valid=2; tx_fail, rx_invalid, rx_read_error and
rx_restart_fail all zero. Profile: 915 MHz, BW125 kHz, SF7, CR4/5, sync `0x12`,
2 dBm, preamble 8, CRC, explicit header, LDRO off; owner confirmed US location and
antennas. This was unsecured diagnostic DATA/ACK, not authenticated Packet-v1 or
phone-to-phone delivery. Distance and exact RSSI/SNR/RTT were not retained; do not
invent metrics from a passed bound or repeat RF just to fill a report.

Restore B then A under that trial's recovery plan, read back each full original
application and verify the exact original hash, normal return, handle closure,
current USB association and ordinary application operation. October 30 passed
both full-original readbacks and owner-confirmed clock/status returns. Flash
equality does not establish full mutable-state equality or radio calibration.

## Defect-to-correction reference

| Symptom | Confirmed cause or limit | Correction | Verification | Reuse |
|---|---|---|---|---|
| Port enumerates but serial access is denied in Codex | Recorded Windows AppContainer denies device-namespace access. | Use the [approved hardware-access route](#windows-serial-access-from-codex); retain physical grants and frozen operators. | Same-host outside-AppContainer namespace query and single exclusive open/close passed. | Reuse the proven context correction before investigating firmware or replacing drivers. |
| Connection refuses after SYNC | Pinned esptool S3 `_post_connect()` attempts watchdog-register writes outside the fixed-range writer's allowlist. The original failed attempt lost its exception, so its unique physical cause remains unknown. | Reuse the successful factory reader's scoped post-connect behavior; retain first fixed failure diagnostics; do not broaden register permissions. | October 30 pinned-SDK replay reproduced the incompatibility; later physical install/readback succeeded. | Compare actual inherited SDK hooks before changing timing or retrying hardware. |
| First post-program ROM read fails, including restoration | `prefix_program()` called `flash_finish(reboot=False)`; `FLASH_END` exits the ROM loader even without the intended explicit return. Historical raw responses were not retained. | Deny `FLASH_END`; finish acknowledged blocks, read/compare while still in ROM, then separately return. | 276 affected host cases passed; corrected physical installation and full-original restoration passed. | Trace command ordering; do not infer corrupt flash from an unavailable loader. |
| Prefix/hash or erase range does not match intended operation | Candidate length, erased tail and sector-rounded span are different quantities; the larger original is not an authorized prefix write. | Bind offset, image length, padded length, erase span and role-specific restore slice together. | September 6 tail readback and October 30 prefix-only/full-original verification. | Calculate erase geometry before dispatch; fail closed on any out-of-span write. |
| Zero diagnostic startup bytes | Capture opened after normal return; Windows serial open clears queued RX and early USB output can be lost. Zero bytes cannot distinguish missed logs from initialization failure. | Use a fresh bounded nonce/run/profile/ready-idle health exchange before acceptance; no speculative console reconfiguration. | October 30 successor health checks passed physically on both nodes. | Observe the decisive lifecycle event, not only historical startup text. |
| One-use radio session unavailable after a health probe | The operator guards commands as single-use; reusing a radio session command can consume/refuse the later RF command. The inspected result records do not preserve a separate physical incident proving its exact original exception. | Use a distinct health challenge/session, finish/close it, then use the separately authorized RF session; no arm/send/ACK in health. | `observe_boot` source separates health and RF; later combined health/RF/restoration trial passed. | A health probe must not consume the action it is preparing to observe. |
| Bluetooth off reported as authorization failure | S24 reconnection preflight collapsed `BLUETOOTH_DISABLED` into terminal `AUTHORIZATION_UNAVAILABLE` before fresh GATT. | Preserve platform prerequisite classification and recover through the normal saved-owner workflow when Bluetooth returns. | October 30: 158 host tests; one real off/on cycle recovered fresh D0/GATT/authorization/Snapshot/Ready in 31,145 ms. | Diagnose adapter/permission state before blaming enrollment or deleting bonds. |
| Reopening app retains failed service | Home/task dismissal reattached the same failed service; Add Device scans exclude the enrolled owner route. | Keep enrollment discovery separate from saved-owner recovery; repair the actual preflight/lifecycle boundary. | Corrected S24 APK retained Ready over Home/relaunch and Recents dismissal/replacement Activity; fresh clock action passed. [Earlier mapping](testing/ANDROID_RETURNING_OWNER_RECONNECT_MAPPING.md) preserves older firmware/GATT failures. | Do not repeat Add Device, force-stop, re-pair or clear data as a universal fix. |
| UI says connected but protected operation is unavailable | GATT link, service discovery, authorization, mandatory Snapshot and Ready are distinct stages. | Require fresh connection-local authorization and required same-session state before Ready or protected commands. | [Note20 physical acceptance](../tests/hardware/OT-177-RECONNECT-ACCEPTANCE-2026-09-06.md); the retained October 26 S24 observation proves only discovery/GATT and connection-preserving resume, not complete acceptance. | Locate last success and first failure; never promote link connection to security acceptance. |

## Android integration: inspect the real workflow

**Proven, artifact-bound:** retained Note20 Ultra/Android 13 and S24 Ultra use the
isolated V1-Test package `io.github.nbjelanovic.otclient.v1test`. UI-only Trail Dev
is not proof of hardware integration; V1-Test is not production authorization.
October 30's corrected S24 APK SHA-256 is
`c4e7eaf3e00811bcf9b628a611d2997808b8a565ac2d3cd6fc6a785a02b3aa63`.
Its predecessor was `bf500f8c68730355efa1e95091c808e051c5c8347ad46f1c1ff65932722eb464`.
The correction was local/unpublished at the retained checkpoint; do not assume
another checkout or installed device includes it.

Use ordinary launcher/visible app controls: enter the saved-device service route,
observe fresh authorization/Snapshot/Ready, issue the visible clock-sync action,
then inspect its response and actual display. Clock sync is not GNSS acceptance.
Home/resume, Activity replacement, service restart and process death are different
cases; report which actually occurred. Automated UI observations are physical
device evidence but not human usability acceptance.

Read-only ADB inspection templates; substitute the currently authorized endpoint:

```powershell
adb devices -l
adb -s <authorized-endpoint> shell dumpsys package io.github.nbjelanovic.otclient.v1test
adb -s <authorized-endpoint> shell dumpsys activity services io.github.nbjelanovic.otclient.v1test
adb -s <authorized-endpoint> shell pidof io.github.nbjelanovic.otclient.v1test
adb -s <authorized-endpoint> logcat -d -t 200
```

Scope/redact logs before retaining them: general logcat may contain unrelated
personal data. These templates are inspection, not authorization to install,
force-stop, toggle Bluetooth, clear logs/data, delete bonds or drive UI. Use existing
authorized diagnostics first; ask the owner only for genuine physical interaction,
protected prompts or missing authority. Interpret `SCAN_START`/`SCAN_STOP`, GATT
status/service discovery, preflight reason, claim, Snapshot and Ready in order.
See [runtime](../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/BleCompanionRuntime.kt)
and [facade](../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/AndroidBluetoothGattFacade.kt);
source presence does not prove the installed APK's behavior.

## Established libraries and reference implementations

Use official chip-manufacturer datasheets, SDK documentation, application notes
and licensed reference drivers; official board-manufacturer schematics and pin
definitions; published technical standards and general public LoRa specifications;
and independently developed Trail implementations. Do not use Meshtastic or
MeshCore source code, architecture or implementation patterns as references for
new Trail development.

Keep working drivers. RadioLib and other third-party libraries are existing
dependencies only where actually used; their source is not a recommended starting
point for new implementations. Preserve dependency, licensing and attribution
records. The recorded physical evidence qualifies only its exact artifacts.

| Permitted reference / existing dependency | Trail already reuses | Remains Trail-specific | Hardware adaptations | License | Compatibility / maintenance risks |
|---|---|---|---|---|---|
| [Semtech SX1262 documentation](https://www.semtech.com/products/wireless-rf/lora-connect/sx1262) and [SX126x C driver](https://github.com/Lora-net/sx126x_driver) | Chip/API reference; inspected Heltec targets use RadioLib, not this separate C driver. | Protocol, admission, queues and bounded lifecycle. | A direct-driver port would need the vendor HAL's reset/wakeup/read/write, plus board SPI, BUSY/IRQ, TCXO, regulator and RF/FEM handling. | Driver: [BSD-3-Clause-Clear](https://github.com/Lora-net/sx126x_driver/blob/master/LICENSE.txt); no patent license granted. | Chip support does not establish board wiring, RF power or regional acceptance; do not introduce a second driver without a concrete need. |
| RadioLib 7.7.1 — existing dependency only | SX1262/Module APIs; [diagnostic dependency lock](../firmware/targets/heltec_v4_radio_diag/dependencies.lock) pins 7.7.1. | [ESP-IDF HAL](../firmware/targets/heltec_v4_radio_diag/main/esp32_radiolib_hal.cpp), FEM switching and bounded driver policy. | Match SPI/CS/BUSY/DIO1/reset, TCXO voltage and external PA/LNA truth table to the actual revision. | [MIT](https://github.com/jgromes/RadioLib/blob/7.7.1/license.txt). | Trail's [TX BUSY](../tools/radiolib_busy_source.py) and [pair-driver calibration](../tools/pair_radio_driver_source.py) derivatives are source-hash pinned; an upgrade can invalidate them. Managed upstream inputs remain untouched. |
| [Espressif ESP-IDF 6.0.2](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/) | GPIO/interrupts, SPI, timers, RTOS, NVS and BLE facilities; bench/radio locks select 6.0.2. | Task/session ownership, persistence rules, security admission and error containment. | Board pinmux, SPI ownership/timing, clock, USB console and sdkconfig. | [Apache-2.0](https://github.com/espressif/esp-idf/blob/v6.0.2/LICENSE); bundled components retain their own licenses. | SDK/API and target changes need matching toolchain/configuration evidence. Use the [versioned SPI reference](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/api-reference/peripherals/spi_master.html), not an unreviewed move to latest. |
| [Espressif esptool](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/index.html) | Guarded ROM adapters pin esptool 5.3.1 and pySerial 3.5; accepted read/write/restore evidence above. | Identity/range/image grants, readback, restoration, first-error retention and SDK command allowlists. | USB/serial route and reviewed reset sequence for the exact chip; use the documented Windows execution context. | esptool: [GPL-2.0-or-later](https://github.com/espressif/esptool/blob/v5.3.1/pyproject.toml); pySerial: [BSD-3-Clause](https://github.com/pyserial/pyserial/blob/v3.5/LICENSE.txt). | SDK hooks, stub defaults and FLASH_END can cross Trail's bounds. Keep the approved frozen adapter; generic CLI commands are not substitutes. |
| Existing [HeltecPairRadioDriver](../firmware/targets/heltec_v4_pair_radio_eval/main/pair_radio_driver.cpp) and [ROM adapter](../tools/enrollment_candidate_rom_adapter.py) | Existing RadioLib/ESP-IDF and esptool boundaries; reuse their interfaces and accepted fixtures first. | MTU, queue/retry/stop budgets, peer/session authority and recovery custody. | New boards need their own HAL, power-off behavior, geometry and recovery binding. | Trail-created code: root [Apache-2.0](../LICENSE); upstream/derived files keep their own notices. | Source presence or host tests do not activate a target or replace a frozen operator's acceptance evidence. |

For an authorized change to an existing dependency, record the exact tag/commit and lock,
retain applicable notices, and check the affected adapter against that version.
Reuse accepted tests/physical results while relevant inputs are unchanged. New
board behavior remains subject to the existing porting checks and physical grants.

Publication gate: PR #68 and subsequent runbook publications must include this
reference-policy correction and preserve unrelated changes; do not publish the
superseded implementation-reference recommendations.

## Carry the lessons to other hardware

| Layer/target | Reuse | Evidence boundary and differences |
|---|---|---|
| All Trail targets | LUProtocol contracts, correlation/recipient identity, bounded ACK/retry/queue rules, fresh authorization, staged diagnostics, first-error retention, exact artifact provenance and recoverable originals | Reuse interfaces and test vectors, not board pin assignments or a diagnostic ACK as a production ACK. See [protocols](protocol/) and [components](../firmware/components/). |
| ESP32-S3 Heltec | Scoped ROM/esptool sequence, partition/erase math, no-stub read-before-return and native USB observations | Proven only for the recorded hardware/profile; other layouts need their own validation. |
| SenseCAP P1-Pro, nRF52840/SX1262 | Protocol/transport abstractions and source-bound build methods | Inventory records bounded historical MeshCore behavior, not Trail firmware acceptance. Confirm exact board revision, bootloader/recovery tool, memory map, SPI/IRQ/BUSY/RESET, TCXO and RF-switch wiring. ESP32 ROM/esptool procedures do not apply. Trail build-only evidence does not prove radio or recovery. |
| Wio Tracker L1 Pro candidate | Same abstraction, observation and recovery principles | Owner identity and public descriptor do not establish exact Pro SKU/revision or pinout. Confirm received hardware and its own bootloader, GNSS, power and radio configuration. Trail integration and role qualification require physical validation. |

## Integrate the next device without repeating investigations

1. **Proposed:** Identify intended role, physical model/revision and authoritative
   task; consult the inventory and affected porting checks. State unsupported features.
2. **Host-tested where existing evidence applies:** Map reusable drivers, protocol
   interfaces and tests; list board-specific differences rather than redesigning the stack.
3. **Requires Physical Validation:** Bind the endpoint and connect using the
   board's established toolchain. Do not transfer ESP32 commands to nRF52840.
4. **Requires Physical Validation:** Preserve originals and prove a board-specific
   recovery route before destructive work; reuse valid custody without redundant copying.
5. **Host-tested only until executed:** Build the exact target/toolchain and verify
   image/partition/link inputs. Reuse unchanged host evidence; test affected boundaries.
6. **Requires Physical Validation:** Under exact authority, install/verify/boot one
   target, observe fresh diagnostics, test permitted real peripherals, then recover
   and confirm ordinary health. Passes and failed attempts retain their distinct scope.
7. **Requires Physical Validation:** Exercise the intended end-to-end Trail role,
   including adverse conditions, actual peers, authorization and user-facing state.
   A USB console, build or diagnostic RF pair alone does not qualify the product.
8. **Proposed release decision:** Compare results to the role's existing measurable
   acceptance criteria and frozen V1 campaign. Missing thresholds/observations stay
   open; preserve exact limits and use targeted reruns after relevant changes.

## Evidence custody and remaining unknowns

October 30's ROM readback diagnosis, radio connection correction, diagnostic
observability correction, successful radio-health trial and S24
`PHYSICAL_RECONNECT_RESULT.json` are retained owner-held evidence, not public
download links. Locate their result/source-freeze records through the responsible
task handoff; do not reconstruct or rerun them from this prose. Their raw device
identifiers, grants, captures and private paths are intentionally omitted here.

Exact retained anchors for the new observations (not download locations):

- S24 `PHYSICAL_RECONNECT_RESULT.json`, SHA-256
  `c06abd94d8e1db9ad4d638b1a094e1efbf35b9b8d10cebc8591f04fb41615a63`.
- Successful radio-health trial `RESULTS.md`, SHA-256
  `4b0686087bc3bac028150f52faf9ab779e9839c84a72b6f4e35bcc5f09ee49e0`.

- COM10 `COM10_DIAGNOSIS.json`, SHA-256
  `b0b900476699e1d1e2298aae2f8f95ffc436370543c83057e7b4afe35f6b9773`.
- COM10 `COM10_CONNECTION_ONLY_RESULT.json`, SHA-256
  `0d8cdae84d10adcfe0c4da9493f89847645ac4a5f5baab27fdc5f7e6501d5a8f`.

Still open: exact original exceptions in collapsed historical connection/read
failures; missing RF metrics/distance; sustained reliability and full phone-to-phone
authenticated delivery on frozen V1 candidates; cold-power/other lifecycle cases
outside each bounded observation; exact received nRF board revisions/pinouts and
Trail bootloader/radio/recovery qualification. The one-use health-session warning
above is source-supported; its specific original physical failure is not independently
reconstructed. No missing measurement is supplied by guessing or synthetic tests.

When another project session solves a reusable defect, record symptom, confirmed
cause, correction, verification and limits in its existing task. GitHub Development
maintains this runbook from that evidence; product sessions update their task-specific
records. Keep [PROGRESS_LOG.md](PROGRESS_LOG.md) as the milestone history.
