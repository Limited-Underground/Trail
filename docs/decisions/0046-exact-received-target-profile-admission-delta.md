# Decision 0046: Accept the exact received target profile for OT-DEV-001

- Status: Accepted
- Date: 2026-08-20
- Work item: OT-103

## Decision

Accept strict append-only `OTRTPA0/v0`, raw SHA-256
`98cce120cadc1bddf5851f1480ae181488e17277ba0a2c8c8c38a70a062be105`,
as the bounded admission of exact `OTRTPE0/v0` received-target evidence for
`OT-DEV-001`, SHA-256
`517809caf31250d126cc3619f9d05386a92811a594dca0087d9acbf1b671147e`.

The accepted profile binds manufacturer `Heltec Automation`, commercial family
`WiFi LoRa 32 V4`, PCB/RF-variant model `HTIT-WB32LAF`, received revision
`V4.2`, ESP32-S3 / ESP32-S3R2 revision v0.2, a 40 MHz crystal, 16 MiB flash,
and 2 MiB PSRAM to the existing `heltec-v4-bench-candidate` / `OT-DEV-001`
evidence unit. The target remains experimental and unsupported.

## Evidence boundary

Five owner-supplied photos contribute one closure input and four corroborating
inputs. Only privacy-safe dimensions, byte counts, SHA-256 identities, and
bounded markings are retained; the raw photos, local paths, EXIF/location data,
and private device identifiers are not retained in the repository.

The official Heltec `WiFi LoRa 32 V4` V4.2.0 datasheet, SHA-256
`d284d4f01f9e801bb8407386cf50ee4d099ed3c3f5e9153683cb5819b53f7f4d`,
is referenced but not retained. Its Table 1.5 `868-928 MHz` range and Table
3.5.1 `863-928 MHz` range remain separate facts; they are not normalized or
reconciled. The owner-matched package literal `HF 863-928` is corroborating
evidence only, and no checkbox state is claimed.

Manufacturer documentation identifies the SX1262 family and the documented
high-band variant. Neither radio silicon/front end nor installed antenna is
electrically verified, and no legal region, direct-radio profile, regulatory
acceptance, compatibility, or hardware support follows.

## Readiness accounting

OT-094 and OT-097 remain immutable historical six-blocker records. OT-102
records the prior current four-blocker state. OT-103 closes only
`exact_received_target_profile_unresolved` and records three current unresolved
requirements:

1. final candidate build configuration;
2. ESP-IDF mbedTLS/PSA dependency lock plus API/configuration eligibility; and
3. direct-radio MTU, PHY, and region.

The two accepted crypto source anchors remain unchanged. Every API/configuration
and candidate-import registry remains empty. `OTCBR0` readiness remains blocked.

## Boundaries

No final candidate configuration, firmware change, device access, flash, radio
or key action, benchmark, candidate or suite selection, packet-v1 authority,
compatibility or regulatory determination, supported-hardware claim, continuing
authority, or score credit is granted.
