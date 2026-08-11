# Update recovery diagnostic event v0

Status: host-tested redacted diagnostics adapter with a separate strict offline
decoder, 2026-08-11. No target log backend, persistent audit retention, remote
export, or physical operator workflow is claimed.

## Purpose

`record_update_recovery_status` converts one coherent redacted recovery status
into one event through OpenTrail's existing bounded logger. It does not create a
second diagnostics subsystem or bypass compile/runtime filtering and sink
backpressure.

The logger record is always:

- component: `update-recovery`;
- message: exactly `OTRD0=XXXXXXXX`, with one uppercase eight-digit word;
- privacy: public, because the encoded word has already removed sensitive
  detail; and
- severity: derived only from the coarse operator state.

Observed and trusted generations remain available in the live status but are
intentionally omitted from `OTRD0`. Policy, hardware/candidate identity,
addresses, keys/handles, checkpoint contents, raw adapter errors, and nested
coordinator results cannot enter this interface.

## Versioned 32-bit word

| Bits | Field |
| --- | --- |
| 0-1 | operation: boot, save, or transition |
| 2-5 | operator state |
| 6-10 | operator reason |
| 11-13 | required action |
| 14 | source operation succeeded |
| 15 | normal operation blocked |
| 16 | operator attention required |
| 17 | reboot required |
| 18 | trial confirmation required |
| 19 | terminal cleanup required |
| 20 | sensitive source detail was redacted; must be one |
| 21-23 | reserved; must be zero |
| 24-27 | format version, currently zero |
| 28-31 | fixed magic nibble `0xD` |

The canonical clean-baseline word is `0xD0105084`, logged as
`OTRD0=D0105084`.

## Coherence and failure behavior

Encoding and decoding independently validate known enum ranges, the exact flags
and action for each state, and allowed operation/state/reason combinations. A
boot rollback cannot masquerade as transition confirmation, a service result
cannot claim normal operation, and clearing the redaction bit is invalid.

Decode rejects wrong magic, unsupported versions, nonzero reserved bits,
unknown enums, altered flags, and incoherent state/reason/action combinations.
Encoding failure occurs before the logger changes.

## Severity and logger behavior

| State | Level |
| --- | --- |
| operational, persistence committed | info |
| trial active, transition rejected, rollback, cleanup, reboot reconciliation | warn |
| safe mode, service required | error |

Normal logger threshold filtering is an accepted non-write and remains
distinguishable from a sink rejection. A full or failing sink increments the
existing logger backpressure counter and the adapter returns `sink_rejected`;
it does not claim the event was accepted.

The production-facing [bounded RAM ring](RING_LOG_SINK_V0.md) overwrites its
oldest record when full, so normal rollover is an accepted write with a visible
overwrite count rather than a sink rejection. Malformed writes still reject.

## Host evidence

Eight deterministic groups cover:

1. the exact clean-baseline word, fixed message, and decode round trip;
2. trial, successful save, and rejected-transition forms;
3. rollback, cleanup, reboot-reconciliation, safe, and service forms;
4. one canonical message at info, warning, and error severity;
5. threshold filtering versus sink rejection;
6. incoherent and unknown status rejection before logging;
7. bad magic, version, reserved bits, enum, flags, and redaction; and
8. fixed, trivially copyable, generation- and identifier-free shape.

The diagnostic adapter and ring integration focused executables each pass
100/100 repeats. The complete 58-executable
OpenTrail host matrix plus all Python and publication-safety checks pass.

The separate
[strict offline decoder](UPDATE_RECOVERY_DIAGNOSTIC_CLI_V0.md) accepts only the
canonical uppercase message and exposes stable v0 category names. Its ten
operator groups and canonical/invalid CLI smoke checks also pass.

## Remaining gates

- bind and serialize the RAM ring in an exact target task composition;
- define authorized persistent retention, export, and deletion;
- prove power-loss behavior for any persistent log backend;
- render decoded actions accessibly on candidate displays; and
- capture real service/reconciliation events under physical storage/trust
  failure injection.
