# OT-163 receipt-console two-device benchmark passed

## Accepted result

The exact `nxk-receipts-v1` candidate completed the unchanged two-cycle Noise XK
radio-cost benchmark on both Heltec WiFi LoRa 32 V4.2 / ESP32-S3 / 16 MB nodes.
The trial ran 2026-09-10 14:04:16-14:10:48 UTC, including sequential installation,
benchmark, both original restorations and independent postchecks. The
[sanitized outcome](../../tests/hardware/OT-163-RECEIPT-CONSOLE-LIVE-OUTCOME-5-2026-09-10.json)
contains the complete validated measurement, image bindings and region evidence.
The stored result passes the existing validator again and its canonical digest
matches the coordinator receipt. Independent review agrees.

| Measurement | Observed result |
|---|---|
| Transmitted frames accepted by the peer | 14; seven in each direction |
| Radio payload bytes | 736 |
| Handshakes | Two baseline and two bounded-retry; all four final successes |
| Intentional timeout cases | Two withheld-m2 cases; one retry each, both successful |
| Lost / duplicate / corrupt / unexpected | 0 / 0 / 0 / 0 |
| Summed TX command windows | 1,597,079 microseconds |
| Individual TX command windows | 107,993-128,869 microseconds |
| Summed theoretical radio airtime | 1,447,424 microseconds |

The result schema retains the name `measured_airtime_us`; those values are
TX command-window measurements, not isolated RF airtime or end-to-end phone
latency. Intentional withheld-message timeouts are not observed packet loss.
Both nodes were together on the bench with antennas attached, confirmed by the
owner immediately before execution. Exact separation was not measured.
Configuration: US915 / 915 MHz, SF7, 125 kHz bandwidth, coding rate 4/5, CRC,
explicit header, eight-symbol preamble, sync word 0x12 and 2 dBm command setpoint.
This is close-bench evidence, not field-range or regulatory acceptance.

## Console and recovery evidence

Both roles completed solicited readiness/restart handling and the full run. Each
retained 63 accepted receipt observations, with zero parser misses, read errors,
invalid reads, pending bytes or observation archive failures. Each role's seven
TX_RETURN and seven RX_REARM_RETURN checkpoints returned zero. Observed rearm
windows were 5,910-5,931 microseconds. Transport diagnostics retain a bounded
128-entry suffix, so they are not an unlimited event history.

The [exact package](../../tests/benchmarks/crypto/OT-163-RECEIPT-CONSOLE-PACKAGE-2026-09-10.json)
and [prior host/build evidence](OT-163-BOUNDED-RECEIPT-BATCH-2026-09-10.md) remained
unchanged. A fresh caller and one-use authority admitted this single run after
[fresh preflight](OT-163-RECEIPT-CONSOLE-PREFLIGHT-2026-09-10.md). Application-only
writes used offset 0x10000; no bootloader, partition, OTA or NVS writes occurred.

Each distinct original application was restored and readback-verified before
reset. Independent final reads matched each preflight exactly over the complete
589,824-byte application/erased-tail span, 32,768-byte bootloader, 4,096-byte
partition sector and 8,192-byte OTA region. Both final guarded resets passed.
The journal is restored, the grant is consumed, and no restore-only recovery
invocation was needed. Do not replay this trial. No hardware process remains
active. Phone Ready and OLED contents were not independently observed.

## Next gate

This closes the failed-run benchmark gate for this exact candidate and setup.
It does not establish one exclusive cause for the previous physical timeout.
Independently admit these radio-cost measurements with the retained crypto
comparison corpus, make the explicit library/suite/wire selection, then integrate
the authenticated product message path and its protected BLE phone endpoints.
No product phone-to-phone delivery or release readiness is claimed. Completion
values and weights remain unchanged; website status is unchanged. These records
are local and uncommitted, with no Git publication or deployment in this task.
