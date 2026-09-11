# OT-198 input-refusal diagnostics

Status: focused suites, both matching firmware builds, composed operator checks
and isolated-runtime probes passed. The complete GitHub host matrix and
publication remain pending.

## Purpose and unchanged boundaries

The [OT-197 physical trial](OT-197-STAGE-TRIAL-2026-09-11.md) returned the broad
`input_result / input_refused` marker without an accepted policy receipt. That
result did not distinguish deadline expiry, malformed input or console health
loss. This additive successor records the actual deciding refusal branch and
adds separate host-side timing observations. It does not assign a physical cause
to the earlier trial.

The new [target](../../firmware/targets/heltec_v4_security_input_diag/README.md)
is `heltec_v4_security_input_diag`, application version `ot198-input-diag-v0`.
The strict wire remains `SEC_EVAL1 ot187-policy-v0`. Existing targets, parsers,
record contracts and accepted build evidence remain unchanged. No timeout,
challenge, framing, serial-closure or restoration rule is relaxed.

## One compact record

The [authoritative contract](../../firmware/components/security_diagnostics/input_record_v1.json)
and [C++ codec](../../firmware/components/security_diagnostics/include/opentrail/security_input_record.hpp)
define exactly one U64 at namespace `ot198diag`, key `stage`:

| Bytes | Meaning |
| --- | --- |
| 0–3 | `98 d1 01 01`: distinct magic, schema 1, image 1 |
| 4 | Existing stage number, 1 through 8 |
| 5 | Stage-specific error code |
| 6–7 | Little-endian CRC16-CCITT, initial `ffff`, covering bytes 0–5 |

Stages remain admitted, install result, waiting, input result, evaluation enter,
evaluation return, send enter and send return. Existing applicable errors retain
their numeric values. Input-result accepts success 0, session refusal 3, or:

| Code | Reason |
| ---: | --- |
| 9 | Initial idle timeout |
| 10 | First-byte assembly timeout |
| 11 | Clock regression |
| 12 | Input buffer limit |
| 13 | Invalid complete-line length |
| 14 | Invalid command prefix |
| 15 | Invalid challenge hex |
| 16 | Receive-loop limit |
| 17 | Console fault |
| 18 | Unexpected parser state |

Generic error 2 is not admitted for this image. The record contains no timing
fields, byte counts, command contents, challenge, keys or device identity.

The [diagnostic parser](../../firmware/components/security_evaluation/include/opentrail/evaluation_control_diagnostic.hpp)
sets its first refusal reason at the deciding branch. Its state transitions and
receipt behavior match the frozen parser. The
[receive helper](../../firmware/targets/heltec_v4_security_input_diag/main/input_control_loop.hpp)
retains the existing read, clock, health and pause callback ordering. It adds no
SDK callback to classify a failure. Health loss after parser-ready is a console
fault; classification does not change the parser state. An earlier parser refusal
is retained if another failure is subsequently observed.

Initial idle remains 60 seconds; assembly remains five seconds from the first
byte. The loop cap remains 7,000 iterations with a one-tick pause. Empty RX is
still ordinary no-data, not a console fault. The new
[entry point](../../firmware/targets/heltec_v4_security_input_diag/main/app_main.cpp)
retains pre-console NVS initialization and the eight-stage sequence. Its
[store](../../firmware/targets/heltec_v4_security_input_diag/main/stage_store.hpp)
requires namespace absence, then set/commit/exact-get for each stage, with no
erase or retry and sticky storage uncertainty.

A valid record remains only the last structurally valid stored marker. It does
not prove acknowledgment of persistence or that the following operation began.
Later stage errors are local to those stages: `send_return / none` still does not
prove policy success or USB delivery. CRC detects corruption, not malicious
rewriting or rollback.

## Readback, custody and host timing

The additive [decoder](../../tools/security_policy_input_readback.py) admits the
new exact record and retains bounded, conservative NVS validation. It does not
broaden the old image's accepted schema. Original-byte absence is distinct from
current live custody. Earlier reset snapshots and consumed grants cannot supply
fresh trial authority.

The [observation seam](../../tools/security_policy_input_observation.py) permits
one full-NVS read after confirmed serial closure and the durable restoration
boundary. Raw bytes remain in an exclusive private capture; public projection
contains only fixed availability, stage and error fields. Observation failures
cannot replace successful restoration or bypass a failed closure, journal or
original-binding check. Complete original application, full NVS and protected
regions still require independent verification before reset.

The [host timing observer](../../tools/security_policy_input_timing.py) uses its
own monotonic clock at candidate reset intent/return, serial open intent/return,
and RUN intent. It reports reset-call, reset-return-to-open, open,
opened-to-RUN and reset-return-to-RUN durations. These are host execution
boundaries, **not an actual device boot clock or firmware receipt timestamp**.
They must not be subtracted from the device clock. Missing, reordered, regressing,
out-of-range or failed timing samples make diagnostics unavailable without
changing the execution or restoration result. Endpoint deadline clocks and SDK
callback sequences are unchanged; diagnostic bookkeeping can still add overhead.

## Focused validation

| Suite | Passed | Evidence boundary |
| --- | ---: | --- |
| [Control](../../tests/host/security_policy_input_control_tests.py) | 24 groups | Frozen/new state and receipt comparison, including 500 deterministic randomized sequences and ten complete callback-trace comparisons; all codec pairs checked against JSON and independent CRC |
| [Lifecycle](../../tests/host/security_policy_input_lifecycle_tests.py) | 31 groups | Actual new app/store with real crypto and synthetic SDK/storage; four groups link the actual frozen console |
| [Readback](../../tests/host/security_policy_input_readback_tests.py) | 26 tests | New record, supported NVS layouts and conservative refusal cases |
| [Observation](../../tests/host/security_policy_input_observation_tests.py) | 17 tests | One capture, binding/closure/storage barriers and restoration separation |
| [Timing](../../tests/host/security_policy_input_timing_tests.py) | 11 tests | Ordered host marks, bounded output and contained observer failures |
| [Bundle](../../tests/host/security_policy_input_bundle_tests.py) | 22 tests | Typed image/source/original admission and refusal cases |
| [Runtime bundle](../../tests/host/security_policy_input_runtime_bundle_tests.py) | 16 tests | Exact dependency and runtime inventory admission |
| [Execution](../../tests/host/security_policy_input_execution_tests.py) | 19 tests | Authority, observation and independent restoration with simulated devices |
| [Operator](../../tests/host/security_policy_input_operator_tests.py) | 13 tests | Request admission and operator/launcher boundaries |
| [Composed operator](../../tests/host/security_policy_input_operator_integration_tests.py) | 11 tests | Actual worker dispatch and execution with synthetic devices; 112.900 seconds |
| [Isolated dispatch](../../tests/host/security_policy_input_isolated_tests.py) | 2 groups | Nine actual PowerShell-to-isolated-Python paths; 61.772 seconds |

Firmware tests cover exact idle/assembly boundaries, last-valid first input,
late first input, malformed prefix/hex/length, buffer exhaustion, loop exhaustion,
clock regression, no-data races, health loss, session refusal, storage uncertainty
and send failure. Lifecycle tests reuse the pinned dependency acquisition and
real native crypto proof; SDK and physical storage remain simulated. Native
C/C++ compilers are required; new target and harness compilation use warnings as
errors. These checks do not establish physical USB timing, physical entropy or
interrupted-flash behavior.

Focused firmware commands were run with the configured native compiler on PATH:

```powershell
C:/Python314/python.exe -B tests/host/security_policy_input_control_tests.py
C:/Python314/python.exe -B tests/host/security_policy_input_lifecycle_tests.py
```

## Applicable preflight and remaining gates

The [firmware porting checklist](../firmware-porting-lessons.md) applies. The
new target README records the implementation preflight. Board/configuration and
partition scope are inherited from the accepted Heltec WiFi LoRa32 V4.2 /
ESP32-S3, 16 MB setup: application at 0x10000, full 12,288-byte NVS at 0xd000.
This is nonradio; product BLE, display, buttons, battery and GNSS are not exercised.
SDK storage call wall-clock bounds and actual boot-to-input timing remain unproven.

Two initially absent builds passed with zero warnings. All seven artifact pairs
are identical; the [build report](../../tests/benchmarks/crypto/OT-198-INPUT-DIAGNOSTICS-BUILD-2026-09-11.json)
records 41 source pins and the ELF/map/config admission. The image is 438,912 bytes,
SHA-256 `5fc90c0d4c5f096227b4e1e33caed1ef8079ef515f1f04884074447464c25b6f`.
The generated configuration remains identical to the accepted predecessor,
including the 100 Hz FreeRTOS tick. Builds establish reproducible software
artifacts, not physical input or policy acceptance.

The composed operator and isolated-dispatch suites passed. Ordinary and hostile-
environment runtime probes passed through actual PowerShell child dispatch. The
runtime admits 3,558 files totaling 96,327,492 bytes; its 553,789-byte manifest has
SHA-256 `b05b870b718454e3d95db0622ae3cfc2d2553834208ee7877dd17882a8274eda`.
The [operator package record](../../tests/benchmarks/crypto/OT-198-INPUT-OPERATOR-PACKAGE-2026-09-11.json)
binds 20 source inputs. Actual-image composed worker validation passed with
synthetic devices. Offline normal/recovery admission also passed against actual
retained original files; those snapshots remain stale and do not establish live
custody or authorize a trial.

The complete host matrix remains required through the
[Host validation workflow](https://github.com/Limited-Underground/Trail/actions/workflows/host-validation.yml);
a duplicate local full run is not claimed. Required CI and publication are the
remaining gates. The workflow owns their current check status; this report does
not claim a pending run passed.

Hardware is deliberately skipped: this implementation task creates neither a
fresh snapshot nor permission to flash, reset or open a serial endpoint. A later
trial requires fresh exact scope, current device custody, one-use authority,
one diagnostic observation, complete A restoration before B, and guarded release
of an untouched role. A passing build alone does not authorize those actions.

No new physical policy acceptance, product crypto selection, V1 completion credit
or public website capability change is established.
