# Offline Map Activation Guard v0

Status: host-tested target-independent lifecycle policy, 2026-08-11

The map activation guard turns the fixed recovery behavior in the
[offline-map architecture gate](OFFLINE_MAP_ARCHITECTURE_V0.md) into a bounded
C++ state machine. It governs only whether a previously verified package may
be presented as available. It does not choose or implement a filesystem,
selector store, package format, renderer, display, transfer method, or map
provider.

## Fixed behavior

1. Boot accepts an active package only when the selector is unambiguous and the
   selected package has complete adapter-supplied evidence.
2. Missing, unreadable, or ambiguous selectors and invalid selected packages
   start mapless. The guard never guesses between packages.
3. Staging requires a different slot/generation plus valid manifest, permitted
   offline rights, available attribution, exact-byte integrity, compatible
   reader, readable index, sufficient storage, and read-only capability.
4. Staging does not change the active package. Cancellation or candidate-media
   loss keeps the current package selected.
5. Trial activation begins only after an external selector adapter reports that
   the exact staged slot and generation were durably selected and verified.
6. The prior package remains identified throughout a bounded trial. A policy-
   supplied number of complete reads must succeed before prior cleanup becomes
   permissible.
7. A failed read, deadline, monotonic-clock regression, or active-media loss
   enters explicit fallback-required state when a prior package exists.
8. Fallback completes only after an adapter reports the exact prior slot and
   generation restored, readable, and fully acceptable. Missing or mismatched
   fallback evidence becomes mapless.
9. When no prior package exists, the same faults become mapless immediately.
10. Mapless and fallback-required states require a visible unavailable notice.

The guard has no messaging, alert, position-sharing, radio, or USB control.
Those services therefore cannot be stopped through this interface. Target
composition still must prove that they actually remain available when map
operations fail.

## State flow

| Current state | Accepted event | Result |
| --- | --- | --- |
| `mapless` or `active` | fully evidenced candidate | `staged`; current map unchanged |
| `staged` | exact selector commit evidence | `trial`; candidate active, prior retained |
| `trial` | required complete reads before deadline | `active`; prior cleanup permitted |
| `trial` | read/time/media failure with prior | `fallback_required`; map unavailable |
| `trial` | read/time/media failure without prior | `mapless` |
| `fallback_required` | exact verified prior restored | `active`; failed candidate displaced |
| `fallback_required` | missing/mismatched prior | `mapless` |
| `active` | active media removed | `mapless` |

Invalid transitions and slot/generation mismatches fail without granting a new
active package.

## Evidence boundary

`MapPackageEvidence` is deliberately small and contains only:

- an abstract A/B slot and nonzero local generation;
- bounded package byte length; and
- Boolean results for manifest, rights, attribution, integrity, reader, index,
  storage, and read-only checks.

It contains no path, URL, geographic bounds, route, breadcrumb, package name,
participant/device identity, credential, key, or free text. The guard trusts
these typed results only as adapter inputs; it does not reproduce the checks in
[`OTMP0/v0`](OFFLINE_MAP_PACKAGE_MANIFEST_V0.md).

## Adapter obligations

Before calling `mark_selector_committed`, an eventual target adapter must write
and read back a recoverable selector without modifying either package. Before
calling `complete_fallback`, it must durably restore and verify the exact prior
selector. It must also:

- authenticate packages under a still-unselected signer/version policy;
- bind evidence to the exact package bytes and reader;
- keep normal mounts read-only;
- preserve the prior package through trial;
- make unavailable state visible; and
- keep communications and USB recovery independently serviceable.

No selector record/codec, filesystem operation, authentication decision, or
target adapter is implemented by this component.

## Current evidence

Ten deterministic host groups cover policy and boot fail-close, valid/invalid
selected packages, non-destructive staging, exact selector commitment, bounded
trial promotion and cleanup, verified fallback, no-prior degradation, deadline
and clock faults, candidate/active/fallback media removal, and refusal to guess
a removed prior package. The focused executable passes 100/100 repeats under
strict C++17 warnings-as-errors.

This is state-policy evidence only. It is not a real map, lawful provider
approval, package authentication, durable selector, filesystem integration,
renderer, physical display, or on-device recovery result.
