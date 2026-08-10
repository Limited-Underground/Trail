# Power-state boundary v0

Status: **host-tested contract; no target adapter or battery-life claim**

## Purpose

OpenTrail needs one consistent answer when a portable client is on battery,
charging, externally powered, low, stale, or unable to read its power hardware.
This boundary keeps those states explicit before display, logging, scheduling,
or field-test code consumes them.

It does not identify battery chemistry, control a charger, estimate state of
charge from voltage, shut down a node, or prove endurance. Those decisions
belong to a frozen hardware target and its reviewed adapter.

## Target adapter contract

`PowerStatusSource::read()` returns one atomic `RawPowerObservation` containing:

- typed read status: `none`, `not_ready`, or `source_failed`;
- external-power state: `unknown`, `absent`, or `present`;
- battery presence: `unknown`, `absent`, or `present`;
- orthogonal charge state: `unknown`, `not_charging`, `charging`, `full`, or
  `fault`;
- optional normalized battery percentage and optional measured millivolts; and
- the boot-local monotonic millisecond when the observation was taken.

The target adapter owns fuel-gauge, ADC, charger, GPIO, and board-specific
details. It must mark missing measurements absent instead of substituting zero
or a remembered value. Validity flags are canonical: the paired numeric field
must be zero when its flag is false.

The caller supplies one timestamp already accepted by the checked monotonic
clock boundary. A future observation is invalid. An observation is current at
exactly `stale_after_ms` and stale one millisecond later.

## Injected policy

Composition must explicitly provide:

- `low_battery_percent`;
- a strictly lower `critical_battery_percent`; and
- a nonzero `stale_after_ms`.

The zero-initialized policy is invalid. The v0 component includes no universal
percentage or voltage threshold. Product policy can therefore be reviewed and
changed without rewriting a board adapter.

## Assessment behavior

The evaluator reads the source exactly once and produces one fixed-size
`PowerAssessment`:

| State | Meaning |
| --- | --- |
| `normal` | Valid battery percentage is above the injected low threshold |
| `low` | Percentage is at or below low, but above critical |
| `critical` | Percentage is at or below critical |
| `external_only` | External power is present and battery absence is explicit |
| `indeterminate` | Source read is valid, but battery presence or percentage is unknown |
| `fault` | The adapter reports a charger/power-subsystem fault |
| `unavailable` | Source is not ready or failed |
| `stale` | A previously valid observation exceeded the freshness limit |
| `invalid` | Policy, timestamp, enum, range, or state combination is incoherent |

Charge remains separate from the battery band. A node can therefore report
`low` and `charging` together rather than allowing charging to hide low state.

Only `normal` and `external_only` permit optional high-power work in v0. Every
other state requests operator attention and denies that optional-work hint.
This is a conservative scheduling/display input, not an automatic shutdown or
safety guarantee.

## Host evidence

Eleven deterministic scenario groups cover:

1. not-ready recovery and a complete normal reading;
2. typed and unknown source failures;
3. invalid policy without touching hardware;
4. exact normal/low/critical percentage boundaries;
5. charging/full status kept orthogonal to the battery band;
6. explicit external-only operation;
7. missing percentage preserved as indeterminate;
8. a reported charger fault distinct from source failure;
9. invalid ranges, hidden fields, and impossible combinations;
10. future, exact-freshness, and stale timestamp boundaries; and
11. bounded FIFO capacity and ordering in test support.

The fake source is under `test_support` only and must not be linked into target
firmware.

## Remaining target gates

- Freeze the exact first-pilot client, battery, charger, fuel gauge/ADC path,
  enclosure, and USB-recovery behavior.
- Implement and review the target adapter with correct atomic sampling.
- Choose thresholds from hardware and session evidence, not the host examples.
- Define behavior during boot, USB attach/removal, brownout, deep sleep, charger
  fault, gauge reset, and battery replacement.
- Render low/critical/stale/fault states and prove they remain visible during
  radio, GNSS, display, logging, and recovery activity.
- Measure current draw, charging, temperature behavior, runtime, and recovery
  on physical hardware. Record start/end battery evidence in the four-person
  pilot without publishing device identifiers or private location data.
