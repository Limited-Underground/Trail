# Decision 0088: prepare the Noise XK radio-cost measurement

- Date: 2026-08-27
- Status: Accepted
- Scope: OT-005 / OT-152 host-only Noise XK radio-cost and bounded-retry preparation

## Decision

Freeze a host-only measurement contract for the accepted benchmark-only
`OTNXK0/v0` Noise XK adapter over the accepted OT-114 US915 direct-LoRa
profile. The three raw benchmark messages are 48, 48, and 64 bytes, for 160
radio-payload bytes in three single-frame transmissions. They do not acquire
Packet V1 framing, a production wire format, or separate OTA1 acknowledgement
frames.

At 915 MHz, BW125, SF7, CR4/5, explicit header, CRC enabled, LDRO disabled,
preamble 8, sync word `0x12`, and the 2 dBm command setpoint, the accepted LoRa
formula produces theoretical airtimes of 97,536, 97,536, and 118,016
microseconds, or 313,088 microseconds per successful handshake. These are
host-computed references, not physical `measured_airtime_us` evidence.

The later execution must reverse initiator/responder roles. Per role it must run
one baseline handshake and one bounded retry scenario. The retry scenario may
withhold message 2 once, then must abort, wipe temporary secrets, reject stale
attempt frames, create fresh attempt identity, and restart the entire handshake
once. The fixed response deadlines retain OT-113's accepted policy: exact
outbound plus expected-response airtime, 500 ms responder-turnaround bound, and
1,500 ms scheduling margin. This yields 2,196 ms for message 2 and 2,216 ms for
message 3.

## Consequences

- OT-152 is preparation only. It authorizes no device access, build, flash,
  radio transmission, key or entropy operation, benchmark execution, selection,
  Packet V1, or score credit.
- The future public receipt may contain only aggregate lengths, counts, timings,
  and outcomes. Raw handshake bytes, keys, identifiers, endpoints, and private
  traces remain private.
- Noise XK radio cost remains unresolved until physical execution supplies
  `handshake_total_wire_bytes`, `fragments`, `measured_airtime_us`, and
  `bounded_retry_result` for both role directions.
- OT-153 must freeze a new executable runner/firmware/restoration bundle and
  separately accept fresh explicit non-reusable two-node radio authority before
  any hardware action. Success or abort consumes that authority.
- V1 remains exact 43.75%, displayed 44%. No website status update is required;
  no percentage, field-readiness, support, release, or demonstrated product
  capability changed.

## Evidence

- [OT-152 evidence](../../tests/hardware/OT-152-2026-08-27.md)
- [Canonical preparation](../../tests/benchmarks/crypto/OT-152-OT005-LIBSODIUM-NOISE-XK-RADIO-COST-PREPARATION-V0.json)
- Validator: `tools/crypto_noise_xk_radio_cost_preparation.py`
- Adversarial suite: `tests/host/crypto_noise_xk_radio_cost_preparation_tests.py`
