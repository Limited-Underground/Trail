# Decision 0036: Post-V2 public lane and Public Assistance direction

- Status: Accepted direction; deferred and unscheduled
- Date: 2026-08-19
- Work item: OT-092
- Scope: Post-V2 future-options governance, public communication, and Public
  Assistance safety boundaries

## Context

“Parallel public and private messages” means independently serviced logical
lanes, not simultaneous use of two LoRa profiles. The owner wants a future
provisioning-independent public/default lane and an optional Public Assistance
Broadcast, while keeping V1, V1.5, and V2 delivery work and measurements
unchanged.

The direction affects future architecture and safety language, so it requires a
durable decision and future-concepts register. It does not justify a packet,
radio profile, implementation task, release promise, or progress award.

## Decision

OpenTrail accepts the product direction recorded in
[`FUTURE_CONCEPTS.md`](../FUTURE_CONCEPTS.md): after V2 is fully functional and
accepted, a future device may use a region-specific public rendezvous lane even
without a configured private group. Devices with private groups may service
both lanes independently, subject to explicit radio scheduling. One LoRa radio
time-shares those lanes and cannot literally listen or transmit on two
different profiles at once.

Ordinary public chat and Public Assistance Alerts remain independently
configurable. Public Assistance is an optional supplemental safety aid, not an
emergency-service replacement or monitored dispatch service. It uses compact,
predefined assistance codes that receiving firmware translates into localized
text. Exact wire values and encoding remain future work.

Any later packet design must represent a catalog version, assistance code,
unique alert ID, expiration, and, only with explicit per-broadcast approval, a
fresh GPS position with accuracy and fix age. Unavailable or stale location is
shown as unavailable or stale; old coordinates are never presented as current.
A deliberate hold or equivalent deliberate confirmation must show exactly
what code and location will become public.

A private-group assistance action may create a separate public packet only
after explicit user confirmation. That packet contains the required protocol
metadata: catalog version, selected assistance code, unique alert ID, and
expiration, plus only the location explicitly approved for that broadcast with
its accuracy and fix age. Of the user content derived from the private action,
only the selected code and approved location may become public.
Private message text must never be automatically decrypted, copied, summarized, or published to the public lane.

Delivery is not guaranteed. A nearby compatible device must be listening and
able to respond. Broadcasting is not delivery, and a device receipt means only
that another compatible device heard the alert. OpenTrail must never say “help
dispatched” without explicit evidence from a real external service. It does not
replace 911, a PLB, satellite messenger, cellular service, or another
recognized emergency system.

The future radio policy must use bounded repeats, randomized backoff, expiry,
duplicate suppression, correlated resolved or cancelled broadcasts, regional
airtime limits, and abuse controls. It must avoid automatic acknowledgement
from every receiver. Rate limiting, sender muting, stale-alert rejection,
replay handling, and clear missed-public-window behavior are mandatory design
inputs. A single region-specific rendezvous profile is preferred but not
selected; different private profiles and scheduled listening windows can miss
alerts.

Public packets are not confidential. Cryptographic integrity and source
continuity are desirable, but a valid device signature proves neither a
person's identity nor the truth of an assistance claim. Exact packet, radio,
security, privacy, abuse-prevention, localization, regulatory, and physical-
acceptance designs remain future work.

## Existing-boundary compatibility

Decision 0033 continues to define V1 as exact two-node pairwise direct LoRa.
Decision 0035 and `OTSL0/v0` grant no relay or broadcast authority and cannot be
reused as public-lane security. The future lane requires a separately reviewed
construction and version.

The identity-free `OTQ0/v0` quick-status payload is CRC-only and lacks alert
identity, expiry, location consent, source continuity, and public-lane security.
It cannot become the Public Assistance packet. Its `Need assistance` semantic
remains private group quick status. Packet v0 may not carry real group,
location, or assistance traffic. Experimental priority-queue timing and rate
values are not deployed public-lane or regulatory policy.

## Progress and authority

This planning decision is deferred until after fully functional and accepted
V2. It is not scheduled and has no promised version, date, or delivery.
It creates no packet, firmware, Android, radio, location, key, device, physical,
support, emergency-service, upload, or distribution evidence or authority.

It earns no V1, V1.5, or V2 progress credit. Android remains 60%; V1 remains
exact 43.75% and displayed 44%; V1.5 and V2 remain unmeasured. The current next
security checkpoint remains the exact OT-005 target benchmark followed by
explicit suite/library, handshake/KDF, and packet-v1 wire selection.
