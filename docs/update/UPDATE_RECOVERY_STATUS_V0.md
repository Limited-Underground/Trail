# Redacted update recovery operator status v0

Status: fixed-shape host boundary with deterministic coherence and privacy
tests. No target logger, renderer, persistent audit store, ESP-IDF binding, or
physical operator workflow exists.

## Purpose

Boot, normal save, and trial-time transition coordinators deliberately retain
detailed internal evidence. A display, logger, or service interface must not
copy those raw results and accidentally expose hardware/candidate identity,
checkpoint contents, adapter errors, or nested storage state.

`UpdateRecoveryStatus` converts each coordinator result into one bounded,
pointer-free operator record. It answers only:

- which recovery operation produced the record;
- the coarse operator state, reason, and required action;
- observed and trusted generation evidence when safely available; and
- whether the operation succeeded, normal operation is blocked, attention,
  reboot, confirmation, or cleanup is required.

## Fixed operator vocabulary

The operation is `boot`, `save`, or `transition`. Operator states are
operational, trial active, persistence committed, transition rejected,
rollback required, cleanup required, safe mode, reboot reconciliation required,
or service required.

| Operator state | Required action |
| --- | --- |
| operational | continue normal operation |
| trial active | continue the trial and require later confirmation |
| persistence committed | no additional action from this save result |
| transition rejected | no durable transition occurred |
| rollback required | reboot to the baseline image |
| cleanup required | run the separately authorized terminal cleanup path |
| reboot reconciliation required | reboot and reconcile checkpoint/trust state |
| safe mode or service required | keep normal operation blocked and enter service |

The reason remains coarse but distinguishes clean baseline, confirmation,
rollback/timeout, generation conflict or exhaustion, checkpoint rejection,
storage/trust failures, and uncertain commit. It is not a raw backend error.

## Fail-closed coherence

Mapping is accepted only when the source state, reason, action flags,
generations, operation, guard outcome, before/attempted/live states, and nested
persistence result agree. Examples include:

- a trial-ready boot must allow the application, require confirmation, and
  carry one exact nonzero active/trusted generation;
- a committed save must have a readable prior checkpoint, a successful save,
  and exact committed/save/trusted-readback generations;
- a committed confirmation must originate from `confirm`, start in trial,
  end confirmed, and contain a coherent committed persistence result; and
- failed durable transitions must leave the original lifecycle state
  unpublished, mark the guard stopped, and match the nested recovery state.

Unknown enum values, default/incomplete results, or contradictory evidence map
to `service_required / invalid_result / service`. Normal operation remains
blocked and no inherited continue, confirmation, cleanup, or reboot claim is
emitted.

## Redaction boundary

The status contains no policy, hardware ID, candidate, radio or peer address,
key or handle, checkpoint payload, trusted/backend error enum, guard error, or
nested coordinator result. Compile-time shape checks enforce the absence of
identity/candidate/checkpoint/error/persistence members. The record is
trivially copyable and limited to 32 bytes for bounded target diagnostics.

This boundary reduces accidental exposure; it does not authorize publishing
generation values. A future persistent/public diagnostic format may omit or
further aggregate them and must receive its own versioned privacy review.

## Evidence and limitations

Eight deterministic groups cover baseline/trial boot, rollback and terminal
cleanup, safe/service/reconciliation boot outcomes, all save states, volatile
and rejected transitions, committed confirmation and rollback, three durable
persistence-failure routes, structural redaction, and unknown/incoherent input.
The focused executable passes 100/100 repeats, and the complete 47-executable
OpenTrail host matrix plus all Python and publication-safety checks pass.

The separate versioned [`OTRD0` diagnostics
adapter](../diagnostics/UPDATE_RECOVERY_DIAGNOSTIC_EVENT_V0.md) omits both
generation values and records only this boundary's coarse outcome through the
existing logger.

This is host decision-shape evidence only. Target scheduling, reboot and
rollback execution, authorized cleanup/reset, display wording, logging and
retention, protected storage/trust, physical interruption, and service-person
usability remain open.
