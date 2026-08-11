# Offline Map Selector Protected First-Baseline Coordinator v0

Status: deterministic host-tested protected first-use composition, 2026-08-11

This coordinator composes the ordinary
[first-baseline coordinator](OFFLINE_MAP_SELECTOR_BASELINE_COORDINATOR_V0.md)
with the
[trusted-generation source](OFFLINE_MAP_SELECTOR_TRUSTED_GENERATION_SOURCE_V0.md).
It creates the first stable selector only when protected history confirms that
the selector domain has never been used, and it does not expose the map until
both durable records agree on generation 1.

## Accepted starting state

Every call requires all of the following:

1. the live guard is running, mapless, and specifically reports
   `no_selector` with no active, previous, or staged slot;
2. the protected-generation source is usable and reports exactly zero;
3. the activation policy and package evidence satisfy the ordinary baseline
   boundary; and
4. the caller exclusively owns selector storage and the protected source for
   the complete operation.

A nonzero protected value proves prior selector history even when both local
selector slots are empty. The coordinator therefore blocks selector access,
publishes an ambiguous mapless state, and requires reconciliation. It never
turns a used or erased selector domain back into first use.

## Save, protect, then publish

The successful order is fixed:

1. inspect protected history before selector storage;
2. require the protected value to be zero;
3. run the ordinary first-baseline operation against a private guard;
4. save and exactly read back canonical stable selector generation 1;
5. atomically advance protected history from 0 to 1 and exactly read it back;
6. publish the private active guard only after both values equal 1.

The first package is therefore never visible merely because its selector save
succeeded. Protected history must independently confirm the same generation.

## Failure behavior

- A retryable initial protected-source read failure leaves the exact clean
  `no_selector` state unchanged, mapless, and retryable. Selector storage is
  not touched.
- A latched or reconciliation-required protected-source failure publishes an
  ambiguous mapless state without selector access.
- Nonzero protected history blocks selector inspection and storage.
- Invalid package evidence or policy, a non-clean owner, an existing selector,
  or selector-save failure never advances protected history and never exposes
  the map.
- Once selector generation 1 has been saved, any protected advance failure,
  conflict, unavailable readback, or unexpected final value publishes an
  ambiguous mapless state and requires fresh-boot reconciliation. The
  operation is not silently retried as clean first use.

Map unavailability does not stop radio, messaging, alerts, position sharing,
vehicle integration, or USB recovery.

## Current evidence and limits

Eleven deterministic host groups cover successful save-before-protect ordering,
zero-to-one exact advance/readback, nonzero history isolation, retryable and
latched initial trust failure, invalid evidence, non-clean owners, existing
selector state, selector-save failure, uncertain applied advance, post-save
conflict, final-value change, and invalid-policy containment. The suite passes
100/100 focused repeats. All fifteen map suites and the complete 73-executable
host matrix pass locally under strict C++17 warnings-as-errors, including the
publication-safety scan.

This is common host composition only. It is not a lock, credential, protected
counter implementation, reset or replacement authority, service-reseed flow,
physical package or storage adapter, power-loss result, filesystem, renderer,
target task, or on-device result. Concrete target composition must serialize
selector storage, protected history, and package ownership for the entire call.
