# Four-Person Pilot Result v0

Status: host evaluator and aggregate result contract, 2026-08-10

`OTPR0/v0` turns one completed four-person session into a deterministic
`pass`, `fail`, `ineligible`, or `invalid` outcome against the exact
[`OTFP0/v0` plan](../../tests/field-plans/OT-023-FOUR_PERSON_PILOT_V0.json).
It prevents a session with the wrong hardware, topology, duration, dependencies,
or incomplete evidence from being mistaken for a failed or successful pilot.

## Verdict meanings

| Verdict | Meaning |
| --- | --- |
| `pass` | Setup matches the frozen plan and every v0 acceptance gate passes |
| `fail` | Setup is eligible, but one or more measured acceptance gates fail |
| `ineligible` | The plan is not ready or the session did not follow its frozen setup/evidence boundary |
| `invalid` | The plan or result record is malformed, noncanonical, or privacy-unsafe |

An `ineligible` session is still useful diagnostic work, but it does not count
toward the three required pilot sessions. A `fail` remains part of the evidence
history and must not be discarded or tuned away.

## Public result content

The strict result record contains only:

- a neutral plan and session label plus broad scenario class and duration;
- evidence-complete and operator-safety confirmations;
- exact public client model/firmware, tested count, identical-unit state, and
  Boolean checks for battery, enclosure, GNSS, display, input, and USB recovery;
- exact client/repeater counts and explicit Boolean disclosure of any phone,
  laptop, server, internet, repeater, or vehicle dependency used;
- origins and observed peer deliveries for position, status, quick-status, and
  critical-alert traffic plus visible duplicates;
- aggregate latency, stale-peer, GNSS-first-fix, and ending-battery results;
- false-success, queue-overflow, and reset counts; and
- configuration cleanup, raw-capture locality, and canonical privacy flags.

Free-form notes are intentionally excluded. The validator recursively rejects
transport ports, serials, addresses, MACs, channel names, coordinates, keys,
PINs, secrets, and noncanonical extra fields.

## Evaluation math

The evaluator derives expected origins and peer-delivery opportunities from the
validated plan. For four clients, every origin has three peer opportunities:

| Class | Origins | Peer opportunities | v0 decision use |
| --- | ---: | ---: | --- |
| Position | 240 | 720 | at least 950,000 ppm delivery |
| Status | 48 | 144 | recorded for review; no independent v0 threshold |
| Quick status | 8 | 24 | recorded for review; no independent v0 threshold |
| Critical alert | 4 | 12 | zero loss |
| **Total** | **300** | **900** |  |

Status and quick-status delivery remain mandatory evidence even though v0 does
not give them separate numeric gates. A future decision to gate them changes the
plan contract and must use a new plan version rather than rewriting v0 after
observing results.

The evaluator also applies the plan's zero duplicate/false-success/overflow/
reset gates, latency/staleness/GNSS upper limits, and ending-battery lower limit.
Position parts-per-million uses integer floor division so a borderline result
cannot round upward into a pass.

## Running the evaluator

After the hardware plan is frozen as `ready`, create a fail-closed result
template before a session:

```powershell
python tools/pilot_result.py `
  --plan tests/field-plans/OT-023-FOUR_PERSON_PILOT_V0.json `
  --new-result build/field-results/pilot-open-field-a.json `
  --session-id pilot-open-field-a `
  --scenario-class open-field
```

Template creation refuses a blocked plan, an unlisted scenario class, a malformed
session label, an existing output file, or an abandoned temporary output. It
copies the frozen model, firmware, topology, and duration, but deliberately
starts evidence, safety, identical-unit, capability, and cleanup confirmations
as false and all observations as zero. A blank template is structurally valid
but cannot evaluate as a pass.

After recording and privacy review, evaluate it with:

```powershell
python tools/pilot_result.py `
  --plan tests/field-plans/OT-023-FOUR_PERSON_PILOT_V0.json `
  --result path/to/privacy-safe-result.json
```

Evaluation exit code `0` means `pass`, `2` means a valid `fail` or `ineligible`
result, and `1` means invalid input. The checked-in plan intentionally refuses
template creation and returns ineligible until the exact four-unit hardware and
firmware freeze changes it from `draft_blocked` to `ready`.

## Current evidence boundary

Nine deterministic host groups cover a matching pass, blocked-plan refusal,
delivery/reliability failure, setup ineligibility, privacy rejection, strict
shape validation, impossible delivery counts, fail-closed template generation,
scenario refusal, and atomic no-overwrite output. This is evaluator evidence,
not a live four-person test or supported-hardware claim.
