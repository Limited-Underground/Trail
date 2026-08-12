# OpenTrail Product Boundaries v0

Status: release-planning architecture, updated 2026-08-12. Capabilities remain
goals unless their linked host and physical acceptance evidence says otherwise.

## Base client

The first-release client is intended to be self-contained. Its required product
boundary includes:

- battery operation in a protective enclosure;
- local display and local input;
- GNSS-aware local/group state with explicit no-fix and stale behavior;
- offline group messaging, quick status, and critical-alert presentation;
- privacy-safe local diagnostic/evidence collection;
- group/security lifecycle and recovery behavior; and
- documented USB recovery.

During normal group operation it must not require a repeater, central server,
internet connection, phone, laptop, map display, vehicle connection, or cloud
account. GNSS loss must not stop messaging; it removes or visibly stales only
position-dependent behavior.

## Optional capability map

| Optional role | Adds | Allowed boundary data | Failure behavior |
| --- | --- | --- | --- |
| One authorized repeater | Coverage/forwarding for the staged topology | Authenticated bounded radio frames and forwarding metadata | Clients continue direct behavior; no false delivery success |
| Opt-in server/archive | Selected breadcrumb/message recovery, export, and deletion after an explicit start | Minimized authenticated records under an explicit retention policy | Local radio/client behavior continues; upload/retention becomes unavailable |
| OpenGauge vehicle source | Normalized vehicle warnings/values | Versioned normalized alerts only; never raw CAN/J1939 in OpenTrail | Vehicle alerts become unavailable; group/location/messaging continue |
| Offline map or larger display | Local map/context and richer presentation | Local verified map packages and semantic UI state; no map tiles over LoRa | Simpler local presentation remains; radio behavior continues |
| Post-session management | Preparation, evidence collection, update/recovery assistance | Explicit user-selected configuration, public-safe aggregate evidence, signed update artifacts when designed | Field operation continues without the management tool |

## Optional archive controls

A future archive is opt-in, not assumed. Its minimum design gates are:

- explicit local start and stop with visible state;
- no silent background enablement;
- data minimization and bounded retention;
- participant/group authorization;
- export and deletion;
- encrypted transport and protected storage;
- offline queue limits and visible sync failure;
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
