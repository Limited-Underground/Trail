# Two-device radio evaluation target

This isolated Heltec V4.2 / ESP32-S3 candidate carries one independent signed
three-message handshake over SX1262 radio. USB provisions the evaluation trust
and compares the full transcript; each device requires its own fresh physical
button confirmation. It provides no product membership or application traffic.

Radio construction and application startup are inert. Only explicit
`OTPAIR1 RADIO` after accepted provisioning arms RF. Arm B before A. The fixed
profile is 915 MHz, 125 kHz bandwidth, SF7, coding rate4/5, configured2 dBm,
explicit headers, two-byte CRC and154-byte maximum frames. A permits two
transmissions; B permits one. There are no retries. Signed local deadlines,
checked stop and independent endpoint cleanup bound the attempt.

From the repository root, with the admitted local ESP-IDF v6.0.2 toolchain:

```powershell
.\tools\Build-PairEvaluation.ps1 -Mode radio -BuildName ot230-pair-radio-review-a
```

The build directory and its log must initially be absent. The build helper
selects this target explicitly; its default mode remains USB. A build never
authorizes device writes or radio transmission. Any physical proposal must bind
exact artifacts, both devices, antennas/profile and sequential full restoration.

See the [OT-234 validation report](../../../docs/testing/OT-234-PAIR-RADIO-TARGET-2026-09-14.md)
for current evidence and remaining gates, including the reused HAL's SDK-abort
limitation. The software checks and one approved two-node radio handshake passed;
both originals were independently restored and the user confirmed normal screens.
That trial authority is consumed. Product traffic, phone integration, range and
full V1 acceptance remain open.
