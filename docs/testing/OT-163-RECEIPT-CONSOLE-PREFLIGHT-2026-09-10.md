# OT-163 receipt-console device preflight

Both intended Heltec WiFi LoRa 32 V4.2 bench roles passed the fresh ROM identity,
ESP32-S3/16 MB geometry, original application and protected-region checks on
2026-09-10, 13:56:41-13:58:08 UTC. Each verified original was reset successfully.
The [exact outcome](../../tests/hardware/OT-163-RECEIPT-CONSOLE-PREFLIGHT-5-2026-09-10.json)
records all region lengths and hashes. They match the previous independently
verified role baselines exactly. No candidate was installed and no radio ran.

The new private caller admits the complete 55-source
[receipt package](../../tests/benchmarks/crypto/OT-163-RECEIPT-CONSOLE-PACKAGE-2026-09-10.json)
before importing project tools, then reuses the pinned ROM/readback routines.
Nine focused mocked groups and fresh-process offline validation passed. Reset
requires a verified expected original image and erased tail; an unexpected image
leaves an explicit recovery state instead of booting unverified application bytes.

Each role's readback covered the 589,824-byte application/erased-tail span,
32,768-byte bootloader, 4,096-byte partition sector and 8,192-byte OTA region.
NVS was excluded from readback and writes. The preflight wrote no flash, issued no
execution grant and changed no phone, APK, bond or registry state. No hardware
process remains active after this preflight. OLED contents and phone Ready were
not independently observed. Cold-power disassembly remains deferred.

The [hardware plan](OT-163-RECEIPT-CONSOLE-HARDWARE-PACKAGE-2026-09-10.md) remains
a proposed single trial using the exact candidate at application offset 0x10000,
US915/915 MHz and 2 dBm, followed by both independent original restorations.
Current routes and identities must be checked again immediately before mutation.
A new caller/package-bound one-use authority and current antenna/setup confirmation
are still required for execution. This read/reset preflight cannot be replayed.

No complete benchmark, physical timeout cause, V1 completion credit, public website
status change or publication is established by these checks.
