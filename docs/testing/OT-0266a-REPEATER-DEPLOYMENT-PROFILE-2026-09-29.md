# OT-0266a Repeater hardware and deployment profile

Prepared 2026-09-29 under approved revision 1. This defines selection and
validation evidence. It does not select, purchase or validate equipment.

## Owner summary

The first evaluation should use one accessible dedicated repeater between two
endpoints, with an independently recoverable starting state. Recommend an
accessible, controlled installation before any unattended outdoor deployment.
That makes power, recovery and radio-path failures observable before a harder
installation adds weather, mounting and service-access uncertainty.

A suitable candidate needs a documented radio and antenna combination, enough
memory and processing capacity for secure bounded forwarding, a power supply
that survives transmit and storage loads, and a recovery method that still
works when its application fails. A matching product name or advertised battery
capacity is insufficient. The complete received assembly must be checked.

External power or a suitably sized backup supply is recommended for sustained
relay use, as recorded in Future Concepts; no supply type, capacity or duration
is selected here. Battery-only, solar, permanent outdoor mounting and combined
client/repeater use each add evidence requirements. No measured range, runtime,
weather rating or supported target is established by this report.

Owner decisions can follow review: intended operating region, installation and
service conditions, available power and required operating duration, and the
candidate assembly to evaluate. No equipment or attendance is needed to finish
this planning deliverable.

## Accepted scope and existing evidence

The [role proposal](OT-0264b-REPEATER-ROLE-PROPOSAL-2026-09-29.md) establishes
dedicated-first planning: exactly one authorized repeater between two endpoints.
The [security review](OT-0265a-REPEATER-SECURITY-REVIEW-2026-09-29.md) and
[scheduling proposal](OT-0265b-REPEATER-SCHEDULING-2026-09-29.md) require
authenticated eligible objects, immutable bytes, current membership/epoch,
replay safety, bounded queues and finite airtime. Their accepted recommendations
are the baseline; this report does not reopen their approval.

[Future Concepts](../FUTURE_CONCEPTS.md) preserves later combined client/repeater
capability. That profile needs separate client resources, fair scheduling and
simultaneous-traffic/power evidence. Dedicated-first evidence cannot establish it.
[Decision 0033](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md)
keeps current V1 independent of a relay, superseding the initial-release promise
in [Decision 0004](../decisions/0004-immutable-first-release-forwarding.md).
[Decision 0035](../decisions/0035-host-tested-secure-lora-key-transport-contract.md)
remains direct-only: buying a capable radio cannot make OTSL0 relay-compatible.
A future relay-capable protected construction and its resource costs remain a
separate gate. No algorithm, outer envelope or new wire identifier is selected.

The [prototype inventory](OT-0264a-REPEATER-EVIDENCE-2026-09-23.md) records
historical MeshCore experiments and host policy evidence. Those results neither
select a current Trail target nor prove a received unit's present identity,
restoration state, radio configuration or field performance.

## Evidence required for each candidate profile

Each row needs an evidence reference, its layer and date, the exact configuration
covered, and remaining unknowns. Keep specification, received-unit observation,
host/build result and physical measurement separate. A failed or missing gate
remains unresolved; it cannot be filled with a sibling product's result.

| Boundary | Candidate specification can establish | Received-unit and measured evidence still required |
| --- | --- | --- |
| Board identity | Manufacturer model/SKU, published board revision, schematic, MCU, flash/PSRAM and radio variants. | Exact received revision and fitted components, memory/partition profile, connectors and assembly differences. Retain private identifiers only in controlled evidence. A USB family or seller photograph is not exact identity. |
| Interface ownership | Published pins/buses for radio, interrupt/reset, storage, power sensing, local controls and maintenance. | Coherent pin/rail map, boot-strap conflicts, shared-bus arbitration and reset side effects on this revision. A recovery connector must remain accessible after assembly. |
| Radio region | Candidate frequency coverage and documented radio/board regulatory references. | Intended jurisdiction and complete firmware/PHY/power/channel/antenna configuration reviewed before transmission. Frequency overlap or a module marking is not complete-assembly authorization or interoperability evidence. |
| Antenna and RF path | Specified antenna band, impedance, connector, gain, cable and mounting requirements. | Exact installed antenna/cable/adapters, connection integrity, placement/orientation and enclosure interaction. Review the complete permitted configuration and measure the actual path; do not infer useful coverage from gain or transmitter power alone. |
| Enclosure and mount | Published material, dimensions, connector seals, mounting and component operating limits. | Complete assembly fit, strain relief, access to disable/recovery, heat dissipation, condensation/water/dust exposure, vibration and mounting security in the intended setting. A rated empty enclosure does not confer that rating after drilling or assembly. |
| Power source | Supply/battery/charger specifications, protections, rail ratings and connector polarity. | Exact source and charging arrangement, loaded voltage/current, radio/crypto/storage peaks, cable losses, brownout/reset behavior and any backup switchover. Capacity labels cannot establish usable energy or runtime. |
| Thermal/energy behavior | Published component limits and candidate environmental suitability. | Temperature and consumption of the complete assembly at idle, receive, sustained admitted load, congestion and charging where applicable. Battery limits and enclosure temperature constrain the eventual operating profile. No numerical thresholds are invented. |
| Security resources | Available entropy, storage interfaces, memory and processor features. | Verified readiness/failure behavior and accepted security construction's RAM/stack/latency needs. Features advertised by a chip do not establish entropy quality, protected persistence or rollback resistance. Do not add an unapproved secure-element or physical-erasure claim. |
| Persistent state | Candidate nonvolatile capacity and endurance specifications. | Exact role/context/epoch/replay namespace, atomic/readback/recovery behavior, write amplification and power-cut results. CRC or file existence is not authentication. Ordinary restart protection and physical-access limitations must match the accepted threat model. |
| Local operation | Candidate switch/button/status interface and access method. | Deliberate enable/disable, truthful disabled/ready/busy/fault indications, bounded control response under load, and service access without a working application or remote connection. An LED label alone does not prove the state. |
| Independent recovery | Documented ROM/boot/maintenance entry, tools and supported image layout. | Exact original image/state provenance, protected spans and allowed offsets, verified restoration route, reset effects, and confirmed return to the original application/state. Recovery cannot depend on candidate firmware or silently erase owner state. |

Do not assume GNSS, a screen, cellular service, internet or Trail Server is
necessary for the dedicated role. Any added peripheral must have a stated
purpose, resource owner and independent failure behavior. The required local
status/recovery method remains a choice; removing a screen does not remove it.

## Resource and power review before a target is frozen

Charge RAM for immutable protected frames and any reviewed outer envelope,
queue/replay metadata, authentication scratch, each task stack, radio buffers,
diagnostics and system headroom. Charge time for authentication, persistence,
interrupt servicing, queue expiry, dispatch and receive rearm. Use the accepted
scheduling proposal's per-source/global limits and airtime accounting; historical
host constants are not selected production limits.

Bound both ordinary traffic and authenticated overload. Persisting rejected
eligible observations can consume storage writes even when no RF is emitted.
The reviewed workload therefore needs storage latency, wear and energy costs,
not just a radio transmit estimate. If a candidate cannot meet the budget,
reduce/review the product profile or reject the candidate; do not drop replay
durability or source authentication to fit it.

Measure stopped, ready/receive, ordinary forwarding, maximum admitted burst,
congestion, storage recovery, low/unknown supply, restart and disable under load.
For backup power, include source removal and return; for solar or intermittent
charging, define the actual installation/workload conditions before any endurance
claim. Record the instrument/method, configuration, duration, uncertainty,
averages and peaks. A usable-energy/load calculation is an estimate; supported
duration requires sustained complete-unit evidence under the claimed conditions.

## Complete evaluation, operation and recovery sequence

| Stage / responsible boundary | Required evidence and transition | Refusal, outage or recovery behavior |
| --- | --- | --- |
| Profile definition / engineering and owner | Name intended role, environment, region, candidate revision, antenna, power and recovery path; distinguish every unknown from an observed fact. | Incomplete profile remains a candidate. Do not freeze a deployable target from guessed pins or an unknown RF variant. |
| Target readiness / build and security owners | Apply the full firmware-porting preflight, accepted protected construction and host/composed-adapter tests. Bind reproducible artifacts, layout and restoration evidence to this target. | Build success alone grants no device access, write or RF authority. Missing security/persistence/recovery gates block the physical trial. |
| Isolated bench readiness / operator | Separately authorize the exact three-radio setup; verify each device, current endpoint, initial state, antenna, artifact, offsets and restoration route immediately before its operation. | No simultaneous flashing, unknown starting state or substitution of a similar board. Use a controlled bench arrangement without implying RF shielding or permission to radiate. |
| Boot / authority and recovery owners | Forwarding stays disabled until current role/context/epoch, replay state and airtime recovery validate. Local recovery remains accessible. | Invalid/uncertain storage or authority stays contained; no default identity, automatic trust reset or fresh airtime credit. |
| Ready and forwarding / scheduler and radio owners | Process bounded authenticated traffic, preserve exact bytes, persist before release, enforce age/airtime and restore receive readiness after the bound terminal event. | Queue admission is not RF completion or destination delivery. Possible emission with missing completion remains uncertain; no automatic second attempt. |
| Load, heat or supply problem / runtime owner | Maintain bounded control/status response; apply only reviewed measured operating limits and retire unsafe queued work. | Unknown power is not a guessed percentage. No security bypass, unlimited retry, invented emergency-delivery promise or automatic restart of a retired role. |
| Relay disabled or unavailable / endpoints | Stop relay admission and serialize disable with dispatch; endpoints retain independent direct behavior. | Already emitted bytes cannot be recalled. No replacement-relay flood, synthetic ACK or dependency on phone/server assistance to keep the base path useful. |
| Interruption and restart / storage and radio owners | Reconcile verified role/replay/airtime state before new admission; identify saved-but-unsent loss honestly. | Do not replay a lost RAM queue or lower protection to recover delivery. Uncertain durable state requires service. |
| Trial completion or failure / operator | Follow the authorized restoration plan per device and verify exact readback plus original operational state; close custody before declaring the trial complete. | A write command or reset alone is not restoration evidence. Keep any unresolved recovery explicitly open; do not widen erase/write scope opportunistically. |
| Release review / evidence owner | Limit any support statement to the exact tested hardware/firmware/region/antenna/power/enclosure profile and its accepted results. | New revision, enclosure alteration, antenna, power source or combined role needs impact review; no automatic transfer of range/runtime/environmental claims. |

The [firmware-porting checklist](../firmware-porting-lessons.md) remains mandatory
before target implementation. This task only defines its future application:
no target or toolchain is selected, so target builds, device enumeration, wiring,
RF execution and restoration checks are not performed. Later work must record
every applicable result and a reason for each genuinely inapplicable item,
respecting that checklist's supersession notices for historical procedures.

## Acceptance evidence to carry into later plans

OT-0267a should define positive and negative controls using the exact selected
profile: sender/relay/receiver with relay enabled and disabled; duplicate/replay,
stale epoch, queue/airtime saturation, delayed or lost radio completion, receive
rearm, disable during dispatch, corrupted/uncertain storage, power cuts and
restart. Add supply loss/return, inaccessible or failed optional peripherals,
and the intended installation's environmental/thermal cases. Combined-role
claims additionally require simultaneous client traffic and measured fairness.

Record generated, admitted, dropped, expired, submitted, completed, uncertain
and receiver-admitted counts separately, along with duplicates, loss, latency,
airtime, queue high-water marks, supply/temperature and test conditions. A relay
transmit count cannot substitute for endpoint acceptance. Range results require
actual distance/context, terrain, obstacles, antenna placement and observations;
neither a close bench success nor a theoretical budget establishes field reach.
Keep identities, keys, private traffic and precise private locations out of
ordinary reports. Host fault injection is not a physical power-cut result.

## Specific remaining choices and next gate

The owner needs to nominate intended region and use setting; accessible temporary
versus permanent/unattended installation; power/charging availability and desired
duration; mounting/size/service constraints; and an exact candidate to evaluate.
Engineering then verifies received identity, antenna/configuration compatibility,
interfaces, recovery and budgets. Battery thresholds, queue sizes, thermal limits,
timeouts and radio settings follow reviewed evidence, not assumptions in this plan.

The accepted dedicated-first role is already the baseline. Selecting a combined
first deployment instead would change its evaluation scope and needs an explicit
decision. Crypto/envelope/epoch construction and secure persistence remain their
own gates; this report does not select them through a hardware checklist.

[OT-0266b](../../tasks/OPTIONAL_PRODUCTS_PLAN.md) owns provisioning, diagnostics
and durable recovery mapping and must register/freeze exact implementation,
host/build, physical-validation and release successors after scope/target
decisions. [OT-0267a/b](../../tasks/BACKLOG.md) retain later acceptance and
deployment guidance. This evidence-required profile can be reviewed now without
pretending those implementation or physical gates are complete.

Only this report and private preparation evidence were authored for this task.
Local link/whitespace checks and coordinator-owned documentation validation
verify the document; they establish no hardware, radio, security implementation,
runtime or environmental result. No new IDs, product names, code, purchases,
V1 credit, Git publication, deployment or public capability change resulted.
