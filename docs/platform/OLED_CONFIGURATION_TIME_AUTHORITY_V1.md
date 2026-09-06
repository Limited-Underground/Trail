# OLED configuration and time authority V1

Status: accepted implementation contract, 2026-09-06; not a deployed protocol
or tested configuration/time integration. Work items OT-170/OT-172/OT-178.
See [Decision 0106](../decisions/0106-oled-configuration-time-authority.md).

## Purpose

Connect the OLED to confirmed device state without turning phone drafts, BLE
connectivity or display fields into configuration authority. This contract
defines the boundary for the next host implementation. It allocates no wire
kind, capability bit, UUID, persistent schema or storage namespace.

The published target still shows region required during ordinary operation.
It has no LoRa transmitter implementation and no live settings or time source.
Its startup logo, pairing, failure and reset paths remain authoritative.
No hardware, phone settings, installed image or radio behavior changes here.

## Reuse and compatibility

| Existing boundary | Reuse and constraint |
| --- | --- |
| [Companion protocol](../../firmware/components/companion/include/opentrail/companion_protocol.hpp) | Normal 0.0, 20-byte envelope, up to 128 payload bytes, normal MTU 151. Unknown versions/kinds/capabilities continue to reject. |
| [Authorization](../../firmware/components/companion/include/opentrail/companion_gatt_authorization.hpp) | Restricted 0.1 claim phase is not a settings extension; its lower MTU does not admit normal traffic. |
| [Coordinator](COMPANION_REQUEST_COORDINATOR_V0.md) | Current concrete request/response buffers are 40/52 bytes. Single-fragment requests, exact session/exchange correlation, preparation before commit and exact duplicate replay remain required. |
| [Configuration storage](../persistence/PERSISTENT_CONFIGURATION_V0.md) | Existing OTCF schema 2/two 64-byte slots do not contain device names, radio region or group policy. CRC/generation is not authenticated storage or cryptographic rollback protection. |
| [Setup model](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1SetupProfile.kt) | Local progress and receipt types are not proof of a firmware write. The published name transition remains local; region support currently contains US915 only. |
| [Clock](../../firmware/components/time/include/opentrail/oled_clock.hpp) | Presentation-only local second-of-day, 12/24 format, device-monotonic elapsed time and strict 24-hour expiry. The boolean authorization parameter does not establish authority. |

A separate unpublished OT-170 name-payload draft already exists in the owner
workspace. Preserve and review that work before importing a codec; do not
create a competing name format. Its proposed 16-byte header plus up to 96 UTF-8
bytes fits the generic payload ceiling but exceeds the current coordinator's
request buffer once wrapped. Payload compatibility alone is not activation.
The draft's payload discriminators are not allocated companion frame kinds.
It is not a dependency or an accepted wire implementation in this checkpoint.

Before transport activation, separately freeze a negotiated successor with
matched Android/C++ codecs, request/response capacities, pending-work bounds,
correlation and result handling. Old peers must remain on the old contract or
report unsupported; never reinterpret existing action codes or reserved bits.
Configuration/time must not use the provisional claim channel, simulator bridge,
Write Without Response, advertisements or the display snapshot as transport.

## Authority and serialized ownership

The trusted adapter derives an opaque context from the current authenticated,
encrypted, application-authorized owner session. It includes device/runtime
identity, authorization generation, connection/transport generation, session
nonce and exchange identity. These are compared within their existing scopes;
do not merge distinct generations, derive them from a display name or accept a
phone-supplied authorization flag. Keep identifiers out of public logs.

One application-task owner admits bounded immutable events and produces one
coherent presentation snapshot. NimBLE callbacks only perform bounded admission
and queue/copy work; no storage or OLED I/O occurs there. Copy names into owned
bounded storage before queuing. Borrowed string views may exist only while the
synchronous renderer consumes the owner-held snapshot.

Revalidate the exact context when applying queued work and immediately before
durable commit. Disconnect, owner revocation, reset, generation replacement,
nonce/exchange exhaustion and timeout invalidate pending work. Old callbacks
cannot publish into a later connection even when it belongs to the same phone.
An admitted queue entry does not mean applied, Ready or transmitted. Capacity
and response-encoding admission precede mutation; queue-full rejects unchanged.

For a bounded host foundation, use one pending operation, with no heap growth,
and explicit completion/cancellation. This is an implementation limit, not a new
wire guarantee. Durable commit versus revocation must have a serialized ordering:
revocation first prevents commit; a commit completed first remains committed,
but its late response cannot claim authority in a replacement session.

## Device-owned configuration

| Field | Source and acceptance rule |
| --- | --- |
| Device name | Exact validated value plus durable revision from device commit/readback. Phone draft and public person name are separate. Preserve the existing draft's strict UTF-8, 32 UTF-16-unit/96-byte limits during future codec review; OLED fallback/truncation never changes stored identity or value. |
| Region | Explicit user selection, supported target profile, durable commit and exact readback. No locale, GNSS, default or display label establishes region. Unknown/unsupported profiles reject without mutation. US915 is the only current app enum, not a new regulatory approval. |
| Radio TX availability | Separate device radio admission after valid region and all actual prerequisites. A configured region alone cannot enable TX. Missing driver or unknown admission keeps TX disabled even if the normal page becomes available. |
| Group and location policy | Confirmed device membership/policy state, including its revision and actor authority. BLE ownership does not imply group administrator. No group mutation through a generic owner-settings write. Unknown group state is distinct from confirmed no membership. |
| Phone Ready | Current owner-authorized normal session after its coherent Snapshot gate. Raw BLE connected, bonded or previously Ready is insufficient. |
| Battery, GNSS and activity | Typed device observations with their own validity and device-local age. Configuration/time input cannot manufacture metrics or radio activity; do not decode the old footer pixels. |

Name/region writes use expected-revision comparison; revision exhaustion rejects
without wrapping. Preparation does not mutate or reserve durable state. Applied
requires exact commit and readback, not a generic action-admitted result or GATT
write acknowledgement. Rejections preserve prior confirmed state. An uncertain
commit produces no applied receipt and requires a fresh authoritative read;
never blindly retry a possibly committed write under a new exchange.

Retain the coordinator's exact duplicate semantics: a cached byte-identical
request replays its result without a second commit; changed bytes under the same
exchange conflict; terminal failures are not re-executed. After an ambiguous
disconnect, reconcile the current durable revision/value before issuing another
write. Phone onboarding advances only on a receipt bound to its exact current
device/session/request and value; local draft restoration cannot restore proof.

Confirmed device configuration may survive a phone disconnect. Reload it through
validated storage on boot; corruption or unsupported schema yields unavailable,
not defaults marked verified. Choose storage migration, capacity and integrity
requirements explicitly before a driver is added. Do not repurpose the owner
blob. Every new user-associated domain must join
[factory-reset erase and absence verification](DEVICE_FACTORY_RESET_V1.md),
including partial failure/power-loss reconciliation. Reset invalidates queued
work and the clock before further presentation.

## Presentation time synchronization

Time is owner-supplied civil time for a display, not trusted UTC, GPS time,
message ordering, authentication expiry or radio timing. No internet or server
is required. It is volatile and does not survive device reboot or ownership
erasure. Date/timezone identifiers are not stored by this first clock boundary.

1. After normal authorization and Snapshot/Ready, the device-side owner may
   issue one fresh, exactly correlated time challenge. Its identity is bound to
   the current context and is never reused within that context. Actual wire
   encoding and transport issuance belong to the later negotiated successor.
2. The phone captures local second-of-day (0..86399) and explicit 12/24-hour
   preference after receiving that challenge. It must discard a stale queued
   sample and capture again; phone time itself remains an owner assertion.
3. At application-task consumption, the device requires the matching pending
   challenge, still-current authority, valid fields and nondecreasing local
   time. Require device-local challenge age strictly below 2,000 ms. Equality,
   future issue time, no challenge, reuse or mismatched context rejects without
   replacing the last valid clock. This chosen bound is a design limit, not a
   measured BLE latency or clock-accuracy guarantee.
4. After all upstream checks, call `OledClock::synchronize` with both sync and
   now equal to that application-task's current device-monotonic tick. Never
   pass Android elapsed time, a phone timestamp or an earlier queued callback
   tick as the clock epoch. The challenge bound limits accepted transport/queue
   age; no unmeasured transit correction is added to the civil-time sample.
5. Mark the challenge consumed once. A retransmitted response cannot resynchronize
   or extend the expiry; duplicate handling returns the previous disposition.
   If an exact result was lost, reconcile rather than guessing success.
6. Obtain `clock.observe(now)` for each coherent rendered snapshot. Do not reuse
   a formerly valid `OledClockReading` without observing current age. Validity
   expires at 86,400,000 ms, including equality, and rollback invalidates it.

The two clocks have different epochs. A 2,000-ms local challenge deadline bounds
device-observed elapsed time; it does not prove the phone set its clock correctly
or provide subsecond accuracy. In particular, old phone samples that falsely
claim freshness cannot be detected solely from civil time. The authorized phone
is trusted for the supplied display value, never for device-monotonic age.

Rejected external data is filtered before calling the clock: its existing
`synchronize` method invalidates on bad input, so calling it for a stale or
unauthorized callback would incorrectly erase a still-valid display. Local
clock rollback/expiry and reset are independent invalidation events and must
still be observed even when incoming data is rejected. The target display's
existing permanent rollback containment must not be bypassed by a new sync.

Ordinary disconnect closes pending challenges but retains a still-valid clock
and confirmed device settings. Revocation, reset and device restart clear time;
reconnect must regain normal Ready before requesting fresh synchronization.
Request a new challenge after Ready and phone civil-time, timezone or format
changes. Periodic refresh scheduling is a later transport decision; it must not
extend validity without a newly accepted sample. Offline time may continue only
until the existing 24-hour limit, with possible drift explicitly unmeasured.

## Acceptance matrix for implementation

These are required tests, not results claimed by this document.

| ID | Trigger | Required observation |
| --- | --- | --- |
| CT-01 | Raw BLE/bond/claim state or unnegotiated operation | No configuration/time admission; old protocol unchanged. |
| CT-02 | Queue callback from wrong device, owner, connection generation or session | No publication or mutation; current context remains intact. |
| CT-03 | Full queue, small output buffer, failed encoding, exhausted identity | Reject before mutation; no wrap or unbounded allocation. |
| CT-04 | Exact duplicate versus changed bytes at same exchange | One commit/sync at most; cached replay versus conflict. |
| CT-05 | Wrong expected revision, failed readback or uncertain commit | No applied receipt; reconcile durable state before another write. |
| CT-06 | Revocation/reset races with preparation/commit/completion | Serialized ordering; stale completion never updates a later session. |
| CT-07 | Phone draft, wrong value or stale setup receipt | No device-confirmed onboarding progression. |
| CT-08 | Unsupported/missing region; valid region with no radio driver | Region-required or normal/TX-disabled as appropriate; no radio authorization inferred. |
| CT-09 | BLE owner asks for admin mutation; unknown versus empty group | Enforce separate group authority; preserve unknown state. |
| CT-10 | Time challenge age 1999/2000 ms; future issue time; invalid fields | Accept only valid current case; rejection preserves prior clock. |
| CT-11 | Device renders between challenge issue and response consumption | Use current device tick; valid response is not rejected as an old clock callback. |
| CT-12 | Phone/device monotonic epochs differ; civil time or format changes | Never compare epochs; fresh current challenge permits legitimate civil-time correction. |
| CT-13 | Disconnect, lost result, exact response replay, reconnect | Keep valid clock; cancel pending old work; duplicate cannot refresh age. |
| CT-14 | Midnight, 12/24 boundaries, 24-hour equality, local rollback | Correct formatting/wrap; expiry or containment yields unknown. |
| CT-15 | Boot, storage corruption, reset interruption | No fabricated configuration/time; every new storage domain participates in verified reset. |
| CT-16 | Name lifetime, invalid UTF-8, max sizes, reset/pairing interruptions | Owned bounded data; exact storage distinct from glyph fallback; exclusive safety display preserved. |

## Ordered increments

1. Implement a host-only fixed-memory time-admission owner using fake trusted
   context/challenge inputs and the existing clock. Prove CT-02/03/04/06/10–14
   before transport or target wiring. Do not add a caller-controlled boolean
   as the only authorization mechanism.
2. Reconcile and import the existing name draft through focused cross-language
   codec review; define the negotiated transport successor, capacities and
   readback/uncertainty behavior before enabling either side. Keep region and
   group authorities explicit rather than bundling an unbounded settings API.
3. Implement device persistence/reset coverage and typed configuration sources;
   then bind application-task events and OLED snapshots with firmware preflight,
   affected host/Android validation and two reproducible target builds.
4. Separately verify installation/recovery prerequisites and accept the exact
   artifact physically, including runtime stack margin, display priority and
   reconnect. Website and cold-power work remain owner-deferred.

## Review and validation record - 2026-09-06

Independent source-boundary and lifecycle reviews found no blocking conflict.
The chosen challenge bound is explicitly unmeasured; implementation tests above
remain outstanding. Local document links and canonical progress/history checks
pass. The preceding adapter commit eb13a824a77d6b0b609147ff886c4e583efb60a1
passed [GitHub Host validation](https://github.com/Limited-Underground/Trail/actions/runs/34032570456).
That result belongs to the adapter, not an implementation of this new contract.

Only documentation changes in this increment. No firmware-porting gate is
activated by a target modification, because no target is modified. No host,
Android or firmware matrix is repeated for document-only changes; publication
safety and document/progress consistency are the affected checks. Hardware
execution, storage migration, new protocol activation and time admission tests
remain separate future gates.
