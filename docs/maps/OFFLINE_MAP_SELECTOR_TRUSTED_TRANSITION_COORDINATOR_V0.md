# Offline Map Selector Trusted Transition Coordinator v0

Status: deterministic host-tested protected-generation runtime composition,
2026-08-11. No protected target backend, ESP-IDF task, hardware counter,
physical durability, reset authority, or on-device result exists.

## Purpose

The ordinary runtime transition coordinator accepts caller-supplied current and
minimum generations so its selector-store behavior remains independently
testable. Those scalar values are not rollback protection when they come from
ordinary application state.

`MapSelectorTrustedTransitionCoordinator` composes that existing coordinator
with `MapSelectorTrustedGeneration`. It derives both generation values from the
protected source, applies each operation to a private activation guard, and
does not publish a map-capable result until selector storage and protected
history agree exactly.

## Ordering

The live guard must be running in active, trial, or fallback-required state.
The target must hold exclusive ownership of both selector storage and the
trusted-generation source for the complete call. The coordinator then:

1. inspects protected history before any selector-store access;
2. supplies that exact generation as both the current selector generation and
   rollback floor to the ordinary transition coordinator;
3. lets selector verification, private lifecycle mutation, commit-last save,
   and exact selector readback finish;
4. rechecks protected history when the selector generation is unchanged, or
   atomically advances and exactly reads it back after a saved transition; and
5. publishes the private guard only when the final protected value exactly
   matches the selector generation that would become live.

Healthy trial reads and rejected operations do not write selector state, but
they still require a final exact protected-source recheck. Trial promotion,
trial failure/deadline, valid fallback completion, and prior cleanup must save
the selector first and advance protected history second.

## Fail-closed outcomes

- A stopped or already-mapless guard is not transitionable and causes no
  trusted-source or selector-store access.
- A failed initial trust read blocks selector-store access and contains any
  currently visible map as unreadable or ambiguous mapless state.
- A zero, stale, rolled-back, conflicted, or live-mismatched selector cannot be
  labelled current by caller state and remains mapless.
- A failed or uncertain protected advance occurs after selector persistence may
  have completed, so the new selector remains unexposed and fresh-boot
  reconciliation is required.
- A changed final protected value after a volatile operation also removes map
  exposure rather than publishing state checked against an obsolete floor.

Invalid fallback evidence deliberately verified-clears the two selector
records through the ordinary coordinator. Protected history is not lowered or
erased by that operation. The result is therefore typed as
`protected_history_retained`, remains mapless, and requires an independently
authorized service/reconciliation path; it is not a normal first-use state.

The result retains typed transition, trusted-source, containment, and ordering
evidence. It contains no package bytes, map paths, coordinates, device or
participant identity, credential, key, URL, or free text.

## Evidence and limitations

Eleven deterministic groups cover protected recheck for volatile and rejected
operations, selector-save-before-trust ordering for every persistent operation,
initial trust failure and storage isolation, zero/stale trust, exact live-state
mismatch, commit-uncertain protected advance, final-value change, retained
protected history after selector clear, and stopped/mapless ownership
isolation. The suite passes 100/100 focused repeats; the complete 71-executable
host matrix and publication-safety scan pass locally under strict C++17
warnings-as-errors.

This is common host composition, not a lock or protected backend. Candidate
replacement, first baseline, service reseed, and protected reset/replacement
recovery remain separate gates. Target serialization, NVS and counter backends,
authentication, package/media behavior, power-loss interruption, flash
replacement, wear, physical attack resistance, renderer behavior, and on-device
runtime transitions remain unproved.
