# Two-device USB evaluation target

This candidate binds one real independent handshake endpoint to isolated NVS,
USB Serial/JTAG, a physical OLED and GPIO0 confirmation. It hosts no synthetic
second peer, NimBLE host, phone application, LoRa transport or product membership
API. The existing Bluetooth controller is owned solely by the reviewed entropy
runtime; it is not an advertising or connection service.

The [OT-230 predecessor trial](../../../docs/testing/OT-230-TWO-DEVICE-BENCH-2026-09-14.md)
refused on both devices before comparison; both originals were restored. The
current `ot231-pair-diag-v1` revision has not been flashed. Its
[diagnostic validation](../../../docs/testing/OT-231-PAIR-STARTUP-DIAGNOSTICS-2026-09-14.md)
adds sticky first-failure OLED codes and read-only `OTPAIR1 DIAG` replies.
Successful physical handshake/button acceptance remains open. Provisioning trusts
the explicitly authorized USB operator; it is not product trust enrollment.

## Target and preflight

- Candidate board: Heltec WiFi LoRa32 V4.2 / ESP32-S3, 16 MB QIO flash at 80 MHz;
  no PSRAM assumption. Exact physical admission remains a separate gate.
- OLED uses the accepted V4 bench binding: SDA17/SCL18, reset21, address0x3C,
  Vext36 active low, 128x64 SSD1306 at400kHz. This build still needs actual display
  and button acceptance. GPIO0 is sampled with pull-up; only the session's fresh
  release/500ms hold/release gesture can confirm. Holding at boot never confirms.
- Partition CSV is byte-identical to the existing bench recovery layout:
  app0x10000; ordinary NVS0xd000/0x3000; protected regions are unchanged.
- Four isolated namespaces: `ot230_boot`, `ot230_role`, `ot230_tx`, `ot230_rx`.
  No broad erase, migration, factory reset or automatic cleanup exists. TX/RX
  must be blank; retained traffic state refuses fresh-only admission.
- ESP-IDF v6.0.2 and its admitted managed libsodium1.0.22/source helper are reused;
  project version is `ot230-pair-v1`, target explicitly esp32s3, main stack24576.
  Two fresh builds match the application BIN/ELF/map, bootloader, partition,
  initial OTA and SDK configuration. Final source/executable admission and actual
  stack headroom remain separate gates; the latter has not been measured.
- USB control uses the bounded `OTPAIR1` session protocol on the installed
  Serial/JTAG driver. Firmware and bootloader logging are disabled. Startup
  READY and explicit HELLO/READY identify application readiness; the host must
  reopen/reidentify after resets and keep boot/ROM output outside command frames.
  Input is LF-only printable ASCII, maximum700 bytes before LF. Partial records
  survive read timeouts; overflow/control input closes the session. Output768
  bytes maximum, writes100ms bounded. USB APIs do not reset the device.
- One app task owns session, NVS, display and raw button sampling. Its monotonic
  authority uses esp_timer and refuses regression. Session lifetime/expiry,
  callback reentry, malformed input and cleanup have initial 48-group host
  coverage; final matrix and actual target acceptance remain separate gates.
- The first valid HELLO or INIT starts the fixed 120-second session window;
  startup idle ticks do not. A can wait while B is sequentially flashed.
  Repeated HELLO cannot renew the window. Each signed invitation also enforces
  its own device-local 60-second expiry, including after local confirmation.
- No radio region, antenna test, GNSS, battery, phone/APK or BLE pairing gate is
  applicable because these functions are unavailable in this target. Radio,
  trust provisioning, peer confirmation and messaging are not claimed.

## Physical boundary

Follow `docs/firmware-porting-lessons.md` and the maintained exact-image operator.
No build authorizes flashing. Before any approved trial, verify exact identity,
write span, original full application/NVS captures and independent restoration;
never write both devices simultaneously. Display failure must refuse confirmation.
The local comparison displays eight hexadecimal transcript digits; this is a
bounded visual aid. The coordinator must compare the full authenticated transcript.
No host confirmation command exists. A local success does not establish remote
membership. Physical use, restoration and compatibility remain unvalidated here.

For the approved trial, compare the displayed A/B roles and matching eight-digit
codes only after both reviews appear. Release GPIO0, then freshly hold it for
500–3000ms and release before that device's remaining deadline; roughly one
second is suitable. Confirm each device independently. A hold longer than three
seconds after arming refuses. Require successful CLOSE cleanup and positive
closure of both passive USB handles, then independently restore and verify both
original applications/full NVS and protected spans before restarting originals.
