# Funding application answer bank

Status date: 2026-08-10

Replace all bracketed fields and tailor every response to the actual program.
The public product name remains undecided; do not substitute ECLU by default.
All cash, hardware, discount, loan, sponsorship, and service-credit requests are
on hold. This answer bank is retained as preparation material only; do not
tailor it for outreach, contact a source, submit, accept, or connect an account
until the owner explicitly clears the hold.

## 50-word summary

`[PUBLIC PROJECT NAME]`, developed in the OpenTrail repository, is an open-source
effort to create self-contained ESP32/LoRa devices for offline group messaging,
location awareness, priority alerts, and controlled relaying. Initial funding
would turn proven host components and three-radio bench evidence into four
identical field-test units and a documented four-person pilot.

## 150-word summary

`[PUBLIC PROJECT NAME]` is an open-source communication and awareness project
maintained under the Limited Underground umbrella. The work explores portable
ESP32-class LoRa devices that remain useful without cellular service, internet,
a phone, a central server, or a repeater. Possible settings include vehicle
groups, racing support, camping, rural property, severe weather, off-road use,
and volunteer field operations. The OpenTrail repository already contains a
documented architecture, deterministic protocol/state components, public CI,
privacy-safe evidence tooling, and bounded physical testing with two Heltec
clients and a Seeed SenseCAP repeater, including a 300/300 five-hour bench soak.
It does not yet contain production firmware or a field-validated reference
device. The proposed funded milestone is intentionally narrow: select one exact
client configuration, build four identical self-contained units plus spares,
complete target integration, and conduct repeatable four-person field sessions
with published aggregate results and honest failure reporting.

## Need statement

Groups frequently operate where normal cellular coverage is absent, congested,
or unreliable. Available systems can be closed, subscription-dependent,
phone-dependent, optimized for one activity, or difficult to inspect and
modify. This project investigates a transparent base device that works locally
first and treats repeaters, vehicle integration, larger displays, and remote
backup as optional additions rather than prerequisites.

## Proposed use of support

Support will be restricted to the next evidence milestone:

- acquire or receive six identical candidate nodes, enough for four active
  participants and two spares;
- obtain matching GNSS, antenna, power, enclosure, mounting, and recovery parts;
- complete direct target-firmware integration and reproducible provisioning;
- measure power, GNSS, UI, radio, recovery, and basic environmental behavior;
- execute at least three one-hour four-person sessions in materially different
  environments; and
- publish privacy-safe results, build/configuration instructions, defects, and
  an itemized use-of-funds report.

## Why this team/project can execute

The project already uses evidence gates rather than product claims. It has
separate OpenTrail and OpenGauge repositories, Apache-2.0 licensing, public
automated validation, explicit hardware-evidence tiers, versioned protocols,
fail-closed test-result tooling, and documented limits. Existing physical work
has produced repeatable close-bench delivery and relay evidence while clearly
recording what it does not prove.

## Deliverables

1. Frozen bill of materials and versioned firmware/toolchain manifest.
2. Reproducible build, flash, configuration, and recovery instructions.
3. Four operational units plus documented spare/replacement handling.
4. Three privacy-safe four-person pilot datasets and deterministic verdicts.
5. Public defect and limitation report.
6. Updated tested-compatible hardware evidence.
7. Next-phase four-plus-repeater plan based on observed results.

## Public benefit

All generally useful software, specifications, test tools, and nonprivate
evidence produced by the funded milestone will be published under the project's
open-source and contribution policies. Other builders will be able to reproduce
the setup, examine tradeoffs, report incompatible hardware honestly, and adapt
the modular system to different activities without making the base device
dependent on a proprietary cloud service.

## Success measures

Success is not defined as a flawless demonstration. It means:

- the exact funded hardware and firmware are reproducible;
- all four units meet the declared readiness gates;
- planned sessions run without infrastructure dependencies;
- evidence passes the privacy and structural validators;
- delivery, duplicate, latency, battery, usability, recovery, and failure data
  are complete enough to support a deterministic verdict; and
- failures and limitations produce an actionable next plan.

## Sustainability

The base system is designed to operate without recurring hosting. Public source,
reproducible hardware tiers, community contributions, small hardware grants,
transparent milestone funding, and optional paid or self-hosted server services
may support later work. No application should promise a particular revenue
model until the legal entity, product name, support obligations, and server
privacy model are decided.

## Risks and mitigations

| Risk | Mitigation |
| --- | --- |
| Radio or regulatory constraints | Freeze region, radio settings, antennas, and applicable rules before public field use |
| Hardware availability changes | Maintain evidence-based candidate/experimented/validated tiers and avoid a single undocumented supplier assumption |
| Security design is incomplete | Keep experimental traffic nonproduction, benchmark reviewed libraries on the exact target, and fail closed on missing trust |
| Field reliability differs from bench | Use multiple environments, publish failures, repeat sessions, and expand group size only after evidence |
| Privacy harm from location history | Aggregate public evidence, make server capture opt-in, minimize retention, and provide export/deletion |
| Name or trademark conflict | Keep the public name replaceable and out of protocols, persistent formats, device identities, and hardware until cleared |
| Server outage or vendor change | Keep local device functions independent and preserve portable APIs, exports, and data formats |

## Applicant fields still required

- `[LEGAL APPLICANT NAME]`
- `[ENTITY TYPE AND JURISDICTION]`
- `[AUTHORIZED SIGNER]`
- `[MAILING ADDRESS]`
- `[CONTACT EMAIL AND PHONE]`
- `[TAX OR REGISTRATION ID, WHEN REQUIRED]`
- `[BANKING/PAYMENT METHOD, ONLY IN THE PROGRAM'S SECURE PORTAL]`
- `[CONFLICT-OF-INTEREST DISCLOSURES]`
- `[PROJECT DATES]`
- `[FINAL ITEMIZED BUDGET]`
- `[WRITTEN CONFIRMATION THAT THE PRIVATE FUNDING/PROPERTY HOLD IS CLEARED]`
