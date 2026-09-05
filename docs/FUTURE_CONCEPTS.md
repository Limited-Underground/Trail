# OpenTrail Future Concepts

Status: durable register for ideas and accepted directions that are not active
release commitments.

## Register policy

Entries in this register are future options, not release commitments,
implementation evidence, support claims, schedules, or progress credit.
Promotion requires a separate current decision, active work item, threat and
regulatory review, implementation plan, and acceptance evidence. Nothing in
this register changes V1, V1.5, or V2 scope or completion.

Every concept uses these fields:

- **Name**
- **Summary**
- **Status:** `idea`, `accepted direction`, `deferred`, or `scheduled`
- **Earliest eligible milestone**
- **Dependencies**
- **Safety, privacy, and security boundaries**
- **Schedule and progress boundary**
- **Decision and evidence links**

`Accepted direction` means the owner accepts the product boundary for future
planning. It does not mean the concept is scheduled, implemented, available,
supported, or physically accepted.

## Optional client/repeater mode on user devices

- **Name:** Optional client/repeater mode on supported user devices
- **Summary:** Owner reaffirmed on 2026-09-04 that a user's device should be
  selectable as a repeater while retaining its normal client functions, rather
  than requiring a dedicated repeater product. Aim for every supported user
  device where its hardware and validated firmware can support both roles.
  Strongly recommend external power or a suitably sized battery backup for
  sustained relay use; do not prescribe capacity or runtime before measurement.
- **Status:** accepted direction
- **Earliest eligible milestone:** Priority post-V1 candidate, assessed for
  V1.5 alongside supported-node interoperability. The owner is open to V1 if
  inexpensive, but no estimate or V1 scope expansion is accepted here. V1
  inclusion would require an explicit successor to Decision 0033; this is not
  automatically deferred until after V2.
- **Dependencies:** Reviewed secure forwarding construction, explicit opt-in
  role control, bounded forwarding and duplicate/replay suppression, airtime
  limits, fair client/relay scheduling, and measured power consumption.
- **Safety, privacy, and security boundaries:** No unrestricted flooding or
  weakening of endpoint authentication/encryption. Normal client use must
  survive relay disablement, power loss, and relay departure. Define battery
  warnings and low-power behavior from measured hardware evidence; do not
  claim an unmeasured universal battery threshold or range improvement.
- **Schedule and progress boundary:** Unscheduled enhancement direction, not
  implemented or validated and no completion credit. Acceptance needs a real
  three-radio sender-relay-receiver path with relay-disabled negative control,
  simultaneous client traffic, packet/loss/duplicate/latency/airtime accounting,
  and power-loss, restart, and battery operation tests on each claimed target.
- **Decision and evidence links:** Owner request 2026-09-04;
  [existing node roles](ARCHITECTURE.md#proposed-node-roles);
  [Decision 0033](decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md).
  Historical forwarding experiments are not acceptance of this combined mode.

## Logo-first idle display and button-driven status pages

- **Name:** Logo-first idle display and button-driven status pages
- **Summary:** After a successful boot, retain the existing full-screen Trail
  logo instead of automatically replacing it with live status. A short press
  of the designated navigation button or input while awake leaves the logo and
  enters the first information page; later short presses cycle the defined
  information pages. If the display is asleep, the first press wakes it while
  preserving the current page, and only a later press navigates. Runtime status
  continues updating while hidden and must not force a page change.
- **Status:** accepted direction
- **Earliest eligible milestone:** Immediately after V1 is fully functional and
  accepted. This is the first post-V1 enhancement and is not a V1 acceptance
  gate.
- **Dependencies:** Select and physically validate the navigation input; define
  the page order and display sleep/wake policy; preserve the existing full-screen
  startup-logo and live-status contracts; distinguish short navigation presses
  from the existing at-least-3,000-millisecond pairing hold/release gesture; and
  add host and selected-target tests for boot, wake-only, navigation, and failure
  precedence.
- **Safety, privacy, and security boundaries:** Boot self-check and runtime
  failure pages override the idle logo. Display or input failure cannot control
  BLE or radio state. No secret or private device identifier may be displayed.
  Pairing remains a separate deliberate hold gesture with higher priority than
  short-press navigation.
- **Schedule and progress boundary:** There is no implementation or delivery
  schedule, support claim, or V1, V1.5, or V2 progress credit. No public website
  status change is required merely for recording this direction.
- **Decision and evidence links:** Owner direction recorded 2026-08-27. There is
  no implementation or physical acceptance evidence yet; assign an `OT-###`
  work item only when the enhancement is promoted into active implementation.

## Provisioning-independent public lane and Public Assistance Broadcast

- **Name:** Provisioning-independent public lane and Public Assistance
  Broadcast
- **Summary:** A future device without a private group could use one default
  region-specific public rendezvous lane for ordinary public messages such as
  “Is anyone nearby?” A device with private groups could independently service
  both its authenticated private traffic and the public lane. These are
  parallel logical lanes: one LoRa radio must time-share service and cannot
  literally transmit or listen on two different radio profiles at once. Public
  Assistance Broadcast would be a separately configurable, optional public
  safety aid using compact predefined assistance codes rather than complete
  sentences. Receiving firmware translates those compact codes into localized
  text.
- **Status:** accepted direction
- **Earliest eligible milestone:** Only after V2 is fully functional and
  accepted. This concept is deferred to that milestone and is not scheduled.
- **Dependencies:** A separate reviewed public-lane architecture and versioned
  packet; a localized assistance catalog; a selected region-specific
  rendezvous profile and measured radio scheduler; fresh-position validation;
  deliberate and accessible confirmation UX; source-continuity, privacy, and
  abuse threat models; regional airtime, frequency, EIRP, dwell-time, antenna,
  and other regulatory review; and physical collision, missed-window, range,
  power, and expiry acceptance on the selected hardware.
- **Safety, privacy, and security boundaries:** The detailed boundaries below
  are mandatory inputs to any later design.
- **Schedule and progress boundary:** There is no promised version, schedule,
  or delivery date. This concept earns no V1, V1.5, or V2 progress credit. V1
  remains exact 43.75% and displayed 44%; Android remains 60%; V1.5 and V2
  remain unmeasured.
- **Decision and evidence links:** [Decision 0036](decisions/0036-post-v2-public-lane-and-assistance-direction.md)
  records the accepted direction. There is no implementation or physical
  evidence.

### Assistance semantics and location consent

Illustrative assistance meanings include lost or need assistance, medical
assistance, stranded, immediate danger, hazard, and resolved or cancelled.
Those meanings are direction only, not frozen numeric wire codes. Any future
design must at least represent a catalog version, assistance code, unique alert
ID, expiration, and, only when the user explicitly approves it for that
broadcast, a fresh GPS position with accuracy and fix age.

The device must explicitly say when location is unavailable or stale and must
never present old coordinates as current. Public Assistance Alerts are
configured independently from ordinary public chat. Sending requires a
deliberate hold or equivalent deliberate confirmation that shows exactly which
code and location will become public.

A private-group assistance action may offer to create a separate public
assistance packet only after explicit user confirmation. The public packet may
contain the required protocol metadata: catalog version, selected assistance
code, unique alert ID, and expiration, plus only the location approved for that
broadcast, including its required accuracy and fix age. Of the user content
derived from the private action, only the selected code and explicitly approved
location may become public. Private message text must never be automatically
decrypted, copied, summarized, or published to the public lane.

### Safety and receipt language

The interface and documentation must repeatedly make all of these limits
clear:

- Delivery is not guaranteed.
- Someone must be nearby, listening, compatible, and able to respond.
- This is not a monitored dispatch service.
- It does not replace 911, a personal locator beacon (PLB), a satellite
  messenger, cellular service, or another recognized emergency system.
- “Broadcasting” does not mean delivered.
- A device receipt means only that another compatible device heard the alert.
- The product must never display “help dispatched” unless a real external
  service explicitly supplies that evidence.

### Radio, abuse, and trust boundaries

The preferred direction is one region-specific public rendezvous profile, but
no profile is selected. If private groups use different radio profiles,
scheduled public-listening windows may miss alerts. Firmware and UI must not
imply continuous public monitoring when the radio is servicing another lane.

Any future design requires bounded repeats with randomized backoff, expiry,
duplicate suppression, regional airtime limits, and a correlated resolved or
cancelled broadcast. It must avoid automatic acknowledgements from every
receiver because simultaneous replies can collide. Rate limiting, sender
muting, stale-alert rejection, replay handling, and abuse controls are required.

Public packets are not confidential. Cryptographic integrity and source
continuity are desirable, but a valid device signature would prove neither a
person's identity nor that an assistance claim is truthful. Exact packet,
radio, security, privacy, abuse-prevention, regulatory, localization, UI, and
physical-acceptance designs remain future work.

### Non-reuse boundaries

The existing identity-free `OTQ0/v0` quick-status payload is CRC-only and lacks
an alert ID, expiration, location consent, source continuity, and public-lane
security. It must not be reused as the Public Assistance packet. Existing
`Need assistance` quick status remains a private group semantic and is not a
Public Assistance Broadcast.

Decision 0035 and `OTSL0/v0` freeze V1's exact pairwise-unicast secure-LoRa
semantics and grant no relay or broadcast authority. They must not be treated
as public-lane security. A public lane requires a separately reviewed
construction and version. Packet v0 must not carry real group, location, or
assistance traffic.
