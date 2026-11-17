# I-012 · Limited Underground MeshTrail Repeater US915 settings proposal

Updated 2026-11-16 after the I-012 technical review. **Keep physical transmit
disabled.** Use 915.000 MHz / 125 kHz as the receive reference. Investigate
907.800 MHz DTS with both Heltec endpoints and the Repeater as one review.
D-028 sets the US/US915 development region and an accessible 10–15 ft
installation. D-098 sets the three-sunless-day battery planning target.

## Why the two profiles stay separate

The fixed Heltec close-bench profile used 915.000 MHz, BW125, SF7, CR 4/5,
sync 0x12, eight preamble symbols, explicit header, CRC on, LDRO off and a
+2 dBm driver setpoint. Decisions 0051–0053 establish the direct-radio
evidence scope. The command setpoint and historical deliveries do not
establish measured RF power or regulatory acceptance. Match effective IQ
and the packet options when verifying receive on the P1-Pro. [Heltec
source at the reviewed revision][heltec] · [Direct-radio evidence][d53]

Neither reviewed Wio-SX1262 grant covers 915.000 MHz. The reported ranges
are 902.3–914.9 MHz for DSS and 903.0–914.2 MHz for DTS, at 0.092 W conducted.
The agency rule requires actual synchronized hopping for FHSS or at least
500 kHz measured at 6 dB for DTS. The tested 907.800 MHz DTS center uses
LoRa, but the public exhibit omits enough driver settings to reproduce it.
Annex A.2 prints `0.645020` under a kHz heading against a 500 kHz minimum.
Confirm the units, original trace and full settings with the grantee or
its lab. A nominal 500 kHz command does not show compliance. The current
fixed BW125 endpoint profile cannot interoperate with a different center
and wider waveform. [Module grants][grant] · [DTS exhibit, pp. 6 and 27][dts] ·
[47 CFR §15.247(a), (b), (e)][r247]

A hopping design would need measured channel width, a compliant hopset,
synchronized receivers, and per-transmitter, per-frequency dwell evidence.
For US915 the FHSS rule limits occupancy to 400 ms per frequency in a
20 s window for widths below 250 kHz at 20 dB and in a 10 s window for
widths of 250–500 kHz. The reference PHY's nominal 255-byte airtime is
399.616 ms. Both F5 invitation frames total 435.712 ms when emitted by
the same transmitter on one frequency. Include bursts, retries and
uncertain attempts in any later accounting. F5 stays direct. Treat
§15.249 as a separate qualification option if DTS cannot be established.
[§15.247(a)(1), (f)–(g)][r247] · [§15.249][r249] · [Semtech airtime calculator][airtime]

## Settings checklist

| Setting | Disposition | Recommendation |
| --- | --- | --- |
| US region and deployment | Accept existing scope | Use US/US915 (D-028). Keep the power bench on an accessible surface. |
| Receive profile | Accept as reference | Use the exact 915.000 MHz / BW125 reference above, with normal IQ, for a separately authorized receive-only verification. |
| Physical transmit and relay admission | Keep disabled | Preserve the existing refusal. Use a separate authorized session for any deliberate RF test or relay admission. [§2.805][r805] |
| Common transmit mode and channel | Review candidate | Start the joint DTS review at 907.800 MHz. Match both endpoints and the Repeater only after the exact mode and each unit's authorization are established. [Module grants][grant] |
| Bandwidth, frequency error and emissions | Measure first | Obtain the source trace and verify 6 dB BW ≥500 kHz for DTS, PSD ≤8 dBm/3 kHz, frequency accuracy, band edges and spurious emissions on the intended setup. [§15.247(a)(2), (d)–(e)][r247] |
| SF, coding, preamble, sync, header, CRC, IQ and LDRO | Verify before use | Capture effective settings on all nodes for each candidate. Recompute airtime and verify the required wire lengths and rearm behavior. |
| TX power and PA path | Measure first | Review +2 dBm conducted (about 1.6 mW) in a qualified fixture as an initial measurement target. Verify the PA variant, output at the connector, uncertainty and link margin per board. [§15.247(b)][r247] · [Heltec variants][heltec-spec] |
| Antenna type, gain, polarization and mounting | Verify first | Identify the supplied 868–915 MHz rod and verify its 2 dBi nominal gain, patterns and equivalence to an authorized antenna type. The grant exhibits describe other antenna types. [Seeed specs][seeed] · [DTS exhibit][dts] · [§15.204(c)][r204] |
| Harness and cable loss | Measure first | Measure the original harness loss in dB at the candidate center and across the intended band. From power measured before that harness, estimate EIRP as conducted dBm plus gain in dBi minus loss in dB. |
| Host integration, separation and other radios | Verify first | Bind the FCC ID and host labels, OEM requirements, 20 cm grant separation, antenna routing and BLE/co-location conditions to the actual unit and custom firmware. The host declaration covers Subpart B. [Module grants][grant] · [Host declaration][sdoc] · [§15.212][r212] · [§2.1043][r1043] |
| Air occupancy, duty and error cleanup | Measure before choosing limits | Verify airtime per transmitter and channel in the selected mode. Include burst load, refusal, completion and rearm. Keep uncertain attempts from adding traffic. [§15.247(a), (f)–(g)][r247] |
| Continuous listening, GNSS and battery | Measure first | Use the receive-only session kit. Compare measured loads against the 30 and 120 forwards/hour planning cases for three sunless days. |

## Open owner questions

1. Authorize an owner-present receive-only bench after the exact nonemitting
   image or probe invocation, observable receive state and restore route
   are identified. Start with the P1-Pro and battery logger.
2. Confirm the logger and pack fixture, representative cold temperature,
   usable reserve and workload. Use 30 forwards/hour as the model baseline
   and 120/hour as sensitivity. Measure GNSS states when supported.
3. Authorize a joint engineering review with the endpoint owner and the
   grantee or TCB for the 907.800 MHz DTS candidate. Request the exact source
   recipe, corrected units, antenna and host conditions first.

[Receive-only power-session kit](REPEATER-RECEIVE-ONLY-POWER-SESSION.md)

[heltec]: https://github.com/Limited-Underground/Trail/blob/c486a42aed97852eac0d7d3826b1dd97340d01c5/firmware/targets/heltec_v4_bench/main/heltec_v4_lora.hpp
[heltec-spec]: https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v4/
[d53]: ../decisions/0053-admit-us915-direct-radio-profile-evidence-v1.md
[grant]: https://files.seeedstudio.com/Seeed_Certificate/documents_certificate/113010003-FCC.pdf
[dts]: https://device.report/m/8011a204c8c323a9783c49171b438f8e04d723a7ac3b3ab630357121d91e9ff3.pdf
[sdoc]: https://files.seeedstudio.com/Seeed_Certificate/documents_certificate/114993643-FCC.pdf
[seeed]: https://wiki.seeedstudio.com/meshtastic_solar_node/#specification
[r247]: https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/subject-group-ECFR2f2e5828339709e/section-15.247
[r249]: https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.249
[r204]: https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.204
[r212]: https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.212
[r1043]: https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-2/subpart-J/section-2.1043
[r805]: https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-2/subpart-I/section-2.805
[airtime]: https://www.semtech.com/design-support/lora-calculator
