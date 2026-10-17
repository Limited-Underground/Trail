# Trail tracking: scope and development sequence

Date: 2026-09-07. Status: owner-authorized parallel server preparation;
tracker firmware and release scheduling remain deferred. No tracking, relay,
third-party compatibility, battery runtime, or server capability is claimed tested.

## Governing priorities

1. Continue the existing Trail communication firmware V1 unchanged. Tracking
   cannot add a V1 prerequisite, change its protocol, or delay its acceptance.
2. The server's first implementation priority is integration with the current
   Trail firmware and all capabilities actually available through its supported
   interfaces. Track unsupported and untested functions explicitly; do not equate
   source code or a server host being online with working integration.
3. Server-side communication integration and tracker preparation may proceed in
   parallel now, where feasible without delaying existing commitments. Shared
   server components may be designed and implemented for both device types;
   this permission is not limited to documentation. It does not authorize
   tracker firmware or additional current V1 firmware options.
4. Start tracker firmware only after Trail communication V1 is complete and
   accepted against its canonical gates. A successful build is not that gate.
5. Post-V1 eligibility is not a promise to precede already accepted enhancements.
   Reconcile the existing future-concepts order when scheduling actual work.
6. Future MeshCore, Meshtastic, and other integrations are tracking-only. They
   do not authorize cross-system messaging, control, or shared radio forwarding.

Owner clarification, 2026-09-07: this restriction applies to ALL additional
options in the Trail communication firmware, not just tracking. No new option,
feature flag, relay mode, tracker hook, or speculative server-facing extension
may be implemented before V1 is complete and accepted merely because it is easy
or might avoid future rework. The sole exception is a demonstrated necessity to
satisfy an already accepted V1 requirement. Document that requirement, the concrete
blocker, why existing behavior cannot satisfy it, and the smallest necessary
correction before implementation. Convenience, server expansion, and future
compatibility are not necessities. Existing V1 implementation and defect fixes
continue within their accepted scope; additional options wait until after V1.

See [Decision 0033](decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md)
and the [future-concepts register](FUTURE_CONCEPTS.md). This plan supplies no V1,
V1.5, or V2 completion credit and requires no website publication.

## Product modes and the no-local-log contract

| Mode | What the user can see | Location recording |
| --- | --- | --- |
| Local tracking without a server | Permitted trackers' current or most recently received position through a Trail communicator and its communications app | Latest position only; no location history or logging |
| Server-connected tracking | Permitted people/assets/devices on a shared map, with source and report age | Server history only when explicitly enabled under a defined retention policy |

Required local-mode wording:

> Local tracking shows only the current or most recently received location.
> It does not record location history. An old position does not show where the
> tracker is now. Updates require a working compatible radio path.

Local mode has no breadcrumb trail, route playback, historical export, background
coordinate log, or historical backfill when a server becomes available later.
Replacing the latest point is not permission to retain previous points. Phone,
communicator, tracker, diagnostics, analytics, and backups must not silently
create a local tracking history. Device configuration/ownership records are
separate from location logs. Operational counters can be retained without coordinates.

Decide before implementation whether the single latest point survives an app or
device restart. Either choice must preserve its original age and uncertainty;
reopening the app cannot make it fresh. The no-history requirement is fixed.

A server gateway may eventually buffer bounded reports for an explicitly enrolled
server deployment during an outage. That is a separate server-collection mode,
not a hidden feature of local tracking. Its retention, security, user disclosure,
and deletion rules must be agreed before implementation. Local mode never builds
an archive that is uploaded after the fact.

## Identities, permissions, and presentation

Keep four concepts separate: person/asset, physical reporting device, radio role,
and gateway. A person or asset may have multiple devices, such as a communicator
and a backpack tracker. Preserve each device's identity, source, age, and position;
group markers for convenience without hiding disagreeing or separated reports.

Use recognizable editable labels and stable internal identities. Temporary setup
labels, display names, Bluetooth addresses, and transport endpoints are not stable
ownership authority. Names and branding must remain replaceable.

The map distinguishes communicators, trackers, vehicles, and equipment, with last
observation age, GPS validity, and available power information. Missing data is
unknown, not zero. A new status packet or delayed upload cannot refresh an old GPS
position. Duplicate or out-of-order reports cannot move the current marker backward
without disclosure. Untrusted clock changes require explicit freshness uncertainty.

Define owner enrollment, viewing grants, local sharing without a server, revocation,
transfer/reset, and retirement. Radio reception alone grants no permission to
identify, display, manage, or archive a tracker. Server workspaces isolate families,
farms, parks, and teams. Tracking is a location aid, not guaranteed supervision or rescue.

## Radio topology and shared relays

Future direct path: tracker -> communicator/app, or tracker -> gateway -> server.
Future relayed path: tracker -> compatible relay(s) -> communicator/app or gateway.
A relay may eventually be a fixed repeater, communicator in client/repeater mode,
or tracker in tracker/repeater mode. Receiving or repeating radio packets does not
itself provide a server connection; a reachable gateway and its backhaul are needed.

Sharing Trail repeaters is an accepted design requirement, not proven compatibility.
Existing V1 direct-LoRa authentication must not be assumed to authorize relaying.
Freeze a reviewed versioned forwarding/security construction, role negotiation,
region/profile compatibility, forwarding limits, duplicate/replay suppression,
loop prevention, and scheduling before making this claim.

Relay participation is explicit and configurable. Fixed external power is a strong
candidate for sustained forwarding. Vehicle operation needs measured parked-power
limits; battery trackers and communicators must preserve their primary function.
Do not default every node into unrestricted repeating. Relay departure and loss of
power must not invalidate direct operation or imply that an unreachable device moved.

Prioritize communication and higher-priority traffic over routine tracking while
also bounding tracker starvation. Use compact reports, staggered schedules, bounded
retries, and motion/idle profiles where supported. Select intervals, device capacity,
hop limits, queue limits, and battery thresholds from measurements, not promises.

## Management responsibilities to validate

| Surface | Proposed responsibility | Required distinction |
| --- | --- | --- |
| Firmware loader | Identify supported hardware; verify and install the correct communicator/tracker image; recover interrupted installs | Installing firmware is separate from granting ownership |
| Communications phone app | Nearby tracker setup, claim/name, power/GPS checks, permitted current-location viewing and local settings | Works without a server; no local tracking history |
| Server web interface | Fleet/workspace membership, person/asset assignment, gateway health, current map, optional server history and remote settings | Phone/desktop responsive web UI; native server-management app is not initially required |

This division is a design proposal to validate, not a commitment to new UI code now.
Define how screenless trackers prove physical possession and how lost access is
recovered. Do not assume the communicator's screen/PIN workflow works on every tracker.
Remote configuration must separate requested, pending, device-applied, and verified
state. Offline devices cannot be presented as having applied a server preference.

## Server preparation and future adapters

Owner clarification: the server need not wait for tracker firmware to exist before
preparing communicator and tracker support together. Begin from the existing server
project's current state and coordinate with any active work there. This permission
does not automatically start another task, override deployment approvals, or move
server implementation into the OpenTrail repository.

Eligible parallel server work includes shared device/person/asset identities,
capability and role records, enrollment/permissions, gateway intake interfaces,
latest-location projection, optional history boundaries, and map presentation for
both device categories. Use the current communicator's actual supported interfaces;
keep future tracker contracts versioned and provisional until firmware acceptance.
Validate future paths with explicitly synthetic fixtures or contract tests, never
as physical tracker evidence. Hide or clearly mark unavailable tracker operations;
do not present simulated devices as real tracking. Missing future firmware must not
block working communicator integration or force firmware scope expansion.

The initial server capability matrix should record each firmware operation/event,
supported version, transport, authorization, acknowledgement/readback semantics,
offline behavior, and acceptance evidence. Complete available communication-system
integration first; record genuinely missing firmware interfaces as dependencies.

Within that work, separate authenticated intake, device/capability registry,
authorization, latest-state projection, optional history storage, and map presentation.
Do not choose a database, deployment platform, or permanent wire format solely for
this concept. Server implementation belongs in its separate project, not OpenTrail.

A future adapter maps source system + source device identity, observation/receipt
time, position validity/precision, and optional power/status into a versioned common
tracking record. Preserve source provenance, coordinate origin (GPS versus configured),
and permission context. Exact fields and security semantics remain to be specified.

Trail is the first supported source. Later third-party adapters use compatible
gateways or documented feeds with authorized access. The shared map does not imply
radio compatibility, equal location accuracy, or equivalent security. Static positions
and sparse advertisements are not live moving-tracker evidence. Unknown capabilities
remain unavailable. No automatic messaging bridge or remote third-party control.

## Development order and promotion gates

These are planning stages, not new active OT tasks. Assign individual OT identifiers
only when promoted through the established backlog process; preserve one public date
heading per day and individual task entries. Split implementation into bounded increments.

| Stage | Timing / dependency | Deliverable and exit evidence |
| --- | --- | --- |
| A: Record boundaries | Now, documentation only | This plan and linked decision; unchanged V1 scope and implementation |
| B: Integrate current firmware with server | Existing server work, after its existing prerequisites | Capability matrix and tested end-to-end support for available operations; honest unsupported states; no tracker dependency |
| C: Prepare shared server support | Authorized in parallel with B now, without delaying existing commitments | Shared communicator/tracker identity, roles, intake, permissions, latest/history and map components where useful; explicit synthetic contract tests; tracker-dependent acceptance pending firmware |
| D: Specify tracker product and protocol | After accepted V1; reconcile other post-V1 priorities | Candidate board/power/GPS requirements, lifecycle/setup, approved no-log semantics, report/config contracts, capacity targets and test plan |
| E: Implement direct tracker and local viewing | D plus loader and local enrollment prerequisites | Two trackers and a communicator/app; correct separate identities, latest-only map, access rejection, freshness and power/restart tests; no server required |
| F: Integrate Trail tracking with server | B and working tracker report contract | Authenticated gateway intake, current map, dedupe, permissions, outage semantics; add optional history with separate retention/deletion acceptance |
| G: Validate shared relay operation | Working direct path and separately accepted relay/security construction | Tracker through Trail relay; communicator and tracker relay roles validated separately; mixed traffic and power/loss evidence |
| H: Add external tracking adapters | Stable Trail intake/map and explicit demand | One ecosystem at a time, actual gateway/device tests, identity/privacy/freshness validation; tracking only |

Review the relay compatibility contract during D so report design does not obstruct
G. Implementation of relaying is not a prerequisite for the direct-tracker trial.
Server history need not delay the initial server current-location map. These dependency
choices permit parallel server work where feasible, but not diversion that delays
current commitments. Tracker firmware remains blocked until accepted V1.

## Acceptance evidence required before claims

- Local mode: multiple devices; valid/stale/unknown positions; loss and recovery;
  duplicates, clock changes, and unauthorized reports. Inspect storage, logs, backups,
  restart, and later server attachment to verify no local location history or backfill.
- Server: authenticated intake; workspace isolation and revocation; observed versus
  received time; delayed/out-of-order uploads; bounded configured buffering; history
  disabled/enabled behavior; retention, deletion and recovery; pending settings.
- Relays: real direct and multi-hop paths with relay-disabled negative controls;
  each claimed hardware/firmware role; concurrent communicator/tracker traffic;
  relay mobility, loss, restart, and saturation. Record offered load, packet counts,
  loss, duplicates, delivery latency, airtime, queue behavior and starvation limits.
- Power/hardware: measured GPS acquisition and reporting consumption, actual vehicle
  input protection and low-voltage behavior, parked operation, external-power loss,
  backup battery if fitted, enclosure/mounting and antenna effects. Do not claim
  weeks/months of runtime, universal board support, range, or guaranteed delivery.
- Loader/lifecycle: exact target/artifact verification, interrupted update recovery,
  retained settings where intended, ownership transfer/reset/revocation and retired
  device rejection. Do not weaken current release or security acceptance.

## Next planning action

When the existing server work becomes eligible, create its current-firmware capability
matrix first and reference this plan in its own canonical handoff. Resolve only the
extension decisions needed for that work. Keep tracker firmware blocked until V1
acceptance; do not flash devices, change the phone app, scaffold tracker firmware,
or publish a tracking capability claim merely because this plan exists.
