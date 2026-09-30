# OT-0257b Tracker target and power selection criteria

Publication version, 2026-09-29: local workspace paths removed; dated technical
facts and evidence limits are unchanged. Original evidence is retained privately.

Prepared 2026-09-29 under approved revision 1. Planning only: no board is chosen,
purchased, tested or labeled supported. Engineering ownership stays in OpenTrail.

## What a suitable first Tracker must do

Report the owner's equipment location to one enrolled receiver without requiring
a nearby phone or internet during the enabled session. The operator must be able
to stop it locally and see that reporting has stopped, even with no location fix,
radio link or valid clock. Boot stays stopped. The device must not turn old
location into a new fix or a queued report into a delivery claim.

These criteria follow the accepted [product proposal](OT-0256b-TRACKER-PROPOSAL-2026-09-29.md)
and [reporting policy](OT-0257a-TRACKER-POLICY-2026-09-29.md). They do not select
the still-unresolved reporting interval, battery runtime, retention limits or
hardware controls. Tracker remains optional; [Decision 0033](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md)
preserves the base V1 phone/BLE/direct-LoRa path. Its old pairing clauses have
their stated supersession and must not become a new Tracker setup default.

## Required interfaces and selection evidence

For each future candidate, complete the following checklist with a source,
observed evidence reference and any unresolved gate. A specification can nominate
a candidate; it cannot fill a received-board or measured-behavior field.

| Selection gate | Required interface or evidence | Reject or leave unresolved when |
| --- | --- | --- |
| Exact received target | Public model/SKU, received board revision, MCU/memory, radio variant, physical controls, supply/battery arrangement and authoritative pin/rail mapping. Record how each fact was established; keep private unit identifiers outside ordinary evidence. | USB family, seller photograph, product name or a sibling board is the only identity evidence. No target composition/build profile can be frozen from that alone. |
| Location provider | A bounded real provider exposes fix validity, quality/freshness evidence, unavailable/lost state and restart generation. Document its bus/protocol, pins, power/reset ownership and antenna placement. | Old coordinates can appear current after wake/reset, or a phone must continuously supply location despite the selected standalone use case. Actual acquisition, loss and reacquisition remain measurements. |
| Clock and scheduler | A checked monotonic scheduling/age source with defined boot/sleep behavior; fix observation and receiver observation remain distinct. Reuse bounded coalescing/backpressure rather than an accumulating route queue. | Clock rollback or sleep/reset silently changes age; independent clocks are subtracted without reviewed correlation; recovery creates a catch-up burst. |
| Direct radio and region | Exact RF variant, antenna path and operating region; documented transport control/interrupt/reset interface. Bind the future position object to the accepted authenticated/encrypted recipient, replay and retry contract. | A matching radio frequency or reusable codec is treated as interoperability, airtime permission or recipient authority. Region/antenna/configuration review must precede any authorized transmission. |
| Start, stop and status | Deliberate current start tied to the enrolled recipient; locally accessible stop and unmistakable active/stopped/waiting/fault indication. Document how stop remains reachable through sleep, no-fix, radio loss and clock fault. | Stop requires a working phone, radio, GNSS or checked clock. A power button, LED or touchscreen label alone does not establish the required behavior. |
| Power and low-energy behavior | Explicit supply/charging/protection boundary, measured load capability, relevant switched rails and observable low-energy/brownout behavior. Name which owner prevents unsafe reporting and preserves the stop/boot-stopped policy. | Nominal battery capacity, a charger IC or a low-power chip specification substitutes for complete-unit measurements. Low energy or restart must not revive prior sharing permission. |
| Configuration and security state | Bounded storage for enrolled recipient/configuration and required security state, with readback/reconciliation and reset policy. No route-history log or raw location diagnostics is assumed. | Damaged/interrupted state grants reporting, or ordinary flash is called rollback-proof. Do not silently add a secure element requirement; apply the expressly accepted threat model and disclose its limits. |
| Independent recovery | Exact boot/maintenance entry and reset side effects, tool/profile, image provenance/digests, allowed offsets/protected spans and an original-restoration route that does not depend on candidate firmware. | Original state cannot be established, the route requires an unapproved erase/reset, or recovery depends on the failing application. A build or vendor-demo boot is not recovery proof. |

Location, radio and power ownership must be separable: disabling a GNSS rail or
losing the receiver must not disable local stop; a location callback arriving
after stop must not reopen the reporting session. Authentication is separate from
BLE setup. Target selection must reserve the interfaces/resources needed by the
reviewed security contract, not guess a new packet format or cryptographic policy.

## Power evidence before a runtime promise

Freeze the intended mounting/environment, power source and responsiveness goal
before comparing candidates. Then define a bounded measurement plan using the
same exact board/revision, firmware/build configuration, antenna, supply/battery,
control/status settings and policy parameters. Record instrument/sample method,
duration, conditions, voltage, current/power and uncertainty; keep estimates
separate from observed endurance.

The measurement matrix must cover stopped/idle, deliberate start and acquisition,
enabled fresh-fix reporting, sustained no-fix, radio receive/transmit and bounded
retry, receiver loss/backpressure, sleep/wake if included, local stop, restart and
low-energy interruption. Capture average consumption, relevant peaks/voltage
droop and time/energy spent in each state. Include local indication and storage
costs rather than measuring only the radio or receiver module. Test return from
an outage without an unlimited backlog or automatic restart of sharing.

Use measured usable energy and workload consumption only for a labeled runtime
estimate; a supported runtime needs sustained complete-unit evidence under the
accepted workload and environmental limits. The owner must set desired runtime
and responsiveness; the later plan must set pass/fail bounds for cadence,
freshness, retries/expiry, startup, low-energy handling and recovery before any
physical execution. This report supplies no invented numeric defaults or claim
that an available battery meets those goals.

## Candidate decision record and next gates

A future selection record must name the exact candidate, the operating region,
intended power/mounting setup, the evidence for every checklist row, remaining
unknowns and the explicit reason to proceed or reject it. Maintain separate
candidate, experimented and validated evidence; none means supported by default.
Historical [Tracker evidence](OT-0256a-TRACKER-EVIDENCE-2026-09-23.md) does not
transfer Heltec or vendor-Wio results to a new Tracker target.

The unresolved owner choices are:

- Intended operating region and installation/environment, including attachment
  and service access; no vehicle supply or enclosure rating is assumed.
- Required responsiveness versus desired runtime, and permitted power source,
  charging/service arrangement, size and mounting constraints.
- Physical start/stop and visible indication approach; phone-assisted setup may
  be evaluated, but continuing phone availability cannot become a field dependency.
- Which exact received candidate to evaluate, with its identity evidence and
  independently restorable starting state. No purchase is needed or authorized
  merely to accept this checklist.

After these choices, [OT-0258a/b](../../tasks/OPTIONAL_PRODUCTS_PLAN.md) must map
the real adapters/owners and positive/negative tests. Apply the complete
[firmware target preflight](../firmware-porting-lessons.md) before target work;
record applicable results and reasons for skips. OT-0259a owns the later exact
physical measurement/acceptance plan, including at least two physical nodes for
any radio claim, packet counts/loss/duplicates/latency and test conditions. Device
enumeration, wiring, writes, transmission and restoration each remain within
their separately authorized exact setup; this planning task grants none of them.

## Validation boundary

This report is a source-derived planning deliverable. No implementation, build,
device behavior test, purchase, new protocol, publication, V1 credit or public
website capability change occurred. Report links and repository documentation
checks validate the document only; their command results are retained in private
task evidence. Independent review and owner acceptance are separate. The root
coordinator owns shared status/backlog/checklist updates. The next action is
review of this checklist and the named choices, not hardware execution.
