# OT-0238c computer-only timing isolation — 2026-10-06

This investigation isolates possible causes of the prior COMMIT B deadline failure. It did not change production code or repeat owner, device, UI, or grant steps.

## Evidence and findings

The review reused the saved physical timeline and six accepted confirmations as timing inputs. It used fresh synthetic protocol fixtures; no expired physical token or authorization was reused. The saved sequential checkpoint intervals total at least 102.639714199 seconds of the shared 120-second window, leaving at most 17.360285801 seconds. Final scope-revalidation tails are charged separately.

Twelve focused Python cases ran the actual controller and USB client with simulated endpoint/target behavior. Five injected COMMIT B deadline boundaries produced the same recorded failure category while varying possible delivery state. They show why the historical COMMIT B delivery/apply state remains unknown; they do not identify which boundary occurred during the physical trial.

Four saved startup commands averaged 7.007–7.314 ms of guard work. This is a sensitivity input only, not measured COMMIT latency. The C++ supplement exercised actual target runtime with separate synthetic timing: it accepted a request after 119 seconds, rejected one after 121 seconds, and passed a separate six-confirmation GPIO plus COMMIT/READY chain. It did not replay the recorded 102.6397-second controller schedule or combine the actual controller and target.

Independent review accepted the bounded diagnostic scope. No deadline, guard, or production behavior changed. No accepted production fix, physical timing fit, full OT-0238c completion, or V1 credit follows from these probes.

## Next gate

Continue computer-only work with source-bound COMMIT decision-point telemetry using existing private query counters. Preserve all current deadlines and fresh checks; do not repeat already-passed owner steps for diagnosis.

## Evidence

- [Timing isolation review and limits](../../.private/ot0238c-timing-isolation-20261006/timing-isolation-review.md)
- [Independent review receipt](../../.private/ot0238c-timing-isolation-20261006/timing-isolation-review.json)
- [Python controller/endpoint probe receipt](../../.private/ot0238c-timing-isolation-20261006/timing-probe-receipt.json)
- [C++ target clock probe receipt](../../.private/ot0238c-timing-isolation-20261006/target-clock-probe-receipt.json)
- [Physical timing and confirmation source](../../tests/hardware/OT-0238c-FIRST-2026-10-06.md)

