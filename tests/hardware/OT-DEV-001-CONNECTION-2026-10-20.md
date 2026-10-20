# OT-DEV-001 phone connection after app re-flash, 2026-10-20

OWNER-REPORTED observation, approximately 08:40 America/New_York (UTC-4). No serial log was captured; the bench firmware emits no serial lines on the pairing path. This note grants no physical, release or V1 acceptance.

| Item | Value |
|---|---|
| Device | OT-DEV-001, Heltec WiFi LoRa 32 V4, COM10 (USB-Serial-JTAG) |
| Firmware target | `heltec_v4_bench` |
| Phone | Samsung SM-N986U (Galaxy Note20 Ultra); adb serial omitted |
| PIN | Not recorded |

| Flashed application image | Bytes | SHA256 | Offset |
|---|---:|---|---:|
| `build/targets/heltec_v4_bench/opentrail_heltec_v4_bench.bin` | 589664 | `3705d4dc58c3882a12df1673af234d12abbe3999b1237c5f7f13fa35d1b044a4` | 0x10000 |

Written with esptool 5.3.1 `write-flash` to the application region only; the bootloader, partition table, NVS and other partitions were not written.

## Owner-reported observations

1. After a fresh boot the device displayed a 6-digit PIN and advertised the pairing service UUID ending `b7d1` with a 6-character complete local name (confirmed in nRF Connect).
2. The Trail Android app discovered the device.
3. The owner selected Authorize this phone and entered the PIN in the Android system pairing dialog.
4. The app showed "Device reported request accepted" and reported the device connected.

## Not established

- This is not OT-0238c retained/rekey acceptance and closes no OT-0238c item.
- No RF, radio-range or frequency-plan acceptance.
- The on-device application hash was not independently read back: the earlier application-partition readback failed with "Packet content transfer stopped". Write integrity rests only on esptool's built-in write verification.
