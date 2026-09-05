# OpenTrail Hardware Inventory

Last updated: 2026-09-04

Compatibility states used here:

- `identified`: electronically or physically identified, but not yet validated for OpenTrail.
- `experimented`: used in a recorded, bounded experiment.
- `validated`: passed defined repeatable compatibility criteria.

No OpenTrail hardware is validated yet.

Current evidence roles are deliberately different:

- `OT-DEV-001` and `OT-DEV-002` are already assembled **bench clients**. They
  are available for USB detection, flash/recovery, LoRa, GNSS, messaging, and
  compatibility tests, but are not the board-level parts intended for the first
  complete touchscreen client.
- `OT-DEV-003` is an integrated solar **packaged-repeater candidate**. Its
  enclosure, battery, solar, radio, and GNSS configuration may be evaluated as
  the actual optional repeater hardware.
- `OT-CAND-004` is an arrived self-contained **Wio Tracker candidate**. Its
  first USB/runtime/configuration pass is `experimented` evidence only; it is
  not a validated or supported client.
- The first complete client remains a separate hardware freeze covering its
  board, touchscreen, controls, power, enclosure, GNSS, and antenna system.

For development, each Heltec bench unit uses its factory-programmed ESP32-S3
base MAC as the durable electronic key mapped to its `OT-DEV-###` label. Exact
values are stored in the ignored local development registry, not this public
inventory, GitHub, shipped firmware/Android data, protocol fields, or customer
logs/UI. Every device read or write must re-read and exactly match that binding
after fresh isolation and enumeration; a port name or BLE advertising address
is never sufficient identity.

See the
[2026-08-10 hardware and US regulatory reconciliation](HARDWARE_REGULATORY_INVENTORY_2026-08-10.md)
for the separation between exact-unit evidence, official family information,
and the fail-closed field gate.

## Candidate OT-CAND-004

The owner identifies the arrived unit as a **Seeed Studio Wio Tracker L1 Pro**.
It had already been flashed as a MeshCore USB Companion and configured for a
USA frequency plan before OpenTrail inspection, so its shipping/pre-write state
was not preserved or independently verified. The first privacy-safe pass is
classified `experimented` for USB/runtime/configuration evidence only.

| Field | Observed result | Evidence boundary |
| --- | --- | --- |
| Evidence state | `experimented`; OT-020 `partial` | USB/runtime/configuration evidence only; no compatibility, validation, or support claim |
| Retail identity | Owner reports Wio Tracker L1 Pro; Windows public USB model is `Seeed Wio Tracker L1`, USB family `2886:1667` | The runtime descriptor does not establish the exact Pro SKU/revision. Exterior label and package evidence remain unrecorded; transient COM assignment is intentionally omitted |
| Shipping/pre-write state | Not available | The owner had already flashed USB Companion and selected a USA frequency plan; shipping firmware/role/configuration and exact write history were not preserved or verified |
| Installed firmware/role | MeshCore USB Companion `v1.17.0-727fc05`, build 09-Aug-2026; repeat false | Four fixed read-only cycles returned stable model/firmware/profile values and increasing uptime; no write, reset, reboot, or transmit action was used |
| Current radio configuration | 910.525 MHz, 62.5 kHz bandwidth, SF7, CR5, configured/max power 22/22 dBm | Configuration only; not proof of exact RF hardware, antenna fit, FCC grant coverage, or authorized operation. No radio packet was sent |
| Runtime snapshot | 4.111 V; queue, packet, airtime, core-error, and receive-error counters all zero; three more cycles remained error/traffic-free | Transient bounded bench evidence, not power/endurance or load evidence |
| GNSS | Detected true, active false, no GPS telemetry present | No setting changed; physical module, activation, fix, accuracy, stale/loss/recovery, cadence, and power behavior remain untested |
| Non-transmitting Heltec comparison | Channel 0 name/hash/secret equality passed only in memory; both default scopes were unconfigured; identities were distinct; clocks were within one second | Values and identities were not emitted. This is configuration comparison only, not over-air interoperability |
| Recovery candidate | No `TRACKER L1` volume appeared during normal runtime | DFU was not entered; vendor-documented entry/exit and bootloader recovery remain untested |
| Evidence record | [OT-020 first USB/runtime pass](../tests/hardware/OT-020-2026-08-13.md) | BLE, over-air, GNSS fix/loss, label/SKU/revision, antenna/RF/regulatory, power/endurance, recovery, and clean-machine gates remain open |

On 2026-08-14, the current-tree C# and Python loaders accepted this Wio family.
The warning-free 59-group C# suite and three consecutive built-in production
refreshes passed with one Heltec, one SenseCAP, and one Wio runtime-identified
and zero ready to flash. The replacement source-free package passed independent
manifest/hash/extraction/launch verification and three exact-roster external UI
Automation cycles with the same three public devices and zero ready. This is
loader-recognition evidence only. It does not change the statement that no
OpenTrail hardware is on a tested-compatible list.

## Device OT-DEV-001

Connected directly to the development laptop for bounded inventory beginning 2026-08-08; selected as the first experimental OpenTrail target on 2026-08-16.

| Field | Verified result | Evidence/source |
| --- | --- | --- |
| Inventory state | Experimental OpenTrail bench target with exact received V4.2 documented-high-band profile, prior bounded BLE/link-status and direct-radio evidence, and current two-device live battery/GNSS compact-footer acceptance. Installed antenna, electrical radio path, full pinout, protected authorization, live OpenTrail LoRa activity, interactive UI/input, recovery-after-loss, regulatory acceptance, and support remain unresolved | [OT-147](../tests/hardware/OT-147-2026-08-26.md), [OT-115](../tests/hardware/OT-115-2026-08-21.md), [OT-114](../tests/hardware/OT-114-2026-08-21.md), [OT-085A](../tests/hardware/OT-085A-2026-08-19.md), [OT-085B](../tests/hardware/OT-085B-2026-08-19.md), and [OT-103](../tests/hardware/OT-103-2026-08-20.md) evidence |
| Evidence role | Experimented OpenTrail bench target with exact received-unit identity, physically accepted startup/status OLED, bounded public BLE read, and automatic lifecycle evidence; not validated or supported hardware | OT-103 adds identity evidence only. OT-061 full-image, OT-064 app-only, and OT-085A app-only write authorizations are consumed; OT-085B and OT-103 performed no target write and grant no standing write, recovery, unit-2, radio, regulatory, or support authority |
| Purchase record | One of two units from Meshnology two-set V4 GPS bundle, Amazon ASIN `B0FS1WQWKF`, selected as `Black-2` | Owner-provided purchase link, 2026-08-12. An owner-provided package photo reads `WiFi LoRa 32 V4`, `LoRa Dev-kits`, `LoRa Band`, and `HF 863-928`; the checkbox state is not claimed. The listing describes two V4 boards, two L76 GNSS modules, two 3000 mAh batteries, N39 cases, and 915 MHz antennas but remains corroborating purchase evidence rather than electrical verification |
| Product/model | Exact received Heltec Automation `WiFi LoRa 32 V4`, PCB/RF-variant model `HTIT-WB32LAF`, received revision `V4.2`, documented-high-band profile | Privacy-safe owner-photo observations admitted by [OT-103](../tests/hardware/OT-103-2026-08-20.md); the prior `Heltec V4 OLED` runtime identity remains corroborating evidence |
| Official documented family/profile | V4.2 datasheet Table 1.5 records `HTIT-WB32LAF` at `868-928 MHz` and `28 +/- 1 dBm`; Table 3.5.1 separately records `863-928 MHz` and `28 +/- 1 dBm`; Table 3.1 identifies ESP32-S3R2, SX1262, 16 MiB flash, and 2 MiB PSRAM | Official PDF digest and source URL are recorded in OT-103; the PDF was not retained. The two published band ranges stay distinct, and family-level SX1262/power statements are not received-unit electrical, antenna, legal-region, or regulatory proof |
| ROM maintenance USB | Espressif Ports-class interface; transient port assignment omitted | Windows Ports-class device enumeration |
| USB identity | Espressif `VID 303A`, `PID 1001`; USB Serial/JTAG | Windows Plug-and-Play properties |
| MCU | ESP32-S3, QFN56, revision v0.2 | `esptool 5.3.1 chip-id` |
| CPU/features | Dual core plus LP core, up to 240 MHz, Wi-Fi, Bluetooth 5 LE | `esptool 5.3.1 chip-id` |
| PSRAM | 2 MB embedded PSRAM, AP_3v3 | `esptool 5.3.1 chip-id` |
| SPI flash | 16 MB, manufacturer ID `0x68`, device ID `0x4018`, quad I/O, 3.3 V | `esptool 5.3.1 flash-id` |
| Crystal | 40 MHz | `esptool 5.3.1 chip-id` |
| Secure Boot | Disabled | `esptool 5.3.1 get-security-info` |
| Flash encryption | Disabled | `esptool 5.3.1 get-security-info` |
| State during query | ROM USB/UART download bootloader | ROM serial banner and query connection |
| Normal application USB | Espressif application USB `VID 303A`, `PID 0002`; transient port assignment omitted | Windows enumeration after normal boot |
| Development electronic identity | Verified prospectively on 2026-09-04: one isolated ESP32-S3 factory base MAC is bound to `OT-DEV-001` in the ignored local registry and matched before and after the canonical application write/readback. The raw value remains private and is not public or production data. | [Development identity procedure](../docs/testing/HELTEC_DEVELOPMENT_DEVICE_IDENTITY.md) and [OT-168 evidence](../tests/hardware/OT-168-2026-09-04.md) |
| Current installed application | Exact canonical reproducible saved-owner successor: 563,824 bytes at `0x10000`, SHA-256 `91D4CEB48CCFBCD21AC97CE604C48FBCCA04D70408D2BF749C90CB053AD04824`, written application-only and independently read back at the exact length on 2026-09-04. The erase ended at `0x99FFF`; its 1,424-byte tail was all `0xFF` before and after. No esptool retry occurred. The retained private full application-range recovery capture remains SHA-256 `BC2421E0D11AE84FD38C17F49A182E1215DAF5F9BD38BDFA62DD8178092D1EFF`. | [OT-168 evidence](../tests/hardware/OT-168-2026-09-04.md) and [returning-owner mapping](../docs/testing/ANDROID_RETURNING_OWNER_RECONNECT_MAPPING.md) |
| Current bounded runtime | The owner confirmed the display returned after the completed write/readback gate and one normal physical reset with BOOT released. The same retained Note20 then passed one immediate saved-owner reconnect through authenticated Ready. It subsequently passed retained-owner persistence after complete app-process death using the current mode chooser, with no assistant service-start tap, and automatic reconnect through Ready after one brief physical RESET with BOOT untouched and USB attached. The old-link-callback-to-new-link interval was approximately 1.493 seconds; exact full Ready latency was not instrumented. The mandatory Snapshot remained radio unavailable, GNSS unknown, power unknown, position stopped, and queued 0. No new PIN, re-pair, app-data clear, reinstall, flash, ROM read, erase, radio operation, or phone input after RESET occurred. This is warm-reset evidence, not finished zero-tap launch, cold power-removal, factory-reset, second-pairing, or two-unit acceptance. A later 277.680-second verified-ROM-entry-to-reset-completion absence reproduced terminal retry exhaustion: the owner confirmed normal display return, but the untouched Note20 app made no new reconnect and remained failed 78.664 seconds after reset completion. This separate no-flash test leaves the hardware display on and app disconnected; periodic recovery remains unimplemented. | [OT-168 evidence](../tests/hardware/OT-168-2026-09-04.md), [returning-owner mapping](../docs/testing/ANDROID_RETURNING_OWNER_RECONNECT_MAPPING.md), [OT-164 evidence](../tests/hardware/OT-164-2026-08-29.md), and [OT-147 evidence](../tests/hardware/OT-147-2026-08-26.md) |
| Protected-storage source proof | A bounded read-only operation matched the exact installed 3,072-byte partition table and verified that the complete 1 MiB source region was all `0xFF`; it retained no raw bytes or private binding details, and its temporary executor was deleted. Manual RST returned the Trail logo and `BLE ADVERTISING`. This satisfies only the OT-070 source prerequisite | [OT-074 evidence](../tests/hardware/OT-074-2026-08-17.md) |
| Prior installed application | MeshCore `v1.16.0-07a3ca9` | Historical application strings and pre-OT-061 public runtime evidence |
| Exact application recovery artifact | The complete 470,928-byte OT-064 factory application was captured read-only and independently verified after connection close; SHA-256 `A7D8E672CF9169F1D1D4E86EEFF80399C47A145E7D64904C207DD5F1B23F359B`; retained only as a private ignored recovery artifact, with no restore/write authority | [OT-076 evidence](../tests/hardware/OT-076-2026-08-17.md) |
| Firmware date reported by MeshCore | 06-Jun-2026 | Connected MeshCore browser application's Device Info |
| Prior MeshCore transport/role | **USB Companion** before OT-061; BLE Companion was installed during the initial inventory | Historical official MeshCore web-flasher and Web Serial evidence; MeshCore was intentionally erased by OT-061 |
| USB Companion flash result | Successful clean flash using `heltec_v4_companion_radio_usb` `v1.16.0-07a3ca9`; erase enabled | Post-flash application boot and first-run Device Setup observed in the connected MeshCore browser app, 2026-08-08 |
| Framework/build | Arduino on ESP-IDF 4.4.7; build date March 5, 2024 | ESP application descriptor in OTA slot `app0` |
| Prior MeshCore flash layout | NVS, OTA metadata, two 6.25 MB app slots, 3.38 MB SPIFFS, 64 KB coredump; `app1` was empty | Historical partition-table/application-descriptor evidence before OT-061; private contents were never read |
| Prior MeshCore radio configuration snapshots | USA/Canada preset 910.525 MHz, 62.5 kHz bandwidth, spreading factor 7, coding rate 5, transmit power 10 dBm; clean-flash default 869.618 MHz, 62.5 kHz, SF8, CR5, 10 dBm | Historical pre-OT-061 MeshCore configuration evidence only; OpenTrail LoRa was not enabled or tested |
| Battery snapshots | 48%, 3.58 V before charging; 97%, 4.17 V after charging; 4.226 V after the USB Companion flash | Connected MeshCore browser telemetry and `meshcli get stats_core`, 2026-08-08; transient observations only |
| MeshCore capacity/status | 1/40 channels, 0/350 contacts, storage 0% used (0 KB of 3169 KB) | Connected MeshCore browser application's Device Info, 2026-08-08 |
| Prior MeshCore post-flash USB runtime snapshot | USB serial connection successful; uptime 670 seconds; 0 errors; queue length 0; 0 packets sent/received; 0 transmit/receive airtime | Historical 2026-08-08 read-only MeshCore evidence |
| Firmware-reported repeater frequency points | 433.000 MHz, 869.495 MHz, and 918.000 MHz | `meshcli get allowed_repeat_freq`, 2026-08-08; firmware-reported permitted repeater points, not proof of antenna suitability, exact RF front end, legal authorization, or full supported band |
| GPS/GNSS bench evidence | OpenTrail now powers and parses the connected GNSS source and presents its current satellite count; the owner visually confirmed a populated `GPS` value after OT-147. Historical MeshCore detection/activation and an OT-103 privacy-safe photo showing a module marked `QUECTEL` and `L76K` remain corroborating evidence | [OT-147](../tests/hardware/OT-147-2026-08-26.md), privacy-safe historical `meshcli` telemetry, and [OT-103](../tests/hardware/OT-103-2026-08-20.md); no coordinates or identity were saved. This proves the live satellite-count path, not a current position fix, accuracy, stale/loss/recovery behavior, radio compatibility, or support |

The device-specific MAC address was observed during the private diagnostic pass
but is deliberately excluded from this public inventory.

### Still unresolved

- Exact installed RF front end and full electrically verified operating band; the
  documented `HTIT-WB32LAF` profile and family-level SX1262 statement do not
  establish the received unit's electrical path
- Installed antenna model, band, gain, connector, feed cable, and loss
- Complete board pinout, exact OLED controller silicon, interactive
  display/input behavior, battery calibration/charger/endurance details, GNSS
  fix/accuracy/stale-loss behavior, sensors, and other pin assignments; the
  selected-unit OLED startup/status binding, L76K module marking, and live
  battery/satellite-count footer are physically accepted
- Direct-radio MTU/PHY/region configuration and regional regulatory/legal
  operating constraints

MeshCore's historical runtime code reported `Heltec V4 OLED`; OT-103 now supersedes that runtime-only identity boundary for OT-DEV-001 with exact privacy-safe received-unit markings. The admitted `HTIT-WB32LAF` / `V4.2` profile does not prove the installed antenna, electrical radio path, full usable band, legal operating mode, regulatory acceptance, compatibility, or support.

Official references used for family matching:

- [Heltec WiFi LoRa 32 V4 product specification](https://heltec.org/project/wifi-lora-32-v4/)
- [Heltec WiFi LoRa 32 V4.2 datasheet](https://resource.heltec.cn/download/WiFi_LoRa_32_V4/datasheet/WiFi_LoRa_32_V4.2.0.pdf)
- [MeshCore Heltec V4 board target](https://github.com/meshcore-dev/MeshCore/blob/main/boards/heltec_v4.json)
- [MeshCore Heltec V4 runtime model detection](https://github.com/meshcore-dev/MeshCore/blob/main/variants/heltec_v4/HeltecV4Board.cpp)

## Development laptop tooling

- OT-034 adds a repository-local, build-only ESP-IDF v6.0.2 `esp32s3`
  candidate under `firmware/targets/heltec_v4_bench`. Its exact contract admits
  one fixed application startup line and a recurring USB Serial/JTAG heartbeat;
  only the application-owned boot-local elapsed-millisecond value is dynamic.
  The application does not initialize, access, or bind board I/O, radio, BLE,
  Wi-Fi, GNSS, storage, identity, or secrets. Framework boot/runtime logs remain
  unreviewed. Three host admission groups and source parsing pass. The pinned
  native build and size analysis also pass, recording exact artifact hashes and
  a hash-stable 8.05-second incremental rerun with status `NOT-FLASHED`; see
  `tests/hardware/OT-034-2026-08-14.md`. No device was discovered, opened, or
  changed. Its generic 2 MB/DIO/80 MHz image header and NVS/PHY/factory table do
  not match an authoritative 16 MB/2 MB-PSRAM received-board profile. This is
  native candidate-build evidence, not compatibility, recovery, runtime, or
  support evidence.
- Windows enumerated the native USB Serial/JTAG interface using the Microsoft `usbser.inf` driver.
- `esptool 5.3.1` is installed for the current Windows user and is available as `python -m esptool`.
- At the pre-OT-061 MeshCore acceptance checkpoint, `meshcore-cli 1.5.7`,
  `meshcore 2.3.8`, and `bleak 3.0.2` were installed for the current Windows
  user. BLE discovery worked, but direct BLE CLI connections on this laptop
  failed in the Windows WinRT GATT address-resolution layer before a MeshCore
  command was sent. OT-DEV-001 then ran USB Companion so serial CLI validation
  could proceed after the browser released the Web Serial port; OT-061 later
  replaced that installation with the experimental OpenTrail image.
- Historical pre-OT-061 USB Companion serial validation succeeded on the selected transient port:
  MeshCLI connected, confirmed firmware `v1.16.0-07a3ca9`, and returned
  read-only configuration/runtime statistics. The browser and MeshCLI could
  not own the Web Serial/COM port simultaneously.
- `tools/Test-MeshCoreUsbNodes.ps1` automatically discovers connected Espressif USB Companion ports and returns a redacted, read-only health snapshot (model/firmware, battery, radio settings, errors, queue, packet counts, and airtime). It deliberately omits node names, public keys, coordinates, PINs, and channel data. Run it only while MeshCore browser tabs are disconnected.
- `tools/Get-MeshCoreGnssStatus.py` returns a read-only role-labeled GNSS snapshot for USB Companions and a serial repeater. It reduces companion telemetry in memory to GPS-field presence and emits only detection/active/fix/satellite state; default output omits local ports, raw replies, coordinates, identities, keys, and PINs. Its four parser/redaction groups pass, and the three-device live snapshot succeeded on 2026-08-12.
- The source-free Windows device utility now uses SetupAPI plus only fixed
  MeshCore runtime-identity requests, with no CIM/WMI, Python, MeshCLI, shell, or
  network dependency. On 2026-08-13 its built-in C# path runtime-identified both
  attached Heltec V4 OLED companions and the SenseCAP Solar repeater while
  retaining every exact-hardware/Flash blocker. It does not expose ports,
  serials, hardware-instance paths, raw replies, BLE PINs, identities, keys, or
  coordinates; see `tests/hardware/OT-019H-2026-08-13.md`.
- `tools/meshcore_channel_lease.py` and the three-node soak harness add a non-secret crash-recovery journal for temporary private-channel tests. A real stopped-session recovery cleared and verified both Heltecs; short traffic/smoke cycles passed; and a 300-minute alternating run delivered 300/300 with zero loss/duplicates/errors, exact +300 repeater flood RX/TX, repeat preserved, and verified channel/journal cleanup. See `tests/hardware/OT-009A-2026-08-09.md`.
- Windows pairing is required before the browser can open the encrypted MeshCore GATT service. A stale or malfunctioning laptop Bluetooth state can preserve the device record while hiding live advertisements; clearing the pairing and restarting the laptop Bluetooth radio restored discovery during this test.
- The initial inventory and BLE diagnostic pass performed no flash write or erase. The user then intentionally flashed the official Heltec v4 USB Companion `v1.16.0-07a3ca9` image with erase enabled; the old MeshCore identity/configuration was expected to be replaced.
- The MeshCore browser inspection was read-only: no settings were changed, and precise location/public-key data visible in the live application were intentionally not copied into this inventory.

## Device OT-DEV-002

Connected independently to the development laptop and queried read-only over USB on 2026-08-08 after the user installed USB Companion firmware and selected the USA/Canada preset.

| Field | Verified result | Evidence/source |
| --- | --- | --- |
| Inventory state | Experimental OpenTrail bench target with an independently accepted exact received V4.2 documented-high-band profile, current OT-147 live battery/GNSS compact-footer image, and historical OT-114 direct-radio evidence. Installed antenna, electrical radio path, full pinout, protected authorization, live OpenTrail LoRa activity, interactive UI/input, recovery-after-loss, regulatory acceptance, compatibility, and support remain unresolved | [OT-147](../tests/hardware/OT-147-2026-08-26.md), [OT-119](../tests/hardware/OT-119-2026-08-22.md), [OT-115](../tests/hardware/OT-115-2026-08-21.md), and [OT-114](../tests/hardware/OT-114-2026-08-21.md) evidence |
| Evidence role | Independently identified experimental OpenTrail bench target; not validated or supported hardware | OT-119 adds exact received-unit profile evidence only and grants no standing device, write, recovery, radio, regulatory, compatibility, or support authority |
| Purchase record | Second unit from Meshnology two-set V4 GPS bundle, Amazon ASIN `B0FS1WQWKF`, selected as `Black-2` | Owner-provided purchase link, 2026-08-12; same listing boundary as `OT-DEV-001` |
| Product/model | Exact received Heltec Automation `WiFi LoRa 32 V4`, PCB/RF-variant model `HTIT-WB32LAF`, received revision `V4.2`, documented-high-band profile | OT-119 owner-confirmed same-unit marking photo plus privacy-safe ROM evidence; the prior `Heltec V4 OLED` runtime result remains corroborating only |
| Official documented family/profile | V4.2 datasheet Table 1.5 records `HTIT-WB32LAF` at `868-928 MHz` and `28 +/- 1 dBm`; Table 3.5.1 separately records `863-928 MHz` and `28 +/- 1 dBm`; Table 3.1 identifies ESP32-S3R2, SX1262, 16 MiB flash, and 2 MiB PSRAM | OT-119 binds the accepted OT-103 official-source facts without turning family specifications into received-unit electrical, antenna, legal-region, regulatory, compatibility, or support proof |
| ROM maintenance USB | Espressif Ports-class interface; transient port assignment omitted | OT-119 privacy-safe no-stub observation receipt |
| MCU | ESP32-S3, revision v0.2 | OT-119 privacy-safe ROM observation |
| PSRAM | 2 MiB embedded PSRAM | OT-119 privacy-safe ROM observation |
| SPI flash | 16 MiB | OT-119 privacy-safe ROM observation; no flash bytes were read |
| Crystal | 40 MHz | OT-119 privacy-safe ROM observation |
| Application USB | Espressif application USB `VID 303A`, `PID 0002`; transient port assignment omitted | Windows/pySerial enumeration |
| Development electronic identity | Pending one isolated `read-mac` enrollment into the ignored local registry. The second unit must produce a distinct base MAC before the two named mappings are accepted. | [Development identity procedure](../docs/testing/HELTEC_DEVELOPMENT_DEVICE_IDENTITY.md) |
| Current installed application | Current image remains unresolved. OT-164 installed and verified the 507,296-byte `69AE6546...` application on both anonymous nodes, but no fresh read binds that historical image to `OT-DEV-002`. The later phone-test successor is now prospectively mapped to `OT-DEV-001`; do not infer the second unit's present image until its distinct private base-MAC enrollment and read pass. | [OT-164 evidence](../tests/hardware/OT-164-2026-08-29.md), [returning-owner mapping](../docs/testing/ANDROID_RETURNING_OWNER_RECONNECT_MAPPING.md), and [OT-168 evidence](../tests/hardware/OT-168-2026-09-04.md) |
| Current bounded runtime | Populated `BAT`/`GPS` and pairing-window behavior remain historical accepted evidence. No 2026-09-04 observation is assigned to this named row until the private electronic mapping is established. | [OT-164 evidence](../tests/hardware/OT-164-2026-08-29.md), [OT-147 evidence](../tests/hardware/OT-147-2026-08-26.md), and [OT-168 evidence](../tests/hardware/OT-168-2026-09-04.md) |
| Prior MeshCore firmware/role | USB Companion `v1.16.0-07a3ca9`, firmware date 06-Jun-2026; 350 contacts, 40 channels; repeat disabled; path-hash mode 0 | Historical MeshCLI evidence before OT-112 |
| Prior MeshCore radio configuration | User-applied USA/Canada preset: 910.525 MHz, 62.5 kHz bandwidth, SF7, CR5, 10 dBm transmit power; maximum reported 22 dBm | Historical filtered `meshcli infos` query before OT-112 |
| Telemetry/contact defaults | Environment, location, and base telemetry disabled; manual contact addition disabled | Filtered `meshcli infos` query |
| Battery snapshot | 4.069 V | `meshcli get stats_core`; transient observation only |
| Prior MeshCore runtime snapshot after USB recovery | Latest 2026-08-13 recovery read: uptime 133 seconds; 0 errors; queue length 0; 0 packets sent/received; 0 transmit/receive airtime; 0 receive errors; firmware and radio configuration unchanged | Historical `meshcli` evidence and [OT-019O](../tests/hardware/OT-019O-2026-08-13.md) before OT-112 |
| Firmware-reported repeater frequency points | 433.000 MHz, 869.495 MHz, and 918.000 MHz | `meshcli get allowed_repeat_freq`; not proof of antenna suitability, exact RF front end, legal authorization, or full supported band |
| Low-level ROM/flash query | OT-119 completed one bounded privacy-safe no-stub read-only observation for the owner-selected `OT-DEV-002`: ESP32-S3 revision v0.2, 40 MHz crystal, 16 MiB flash, and 2 MiB embedded PSRAM. The normal `ot_bench` heartbeat returned. Raw output, port, MAC/chip identifier, USB serial/hardware path, flash bytes, and raw eFuse contents were not retained or read. The older OT-019O failed attempt remains historical recovery evidence. | [OT-119 admission](../tests/hardware/OT-119-2026-08-22.md) and [OT-019O negative/recovery evidence](../tests/hardware/OT-019O-2026-08-13.md) |
| GPS/GNSS bench evidence | OpenTrail now powers and parses the connected GNSS source and presents its current satellite count; the owner visually confirmed a populated `GPS` value after OT-147. Historical MeshCore detection/activation remains corroborating evidence | [OT-147](../tests/hardware/OT-147-2026-08-26.md) and the same privacy-safe historical method as `OT-DEV-001`; no coordinates or identity were saved. This proves the live satellite-count path, not a current position fix, accuracy, stale/loss/recovery behavior, exact physical module marking, or support |

Device-specific identity/public-key values returned by MeshCLI were deliberately excluded from this inventory.

### Still unresolved for OT-DEV-002

- Exact installed RF front end and full electrically verified operating band; the
  admitted `HTIT-WB32LAF` / `V4.2` documented-high-band profile and
  family-level SX1262 statement do not establish the electrical path
- Installed antenna type/connector, exact physical GNSS module marking, battery calibration/charger/endurance details, GNSS fix/accuracy/stale-loss behavior, sensors, and remaining pin assignments
- Regulatory constraints applicable to the selected preset and intended deployment

## Device OT-DEV-003

Connected to the development laptop and queried over its MeshCore repeater USB
console on 2026-08-08 after the user installed the official repeater firmware
and selected the USA region.

| Field | Verified result | Evidence/source |
| --- | --- | --- |
| Inventory state | Runtime board/firmware identity, USB interface, active radio configuration, battery, repeater role, and close-range forwarding behavior identified; purchase record identifies P1-Pro while the exact received label/revision and physical internals remain unresolved | MeshCLI repeater console, USB enumeration, owner purchase record, and the bounded OT-009 bench experiment |
| Evidence role | Integrated solar packaged-repeater candidate | May be evaluated as the actual optional repeater after exact received-profile, recovery, GNSS, power, weather, radio, and regulatory gates |
| Purchase record | SenseCAP Solar Node **P1-Pro**, Amazon ASIN `B0FMDHBWX8` | Owner-provided purchase link, 2026-08-12. Seeed's current MeshCore P1-Pro product is SKU `100023690` with XIAO nRF52840 Plus, Wio-SX1262, L76K GNSS, and battery; exact received label/revision remains to be transcribed |
| Runtime board | `Seeed SenseCap Solar` | Repeater `board` command |
| USB interface during test | USB `VID 2886`, `PID 0059`; transient port assignment omitted | pySerial enumeration |
| Installed role and firmware | MeshCore Repeater `v1.16.0-07a3ca9`, build 06-Jun-2026 | Repeater `ver` command |
| Radio configuration | 910.5250244 MHz, 62.5 kHz bandwidth, SF7, CR5, 22 dBm transmit power; repeating enabled | Repeater `get radio`, `get tx`, and `get repeat` commands |
| Battery snapshot | 4.155 V while USB-connected | Repeater `stats-core`; transient observation only |
| Runtime health snapshot | Uptime 448 seconds, 0 core errors, empty queue, -110 dBm reported noise floor, and 0 receive errors | Repeater `stats-core`, `stats-radio`, and `stats-packets` |
| Clock | Fresh flash initially reported 15-May-2024; synchronized over USB to current UTC and verified remotely from both Heltec companions | Repeater `clock sync`; Companion `req_clock` from both Heltec units |
| Companion discovery | Both Heltec companions independently stored the SenseCAP repeater advert | Redacted contact-list comparison on both Heltec units |
| Close-range forwarding | One temporary private-channel message delivered in each Heltec direction with 0 loss, 0 duplicates, 240.8/276.9 ms latency, and 11.5/12.0 dB SNR; the repeater recorded exactly +2 flood RX/+2 flood TX. Explicit one-hop direct routes then succeeded in both directions (1,121 ms and 880 ms acknowledgement round trips), each with exactly +2 direct RX/+2 direct TX at the repeater. With repeat temporarily off, the repeater recorded +1 direct RX/+0 direct TX, the sender timed out, and the destination received no message. | `tests/hardware/OT-009-2026-08-08.md` |
| GPS/GNSS bench evidence | The documented status initially reported off; explicit bench enablement produced active/no-fix/0 satellites for the first 27 seconds, followed later by a live fix and checks at 4, 7, and 8 satellites | MeshCore repeater bare `gps` status, 2026-08-12; no coordinates or identity were saved. This proves the installed firmware/GNSS path can obtain a live fix, not exact physical module/wiring, accuracy, repeatable cold-start time, loss/obstruction behavior, or power cost |

The device-specific USB serial number, MeshCore public key, and node identity
were deliberately excluded from this inventory.

### Still unresolved for OT-DEV-003

- Exact received P1-Pro label/SKU/revision, installed battery revision,
  physical GPS/GNSS module/wiring, enclosure revision, and internal board revision
- GNSS accuracy, repeatable cold-start time, stale/loss and obstructed behavior,
  plus active-GNSS power cost
- Independent MCU/radio/flash identity and exact antenna/RF characteristics
- Solar charging performance, sleep/current behavior, weather exposure, and
  battery endurance
- Field range, physical obstructed-path behavior, congestion behavior, reboot recovery,
  and regulatory constraints at the selected power and radio preset
- MeshCLI `req_status` compatibility: the repeater exchanged packets during the
  request but MeshCLI 1.5.7 returned no parsed status within 15 seconds;
  `req_clock` succeeded from both companions, so this was not a radio-link
  failure

## Two-node USB preflight

Both boards were connected simultaneously to the development laptop on 2026-08-08 and detected independently; transient port assignments are omitted. The redacted USB health check confirmed:

- Both report Heltec V4 OLED and MeshCore USB Companion `v1.16.0-07a3ca9`.
- Both use 910.525 MHz, 62.5 kHz bandwidth, SF7, CR5, and 10 dBm transmit power.
- Battery snapshots were approximately 4.21 V and 4.07 V.
- Both reported 0 errors, empty queues, 0 packets sent/received, 0 receive errors, and 0 transmit/receive airtime.

The user subsequently confirmed both LoRa antennas were attached. OT-007A establishes authenticated bidirectional application delivery. A temporary private-channel sample delivered 5/5 numbered messages in each direction with 0 loss, 0 duplicates, 233.3-247.2 ms observed latency, 11.25-12.25 dB SNR, zero receive/core errors, and empty queues. The temporary channel was erased and verified empty on both devices. Earlier application timeouts were harness false negatives caused by requiring the full MeshCore display text to equal the marker; MeshCore includes sender display text around channel messages. `OT-DEV-002` also had a stale clock, which was synchronized but was not the root cause. See `tests/hardware/OT-007A-2026-08-08.md`.

## Purchase-record references

- [Meshnology two-unit V4 GPS bundle, ASIN B0FS1WQWKF](https://www.amazon.com/dp/B0FS1WQWKF)
- [Heltec WiFi LoRa 32 V4 family](https://heltec.org/project/wifi-lora-32-v4/)
- [SenseCAP Solar Node P1-Pro purchase listing, ASIN B0FMDHBWX8](https://www.amazon.com/dp/B0FMDHBWX8)
- [Seeed SenseCAP Solar Node P1-Pro for MeshCore, SKU 100023690](https://www.seeedstudio.com/SenseCAP-Solar-Node-P1-Pro-for-Meshcore-p-6741.html)

## Three-node repeater bench proof

With `OT-DEV-003` running as a repeater on the matching USA/Canada radio
settings, both Heltec companions received its advert and successfully requested
its synchronized clock over LoRa. A separate temporary private-channel run sent
one message in each direction between the Heltecs. Both messages delivered with
no loss or duplicates, and the SenseCAP counters changed by exactly +2 flood RX
and +2 flood TX, proving that it received and retransmitted both flood packets.
The private channel was erased from both companions and verified empty by the
test harness. This initial flood sample is bounded close-range forwarding
evidence, not by itself forced-path or field-range evidence; the Heltecs were
also within direct radio range.

A follow-up used explicit one-hop companion contact paths through the SenseCAP.
Positive direct-message tests succeeded in both directions and produced exact
message/acknowledgement direct RX/TX deltas at the repeater. With repeating
temporarily disabled, the repeater heard but did not retransmit the same routed
packet, the sender received no acknowledgement, and the destination received no
message. Repeating was restored and verified on; temporary paths and peer
contacts were removed. This proves the logical repeater path without claiming
physical isolation or range.
