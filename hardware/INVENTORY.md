# OpenTrail Hardware Inventory

Last updated: 2026-08-09

Compatibility states used here:

- `identified`: electronically or physically identified, but not yet validated for OpenTrail.
- `experimented`: used in a recorded, bounded experiment.
- `validated`: passed defined repeatable compatibility criteria.

No OpenTrail hardware is validated yet.

## Incoming candidate OT-CAND-004

The owner reports ordering a **Seeed Studio Wio Tracker L1 Pro for MeshCore**
with OLED, GNSS, antenna, enclosure, and built-in battery. It has not arrived
and has no OpenTrail evidence yet.

| Field | Candidate information | Evidence boundary |
| --- | --- | --- |
| Retail identity | Seeed's MeshCore product page currently lists `Wio Tracker L1 Pro for MeshCore`, SKU `100030144`, pre-flashed as Bluetooth Companion | The Amazon package label/revision must be inspected; a related Meshtastic datasheet uses SKU `114993649`, so the exact received SKU is unresolved |
| Controller/radio | Vendor datasheet identifies nRF52840 and Wio-SX1262, 862-930 MHz | Specifications only; no physical/electronic confirmation and not an ESP32 OpenTrail firmware target |
| GNSS/display/power | Vendor datasheet identifies L76K multi-constellation GNSS, 1.3-inch 128x64 OLED, USB-C, solar input, and a built-in 2000 mAh battery | Specifications only; fix performance, screen revision, charging, endurance, and solar behavior are untested |
| Intended first role | Preserve and evaluate shipping MeshCore Bluetooth Companion firmware; redacted USB/BLE/GNSS and Heltec interoperability checks | No erase or reflash is authorized merely to inventory the device; see `hardware/WIO_TRACKER_L1_PRO_BRINGUP.md` |
| Recovery candidate | Vendor documents double-`RST` UF2 entry as a `TRACKER L1` volume and single-`User` exit | Procedure only; entry/exit and bootloader recovery remain untested on the received unit |

This candidate does not change the statement that no OpenTrail hardware is on
a tested-compatible list.

## Device OT-DEV-001

Connected directly to the development laptop and queried read-only over native USB on 2026-08-08.

| Field | Verified result | Evidence/source |
| --- | --- | --- |
| Inventory state | Runtime model and installed firmware identified; the device was changed from BLE Companion to USB Companion and reconnected successfully; exact SKU/RF front end, installed antenna, and pinout remain unresolved | Windows USB/BLE enumeration, Espressif ROM/flash metadata queries, the MeshCore browser application, and the official MeshCore web flasher |
| Product/model | Heltec WiFi LoRa 32 **V4 OLED** (runtime-detected; not V4.3) | MeshCore Device Info reported `Heltec V4 OLED`; official MeshCore runtime code reports V4.3 separately when its KCT8103L power amplifier is detected |
| USB port during test | `COM3` | Windows Ports-class device enumeration; port assignment can change |
| USB identity | Espressif `VID 303A`, `PID 1001`; USB Serial/JTAG | Windows Plug-and-Play properties |
| MCU | ESP32-S3, QFN56, revision v0.2 | `esptool 5.3.1 chip-id` |
| CPU/features | Dual core plus LP core, up to 240 MHz, Wi-Fi, Bluetooth 5 LE | `esptool 5.3.1 chip-id` |
| PSRAM | 2 MB embedded PSRAM, AP_3v3 | `esptool 5.3.1 chip-id` |
| SPI flash | 16 MB, manufacturer ID `0x68`, device ID `0x4018`, quad I/O, 3.3 V | `esptool 5.3.1 flash-id` |
| Crystal | 40 MHz | `esptool 5.3.1 chip-id` |
| Base MAC | `8c:fd:49:b7:09:1c` | Espressif ROM query and USB parent identity |
| Secure Boot | Disabled | `esptool 5.3.1 get-security-info` |
| Flash encryption | Disabled | `esptool 5.3.1 get-security-info` |
| State during query | ROM USB/UART download bootloader | ROM serial banner and query connection |
| Normal application USB | Espressif application USB `VID 303A`, `PID 0002`, observed as `COM6` | Windows enumeration after normal boot; COM assignment can change |
| Installed application | MeshCore `v1.16.0-07a3ca9` | Compiled application strings read from the application metadata page |
| Firmware date reported by MeshCore | 06-Jun-2026 | Connected MeshCore browser application's Device Info |
| Installed MeshCore transport/role | **USB Companion** (current); BLE Companion was installed during the initial inventory | Official MeshCore web flasher selected Heltec v4 / Companion USB `v1.16.0`; the post-flash MeshCore app connected over Web Serial and displayed the fresh Device Setup wizard |
| USB Companion flash result | Successful clean flash using `heltec_v4_companion_radio_usb` `v1.16.0-07a3ca9`; erase enabled | Post-flash application boot and first-run Device Setup observed in the connected MeshCore browser app, 2026-08-08 |
| Framework/build | Arduino on ESP-IDF 4.4.7; build date March 5, 2024 | ESP application descriptor in OTA slot `app0` |
| Flash layout | NVS, OTA metadata, two 6.25 MB app slots, 3.38 MB SPIFFS, 64 KB coredump; `app1` currently empty | Partition table and application descriptor pages; NVS and SPIFFS contents were not read |
| Radio configuration snapshots | **Current user-applied USA/Canada preset:** 910.525 MHz, 62.5 kHz bandwidth, spreading factor 7, coding rate 5, transmit power 10 dBm (maximum reported 22 dBm). The clean-flash default had been 869.618 MHz, 62.5 kHz, SF8, CR5, 10 dBm. | Post-apply `meshcli infos`, 2026-08-08. Packet and airtime counters remained zero after verification. Preset selection records configuration but is not proof of antenna suitability, exact RF front end, or regulatory authorization. |
| Battery snapshots | 48%, 3.58 V before charging; 97%, 4.17 V after charging; 4.226 V after the USB Companion flash | Connected MeshCore browser telemetry and `meshcli get stats_core`, 2026-08-08; transient observations only |
| MeshCore capacity/status | 1/40 channels, 0/350 contacts, storage 0% used (0 KB of 3169 KB) | Connected MeshCore browser application's Device Info, 2026-08-08 |
| Post-flash USB runtime snapshot | USB serial connection successful; uptime 670 seconds; 0 errors; queue length 0; 0 packets sent/received; 0 transmit/receive airtime | `meshcli` read-only `stats_core`, `stats_radio`, and `stats_packets` queries on `COM6`, 2026-08-08 |
| Firmware-reported repeater frequency points | 433.000 MHz, 869.495 MHz, and 918.000 MHz | `meshcli get allowed_repeat_freq`, 2026-08-08; firmware-reported permitted repeater points, not proof of antenna suitability, exact RF front end, legal authorization, or full supported band |

The MAC address is device-specific. Avoid publishing it unnecessarily in public logs or screenshots.

### Still unresolved

- Exact commercial SKU and any V4 minor board revision not exposed by the runtime model
- Exact installed RF front end/full supported band; the official Heltec V4 family specification identifies an SX1262 and a nominal 863-928 MHz high-band variant, but this has not been electrically confirmed on this specific enclosed unit
- Installed antenna type and connector
- Exact OLED/display wiring, physical GNSS source/module, battery/charger details, sensors, and pin assignments
- Regional regulatory/legal operating constraints (the current USA/Canada preset is now recorded)

MeshCore's official runtime code reports `Heltec V4.3 OLED` when it detects the KCT8103L power amplifier and `Heltec V4 OLED` otherwise. This device reported `Heltec V4 OLED`, resolving that runtime distinction without opening the case. Radio silicon, full supported band, and full SKU may still require a label, purchase record, authoritative hardware record, or controlled radio inspection.

Official references used for family matching:

- [Heltec WiFi LoRa 32 V4 product specification](https://heltec.org/project/wifi-lora-32-v4/)
- [MeshCore Heltec V4 board target](https://github.com/meshcore-dev/MeshCore/blob/main/boards/heltec_v4.json)
- [MeshCore Heltec V4 runtime model detection](https://github.com/meshcore-dev/MeshCore/blob/main/variants/heltec_v4/HeltecV4Board.cpp)

## Development laptop tooling

- Windows enumerated the native USB Serial/JTAG interface using the Microsoft `usbser.inf` driver.
- `esptool 5.3.1` is installed for the current Windows user and is available as `python -m esptool`.
- `meshcore-cli 1.5.7`, `meshcore 2.3.8`, and `bleak 3.0.2` are installed for the current Windows user. BLE discovery works, but direct BLE CLI connections on this laptop failed in the Windows WinRT GATT address-resolution layer before a MeshCore command was sent. USB Companion is now installed so serial CLI validation can be performed after the browser releases the Web Serial port.
- USB Companion serial validation succeeded on `COM6`: MeshCLI connected, confirmed firmware `v1.16.0-07a3ca9`, and returned read-only configuration/runtime statistics. The browser and MeshCLI cannot own the Web Serial/COM port simultaneously.
- `tools/Test-MeshCoreUsbNodes.ps1` automatically discovers connected Espressif USB Companion ports and returns a redacted, read-only health snapshot (model/firmware, battery, radio settings, errors, queue, packet counts, and airtime). It deliberately omits node names, public keys, coordinates, PINs, and channel data. Run it only while MeshCore browser tabs are disconnected.
- `tools/meshcore_channel_lease.py` and the three-node soak harness add a non-secret crash-recovery journal for temporary private-channel tests. A real stopped-session recovery cleared and verified both Heltecs, and short traffic/soak smoke cycles passed with no loss, duplicates, or errors; the long-duration result is still pending in `tests/hardware/OT-009A-2026-08-09.md`.
- Windows pairing is required before the browser can open the encrypted MeshCore GATT service. A stale or malfunctioning laptop Bluetooth state can preserve the device record while hiding live advertisements; clearing the pairing and restarting the laptop Bluetooth radio restored discovery during this test.
- The initial inventory and BLE diagnostic pass performed no flash write or erase. The user then intentionally flashed the official Heltec v4 USB Companion `v1.16.0-07a3ca9` image with erase enabled; the old MeshCore identity/configuration was expected to be replaced.
- The MeshCore browser inspection was read-only: no settings were changed, and precise location/public-key data visible in the live application were intentionally not copied into this inventory.

## Device OT-DEV-002

Connected independently to the development laptop and queried read-only over USB on 2026-08-08 after the user installed USB Companion firmware and selected the USA/Canada preset.

| Field | Verified result | Evidence/source |
| --- | --- | --- |
| Inventory state | Runtime board/firmware identity, USB transport, active radio configuration, battery, and clean runtime statistics identified; low-level ROM/flash identity and physical SKU/RF/antenna/pinout remain unresolved | MeshCLI serial queries and Windows USB enumeration |
| Product/model | Heltec V4 OLED (runtime-detected) | `meshcli ver` |
| Application USB | Espressif application USB `VID 303A`, `PID 0002`, observed as `COM11`; assignment can change | Windows/pySerial enumeration |
| Installed transport/role | USB Companion | Successful MeshCLI serial connection after the user's clean USB Companion flash |
| Firmware | MeshCore `v1.16.0-07a3ca9`, firmware date 06-Jun-2026 | `meshcli ver` |
| Capacity/options | 350 contacts, 40 channels; repeat disabled; path-hash mode 0 | `meshcli ver` |
| Radio configuration | User-applied USA/Canada preset: 910.525 MHz, 62.5 kHz bandwidth, SF7, CR5, 10 dBm transmit power; maximum reported 22 dBm | Filtered `meshcli infos` query |
| Telemetry/contact defaults | Environment, location, and base telemetry disabled; manual contact addition disabled | Filtered `meshcli infos` query |
| Battery snapshot | 4.069 V | `meshcli get stats_core`; transient observation only |
| Runtime snapshot after USB recovery | Uptime 64 seconds; 0 errors; queue length 0; 0 packets sent/received; 0 transmit/receive airtime | `meshcli` core/radio/packet statistics |
| Firmware-reported repeater frequency points | 433.000 MHz, 869.495 MHz, and 918.000 MHz | `meshcli get allowed_repeat_freq`; not proof of antenna suitability, exact RF front end, legal authorization, or full supported band |
| Low-level ROM/flash query | Not obtained. The running application port does not implement the ESP32 ROM-reset handshake used by `esptool`; the read-only attempt failed before reaching the chip and temporarily required a USB cable reseat. MeshCore recovered normally and all post-recovery checks passed. | `esptool 5.3.1 chip-id` failure plus successful post-recovery MeshCLI verification |

Device-specific identity/public-key values returned by MeshCLI were deliberately excluded from this inventory.

### Still unresolved for OT-DEV-002

- Independent ROM-level MCU, PSRAM, flash, crystal, and security-fuse confirmation
- Exact commercial SKU/minor revision and installed RF front end/full supported band
- Installed antenna type/connector, physical GNSS source, battery/charger details, sensors, and pin assignments
- Regulatory constraints applicable to the selected preset and intended deployment

## Device OT-DEV-003

Connected to the development laptop and queried over its MeshCore repeater USB
console on 2026-08-08 after the user installed the official repeater firmware
and selected the USA region.

| Field | Verified result | Evidence/source |
| --- | --- | --- |
| Inventory state | Runtime board/firmware identity, USB interface, active radio configuration, battery, repeater role, and close-range forwarding behavior identified; exact commercial P1/P1 Pro SKU and physical internals remain unresolved | MeshCLI repeater console, USB enumeration, and the bounded OT-009 bench experiment |
| Runtime board | `Seeed SenseCap Solar` | Repeater `board` command |
| USB interface during test | `COM17`; USB `VID 2886`, `PID 0059` | pySerial enumeration; port assignment can change |
| Installed role and firmware | MeshCore Repeater `v1.16.0-07a3ca9`, build 06-Jun-2026 | Repeater `ver` command |
| Radio configuration | 910.5250244 MHz, 62.5 kHz bandwidth, SF7, CR5, 22 dBm transmit power; repeating enabled | Repeater `get radio`, `get tx`, and `get repeat` commands |
| Battery snapshot | 4.155 V while USB-connected | Repeater `stats-core`; transient observation only |
| Runtime health snapshot | Uptime 448 seconds, 0 core errors, empty queue, -110 dBm reported noise floor, and 0 receive errors | Repeater `stats-core`, `stats-radio`, and `stats-packets` |
| Clock | Fresh flash initially reported 15-May-2024; synchronized over USB to current UTC and verified remotely from both Heltec companions | Repeater `clock sync`; Companion `req_clock` from `COM6` and `COM11` |
| Companion discovery | Both Heltec companions independently stored the SenseCAP repeater advert | Redacted contact-list comparison on `COM6` and `COM11` |
| Close-range forwarding | One temporary private-channel message delivered in each Heltec direction with 0 loss, 0 duplicates, 240.8/276.9 ms latency, and 11.5/12.0 dB SNR; the repeater recorded exactly +2 flood RX/+2 flood TX. Explicit one-hop direct routes then succeeded in both directions (1,121 ms and 880 ms acknowledgement round trips), each with exactly +2 direct RX/+2 direct TX at the repeater. With repeat temporarily off, the repeater recorded +1 direct RX/+0 direct TX, the sender timed out, and the destination received no message. | `tests/hardware/OT-009-2026-08-08.md` |

The device-specific USB serial number, MeshCore public key, and node identity
were deliberately excluded from this inventory.

### Still unresolved for OT-DEV-003

- Exact SenseCAP commercial SKU (P1 versus P1 Pro), installed battery type,
  optional GPS presence, enclosure revision, and internal board revision
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

Both boards were connected simultaneously to the development laptop on 2026-08-08 and detected independently as `COM6` (`OT-DEV-001`) and `COM11` (`OT-DEV-002`). The redacted USB health check confirmed:

- Both report Heltec V4 OLED and MeshCore USB Companion `v1.16.0-07a3ca9`.
- Both use 910.525 MHz, 62.5 kHz bandwidth, SF7, CR5, and 10 dBm transmit power.
- Battery snapshots were approximately 4.21 V and 4.07 V.
- Both reported 0 errors, empty queues, 0 packets sent/received, 0 receive errors, and 0 transmit/receive airtime.

The user subsequently confirmed both LoRa antennas were attached. OT-007A establishes authenticated bidirectional application delivery. A temporary private-channel sample delivered 5/5 numbered messages in each direction with 0 loss, 0 duplicates, 233.3-247.2 ms observed latency, 11.25-12.25 dB SNR, zero receive/core errors, and empty queues. The temporary channel was erased and verified empty on both devices. Earlier application timeouts were harness false negatives caused by requiring the full MeshCore display text to equal the marker; MeshCore includes sender display text around channel messages. `OT-DEV-002` also had a stale clock, which was synchronized but was not the root cause. See `tests/hardware/OT-007A-2026-08-08.md`.

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
