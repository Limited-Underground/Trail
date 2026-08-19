# First-Release Capacity Policy v0

Status: approved release-planning boundary, updated 2026-08-12. This is not a
current device-support, delivery, range, collision, security, regulatory, or
production-reliability claim.

> Historical/superseded current-release scope: Decision 0033 preserves this v0
> policy as accepted planning history but supersedes its four/four-plus/eight
> evidence sequence and first-release repeater claim. Current V1 is the separate
> two-Heltec/two-Android direct-LoRa Companion topology; V1.5 is the separate
> unmeasured four-supported-node interoperability gate.

## Planned v0 boundary

The first release is intended to support:

- one group;
- at most eight active client devices; and
- at most one optional authorized repeater.

The repeater does not count as a client. A client must remain useful without a
repeater, server, internet connection, phone, laptop, or vehicle connection.
Those may become optional preparation, logging, recovery, or integration tools,
but cannot be required to keep the base group operating.

## Evidence sequence

Support must be earned in order:

1. four identical standalone clients, no repeater;
2. four identical clients plus one authorized repeater; and
3. eight identical clients plus one authorized repeater.

Each phase requires frozen hardware, firmware, radio/profile configuration,
traffic cadence, evidence format, and pass/fail gates. A failed or materially
changed phase is repeated before progression. Runs should cover materially
different broad environments; one good location does not become a universal
range claim.

## Meaning of support

A phase can be called supported only after its public aggregate evidence shows:

- required traffic was attempted and reconciled by class;
- no false delivery success or critical-alert loss;
- no visible duplicate, queue overflow, or device reset;
- latency, stale-state, GNSS, and battery gates were evaluated;
- repeater forwarding and failure behavior were explicit when present;
- privacy-safe logs passed the published validators; and
- recovery and cleanup completed under the frozen procedure.

Host capacity, fixed array sizes, airtime estimates, close-bench delivery, or a
single successful field session cannot independently establish support.

## Change control

Changing the public support ceiling or allowing more repeaters requires a new
policy version, load analysis, security/recovery review, and staged physical
evidence. It is not a documentation-only change.
