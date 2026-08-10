# Privacy-Safe Field-Test Log v0

Status: host tool and public-summary contract, 2026-08-10

`OTFL0` separates private raw hardware captures from evidence that can be safely
published. The raw file may temporarily contain transport ports and direction
labels needed for recovery. The public summary retains aggregate measurements
under neutral labels and rejects identity-, location-, or secret-bearing fields.

## Workflow

1. Run a bounded hardware test with its recoverable local journal and raw output
   under the ignored `build/hardware-test-state` directory.
2. Convert the raw result with `tools/field_test_log.py summarize`, supplying the
   verified hardware/firmware and radio profile.
3. The converter maps devices to `client-1`, `client-2`, and `repeater-1`, writes
   atomically, and refuses to overwrite an existing public record unless
   `--overwrite` is explicit.
4. Run `tools/field_test_log.py validate` before committing the summary.
5. Keep the raw capture local; publish the aggregate `OTFL0` JSON plus a concise
   dated Markdown interpretation.

## Public record

The v0 record contains:

- schema/version and a non-identifying session label;
- staged-test phase, UTC start/end, duration, broad location class, and coarse
  motion state;
- client/repeater counts and whether repeater necessity was actually proved;
- human-readable hardware model and firmware version without serials;
- frequency, bandwidth, spreading factor, coding rate, transmit powers, and
  repeater start/end state;
- attempted, delivered, lost, duplicate, interval, and ordered latency summary;
- aggregate per-role radio/core/error/queue/airtime counter deltas;
- verified cleanup counts and lease-journal removal; and
- a canonical privacy declaration.

## Rejected public content

Validation recursively rejects keys for transport ports, serial numbers,
addresses, MACs, keys, secrets, PINs, channel names, latitude, longitude, and
coordinates. It also rejects COM-port and MAC-address patterns in string values.
The schema has no raw-payload, per-message route, participant-name, or precise
location field.

Counts and structure also fail closed: delivered plus lost must equal attempted;
latency minimum/median/p95/maximum must be ordered; hardware/client counts must
match topology; radio bounds and Boolean states are checked; cleanup counts
cannot exceed expected clients; required counters, models, and firmware labels
must be present.

This is a publication boundary, not an anonymity guarantee. An operator must
still review free-form external material and avoid publishing a route/time
combination that could identify a participant or private property.

## Current evidence

The first generated record is
[`OT-022-2026-08-10.json`](../../tests/hardware/OT-022-2026-08-10.json), with the
interpretation in
[`OT-022-2026-08-10.md`](../../tests/hardware/OT-022-2026-08-10.md).
Four deterministic host groups cover redaction, count/latency/topology
coherence, prohibited keys/values, and atomic overwrite refusal.
