# Decision 0057: admit the second measurement node exact profile

- Status: Accepted
- Date: 2026-08-22
- Scope: OT-005 benchmark phase 0 only

## Decision

Accept the exact OT-119 privacy-safe USB observation receipt, second-node
received-target evidence, and append-only `OTRTPA1/v1` admission. The evidence
binds only `OT-DEV-002`, independently from the accepted `OT-DEV-001` profile,
to the Heltec Automation WiFi LoRa 32 V4 / `HTIT-WB32LAF` / received `V4.2`
documented-high-band profile.

One bounded, read-only, no-stub ROM session observed ESP32-S3 revision v0.2, a
40 MHz crystal, 16 MiB flash, and 2 MiB embedded PSRAM. The owner explicitly
associated the selected physical unit with a privacy-safe photo showing both
`HTIT-WB32LAF` and `V4.2`; the normal `ot_bench` heartbeat returned after the
transient ROM entry. Raw probe output, the transient serial port, MAC or chip
identifier, USB serial/hardware path, raw photo, local path, EXIF/location data,
and private device identity are not retained.

This independent profile admission completes phase 0 of the frozen `OTCBX1/v1`
procedure. Accepted source/API-configuration/candidate-import counts remain
`3/3/0`. Benchmark measurement remains false and blocked only by absent retained
candidate import/build admissions and absent fresh benchmark execution
authority.

## Evidence

- USB receipt raw/canonical SHA-256: `16e69159aed7b9e7d9304cd7cc16d25b7205fc9283ee7751f26b3b9580df5f7c` / `e89f3e027f695d88e764af01b1e032b360a23455a7122121833720d2fbf7adf7`
- Profile evidence raw/canonical SHA-256: `0e8a9862091f7a1c58630bb64fc9250bdb24bddfdf8c09856629dd7dc73255e1` / `7f470316d446cdc3be5a878580418c08bff628e703dd8f419aa5e83f9001d223`
- Admission raw/canonical SHA-256: `afd3d8b17f80c49560f9fad71e93703ef6d142ee538146fc5829b2a0799d0e36` / `0eff2d934891f36999bdafb2a14ffc755b258c19bccb96a5a8d96db06105a443`

The append-only registry now contains two independently evidenced target units,
while crypto source/API-configuration/import counts remain `3/3/0`.

## Boundaries

The profile is exact for the received evidence unit but does not establish the
installed radio front end, antenna, electrical band, EIRP, legal operating
region, regulatory acceptance, compatibility, validation, or support. No flash
content or raw eFuse content was read; no firmware, persistent device state,
radio, key/entropy state, candidate import/build, benchmark, suite/wire
selection, secure-LoRa implementation, Packet V1, or score changed.

Android remains 60%; V1 Companion remains exact 43.75% and displayed 44%; the
historical standalone baseline remains exact 31.75% and displayed 32%; V1.5 and
V2 remain unmeasured.
