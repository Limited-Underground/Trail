# I-012 · Trail Repeater US915 settings proposal

November 15, 2026. **Engineering review proposal only.** D-028 selects the US
and US915 for development; it does not select an authorized transmitter
configuration. Leave P1-Pro TX refused. The existing port requires explicit
receive settings and its physical submit path still refuses; this doc changes
no defaults, firmware, endpoint settings, or stage of acceptance.

| Setting | Candidate for later review | Reason and unresolved check |
| --- | --- | --- |
| Endpoint reference | 915.000 MHz, 125 kHz, SF7, CR 4/5, sync word 0x12, 8 preamble symbols, explicit header and CRC. | [Current Trail close-bench profile](https://github.com/Limited-Underground/Trail/blob/c486a42aed97852eac0d7d3826b1dd97340d01c5/firmware/targets/heltec_v4_bench/main/heltec_v4_lora.hpp). Use as a receive-only interoperability reference under separate authority. Existing endpoint evidence is not authorization for Repeater TX. |
| Channel and mode | Investigate **907.800 MHz** in the exact documented Wio-SX1262 digital transmission system (DTS) mode. | A center evaluated in the DTS exhibit and inside that grant range. Review the precise firmware modulation and measured 6 dB bandwidth rather than copying the fixed 125 kHz profile or assuming a nominal 500 kHz setting qualifies. Changing the endpoints to match would be separate shared scope. |
| Power | Start conducted engineering review at **+2 dBm** (about 1.6 mW); measured value, not an assumed chip setting or maximum. | Matches the level of the reference close-bench profile and avoids overdriving a short link. With a verified 2 dBi receiver-side-independent transmit antenna and negligible feedline loss, nominal EIRP would be about +4 dBm; that arithmetic does not prove compliance. The +22 dBm data-sheet scenario in the battery worksheet was for current estimation only. |
| Antenna | Prefer the supplied Seeed 868–915 MHz 2 dBi rubber rod and original harness, **only if confirmed authorized for the exact module/configuration**. No amplifier or substitute antenna proposed. | Confirm the received unit, antenna part/type, gain, cable, installation and grant conditions. Being stock, within the band, or lower gain is not sufficient by itself. |

The [Wio-SX1262 equipment grant](https://files.seeedstudio.com/Seeed_Certificate/documents_certificate/113010003-FCC.pdf)
for FCC ID Z4T-WIO-SX1262 lists 903.0–914.2 MHz and 0.092 W conducted in DTS
mode; the [companion grant and exhibits](https://fccid.io/Z4T-WIO-SX1262)
list 902.3–914.9 MHz for the distinct DSS (hopping) mode. Neither shown range
includes 915.000 MHz. The [DTS laboratory exhibit](https://fccid.io/Z4T-WIO-SX1262/Test-Report/Test-Report-DTS-7738781)
and [DSS laboratory exhibit](https://fccid.io/m/2b173efa2de97987cdf3f2a5dd432222adefcd55bb9513f094fb4a4692e86672.pdf)
describe separate test configurations; do not combine their evidence.
The [P1/P1-Pro declaration](https://files.seeedstudio.com/Seeed_Certificate/documents_certificate/114993643-FCC.pdf)
covers Subpart B, not the intentional transmit mode. Seeed's
[board specification](https://wiki.seeedstudio.com/meshtastic_solar_node/)
describes the stock radio and antenna; it does not establish that Trail's
firmware profile meets the exact product's applicable grant.

[47 CFR 15.247(a)](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.247)
distinguishes hopping from digital modulation; for the latter the measured
6 dB bandwidth must meet the 500 kHz minimum. A single fixed narrow channel
does not become a compliant hopping system at low power or with sparse traffic.
For context only, the Trail 125 kHz reference airtime calculation gives
399.616 ms for 255 bytes and 307.456 + 128.256 = 435.712 ms for the separate
F5 194-byte and 70-byte invitation parts. Cumulative occupancy, retries and
bursts by each transmitter must be checked on the eventual applicable profile;
these nominal values are **not** a permission, regulatory allowance, or prediction
for the proposed DTS mode. See the
[Semtech airtime tool](https://www.semtech.com/design-support/lora-calculator)
and [47 CFR 15.204(c)](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.204)
for the antenna constraint.

Before asking for separate permission to transmit:

1. Bind the exact received P1-Pro and endpoint revisions, module identifiers,
   OEM integration/exposure conditions (the cited grant specifies 20 cm
   separation and restrictions on co-location), antenna part and harness to
   applicable authorization or a qualified compliance review. Stop if uncertain.
2. Have the responsible RF engineer identify an authorized mode on all nodes
   and record the agreed config, including measured conducted power, bandwidth,
   PSD, band-edge and spurious emissions and, if applicable, hopping occupancy.
3. In a separately authorized session, confirm direct endpoint operation,
   then qualify a single-relay path only after protected relay admission,
   driver reconciliation and the required recovery evidence exist. Measure
   actual RX/TX and GNSS power before accepting battery or solar performance.

**Owner recommendation:** approve an RF engineering review of the conditional
907.800 MHz DTS / +2 dBm / verified stock-antenna candidate; retain TX refusal
and the direct reference until a joint profile is accepted. If no applicable
configuration is established, choose receive-only qualification first.
