# OpenTrail Product Boundaries v0

Status: release-planning architecture, updated 2026-08-19. Capabilities remain
goals unless their linked host and physical acceptance evidence says otherwise.

## Current V1 Companion boundary

Decision 0033 defines the first release as exactly two supported Heltec LoRa
devices and two approved Android phones, one physically authorized phone per
Heltec. Its required path is:

Phone A ⇄ BLE ⇄ Heltec A ⇄ direct LoRa ⇄ Heltec B ⇄ BLE ⇄ Phone B

Each Heltec remains authoritative for radio, security, queues, message and
acknowledgement identity, delivery outcomes, GNSS validity, and durable state.
Its one approved Android phone is the required V1 user interface. The two-pair
path must not depend on a repeater, relay, central server, internet connection,
laptop, map display, vehicle connection, or cloud account. GNSS loss must not
stop messaging; it removes or visibly stales only position-dependent behavior.

V1 requires a fresh six-digit PIN during one automatic 60-second pairing window
on verified unowned boot, saved-owner-only reconnect on an owned boot, and
separately authenticated/encrypted direct-LoRa messaging. There is no V1 phone-
replacement or lost-phone transfer path. The authorized app can request a
destructive factory reset after explicit in-app confirmation without a Heltec
confirmation; lost-phone recovery uses the local 10-second hold, warning,
release, and short-press confirmation sequence. Both paths erase all user data,
including maps and BLE bonds, before returning to unowned pairing. These are
current requirements, not completed capabilities. Factory reset, reflashing,
invasive access, or old-flash restoration may reset or roll back phone
authorization; V1 makes no rollback-proof ownership claim against that physical
attacker. See [Decision 0103](decisions/0103-adopt-ot168-v1-factory-reset-and-boot-pairing.md)
and the [current reset contract](platform/DEVICE_FACTORY_RESET_V1.md).

## Preserved standalone and later-node boundaries

The original self-contained touchscreen-client concept remains a future product
track with its own display, input, battery, GNSS, radio, recovery, and physical
acceptance obligations. Historical four-person standalone plans remain evidence
for that original scope but no longer define V1 Companion completion.

V1.5 is the separate unmeasured four-supported-node interoperability gate.
Mixed supported hardware is allowed and preferred but not required; four
identical supported nodes may pass, four phones are not required, and any relay
claim requires a physical three-radio sender-to-relay-to-receiver path.

## Working product-family forms

The owner-approved, provisional names in
[Decision 0008](decisions/0008-limited-underground-trail-working-product-family.md)
map onto these boundaries without changing the protocol:

| Working form | Required presentation boundary |
| --- | --- |
| `Limited Underground Trail Essential` | Screenless LoRa companion; one paired Android phone is required for normal user interaction, while authoritative radio/security/queue state remains on the device |
| `Limited Underground Trail Gold` | One local touchscreen; self-contained field operation without a phone or laptop |
| `Limited Underground Trail Platinum` | Two local displays; self-contained field operation without a phone or laptop |
| `Limited Underground Trail Repeater` | Optional repeater; never a base-client dependency |

`Limited Underground Trail` is both the Android application name and umbrella
family. `Limited Underground Firmware Loader` is shared maintenance tooling,
not a field client; it remains Preview/inspection-only until real firmware
writing and recovery are verified.

## Optional capability map

| Optional role | Adds | Allowed boundary data | Failure behavior |
| --- | --- | --- | --- |
| One authorized repeater | Coverage/forwarding for the staged topology | Authenticated bounded radio frames and forwarding metadata | Clients continue direct behavior; no false delivery success |
| Opt-in server/archive | Selected breadcrumb/message recovery, export, and deletion after an explicit start | Minimized authenticated records under an explicit retention policy | Local radio/client behavior continues; upload/retention becomes unavailable |
| OpenGauge vehicle source | Normalized vehicle warnings/values | Versioned normalized alerts only; never raw CAN/J1939 in OpenTrail | Vehicle alerts become unavailable; group/location/messaging continue |
| Offline map or larger display | Local map/context and richer presentation | Local verified map packages and semantic UI state; no map tiles over LoRa | Simpler local presentation remains; radio behavior continues |
| Post-session management | Preparation, evidence collection, update/recovery assistance | Explicit user-selected configuration, public-safe aggregate evidence, signed update artifacts when designed | Field operation continues without the management tool |

## Optional archive controls

The host-tested [client-side session boundary](location/BREADCRUMB_ARCHIVE_SESSION_V0.md)
now proves explicit start/stop, current-fix-only capture, one minimized canonical
record, monotonic same-boot session use, and fail-visible local transport
pressure. The separate [bounded RAM outbox](location/BREADCRUMB_ARCHIVE_OUTBOX_V0.md)
adds no-overwrite FIFO retention and exact-head removal only after a host fake's
durable-ack result. Neither implements the server/archive service. The remaining
minimum design gates are:

- target UI composition that preserves explicit start/stop and visible state;
- no silent background enablement or restart enablement;
- data minimization and bounded retention;
- participant/group authorization;
- export and deletion;
- encrypted transport and protected storage;
- target-validated queue sizing, optional protected persistence, and visible
  sync/discard/reconciliation controls;
- no public raw route, participant, credential, or device-identity disclosure;
  and
- base operation that survives server or internet failure.

The archive does not provide rescue guarantees and cannot turn missing radio or
GNSS evidence into a known location. Provider/account/DNS details, costs,
credentials, private routes, and participant records stay outside the public
repository.

## OpenGauge boundary

OpenGauge owns vehicle input, CAN/J1939 decoding, gauge rendering, and vehicle-
specific validation. OpenTrail accepts only the documented normalized,
versioned alert interface and adds its own group/location context. Neither
project requires the other for its base function.

## Change rule

An optional role becomes part of the base only through a new versioned decision
that updates hardware, firmware, recovery, privacy/security, field-test, and
support obligations. A website description alone cannot change this boundary.
