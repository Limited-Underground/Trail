# OT-177 publication CI correction

## Observed failure

GitHub run [34008325864](https://github.com/Limited-Underground/Trail/actions/runs/34008325864)
on `ac45b3148ee34af98c42b3a48aab1307ab7eab71` completed with failure.
Runner checkout and Python, .NET and GCC setup succeeded. The complete host
matrix stopped at `tests/host/future_concepts_tests.py:58`, whose exact title
list omitted the accepted optional client/repeater entry. The preceding run
[33936890877](https://github.com/Limited-Underground/Trail/actions/runs/33936890877)
shows the same assertion. This failure was reproduced locally before correction;
it is not evidence of local sandbox corruption.

## Correction and validation

The exact title and status lists now include all three accepted directions.
A scoped client/repeater check preserves post-V1 scope, separate V1 decision,
external-power guidance, endpoint security, no completion credit, and measured
three-radio acceptance with negative control and power/restart tests.

- `python tests/host/future_concepts_tests.py`: PASS, 10 scenario groups.
- `tools/Test-Host.ps1`: passed native/Python contracts, corrected governance,
  publication safety, signature vector and Windows loader checks, then failed
  the simulator Windows bridge privacy assertion (23 groups, one failure).
  Simulator core passed 33 groups. Remaining simulator checks had not run.
- The bridge test rejected the word `private` in an exception stack trace,
  matching the isolated checkout directory rather than leaked helper details.
  The assertion now requires the exact sanitized message, no inner exception,
  and absence of the actual injected helper detail in the complete exception.
- `tools/Test-WindowsSimulator.ps1`: PASS, exit 0 after the assertion correction;
  complete core, Windows bridge, Python helper/native protocol and UI checks.
  Together these runs cover the complete affected host matrix. The initial
  full command's exit 1 remains recorded; it was not rerun from the start after
  this test-only simulator correction.
- Remote correction publication and GitHub rerun: pending.

The earlier Android candidate validation remains separate and unchanged.
No application, firmware, hardware, support or release acceptance is added.
Every V1 milestone was reviewed against this test-only correction: none changes.
Android remains 60%; V1 remains exact 43.75% / displayed 44%.
The already-pending website qualification synchronization remains a separate gate.
