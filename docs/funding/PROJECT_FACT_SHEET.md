# OpenTrail project fact sheet

Status date: 2026-08-10

## Identification

| Field | Current value |
| --- | --- |
| Public project name | `[PUBLIC PROJECT NAME]` — not selected |
| Engineering/repository name | OpenTrail |
| Umbrella identity/domain | Limited Underground / `limitedunderground.com` |
| Repository | https://github.com/nbjelanovic/OpenTrail |
| License | Apache License 2.0 |
| Legal applicant/payee | To be confirmed; all funding and hardware outreach is on hold |
| Current phase | Architecture, deterministic host components, and bounded bench proof |

## One-sentence description

OpenTrail is an open-source effort to develop self-contained, ESP32-class LoRa
devices for offline group messaging, location awareness, priority alerts, and
controlled relaying when normal cellular service is absent or unreliable.

## The problem

Small groups operating around vehicles, camps, rural property, outdoor events,
storms, or volunteer field work often lose cellular coverage. Existing choices
can require a phone, subscription, central service, closed ecosystem, or a
deployment designed for only one activity. The project is exploring a modular
base device that remains useful without a server, phone, repeater, or internet
connection while allowing optional displays, repeaters, vehicle alerts, and
server-assisted recovery later.

## Proposed first outcome

The first public field milestone is deliberately small: four people, four
identical self-contained client units, and no repeater or infrastructure during
each one-hour session. The planned progression is:

1. four clients without a repeater;
2. four clients plus one repeater; and
3. eight clients plus one repeater.

The first plan defines three materially different environments, 300 originated
messages, 900 peer-delivery opportunities, privacy-safe logging, and explicit
pass/fail/ineligible verdicts. It is not hardware-ready yet.

## Current evidence

- Two Heltec V4 OLED USB Companion nodes and one Seeed SenseCAP Solar repeater
  operate on matching USA/Canada MeshCore settings.
- A five-hour close-bench soak delivered 300 of 300 alternating messages with
  no observed loss, duplicates, or new radio errors and verified cleanup.
- A later bounded burst delivered 30 of 30 messages at two-second intervals,
  with exact repeater RX/TX accounting and verified cleanup.
- Role-reversed physical alert/acknowledgement exercises transported exact
  OpenGauge/OpenTrail frames through the three-radio path and exercised bounded
  host authorization, replay, retry, rejection, and completion behavior.
- Public GitHub Actions continuously validates the deterministic host suites.
- The four-person pilot plan, aggregate evidence format, and verdict evaluator
  are public and fail closed when hardware, evidence, privacy, or topology does
  not meet the declared plan.

## What is not yet proven

- No production OpenTrail firmware or supported-hardware declaration exists.
- No exact four-unit client hardware and firmware freeze exists.
- Direct SX1262 binding, authenticated on-device transport, protected key
  storage, GNSS behavior, production UI, field range, battery endurance,
  environmental suitability, and regulatory acceptance remain unproved.
- No four-person field session has run.
- Optional server-side breadcrumb recovery has only a host-tested local capture
  boundary. No remote archive, account, retention, or promised service exists.

## Why the work is open

Open development makes protocol assumptions, hardware evidence, test failures,
and funding use reviewable. The project intends to publish reproducible build
and test instructions, clearly tier hardware as candidate/experimented/
validated, accept outside contributions under documented rules, and avoid
making core offline communication depend on a paid service.

## Safety and privacy boundary

The system is intended as a supplemental communication and awareness aid. It
cannot guarantee delivery, location accuracy, rescue, emergency response, radio
range, or legal operation in every location. Public field evidence excludes
precise locations, personal identifiers, device addresses, credentials,
private channel names, and secrets. Any future server feature must be opt-in,
explicitly started and stopped, exportable, deletable, retention-limited, and
nonessential to base-device operation.

## Near-term support request

The first useful support package funds or supplies enough identical hardware to
build four field units plus two spares, complete direct target firmware work,
measure GNSS/power/display behavior, create safe enclosures, and conduct the
first repeatable four-person pilot. See the separate hardware request and
budget for details.

All cash, hardware, discount, loan, sponsorship, and service-credit outreach is
currently paused by owner direction. This fact sheet is preparation material,
not authority to contact a program or vendor, submit, accept terms or property,
direct shipment, connect an account, or announce support.
