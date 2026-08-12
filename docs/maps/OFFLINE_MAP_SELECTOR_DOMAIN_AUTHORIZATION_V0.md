# Offline Map Selector Protected-Domain Authorization v0

Status: deterministic host-tested authorization handoff, 2026-08-11. No reset
executor, credential verifier, or on-device authentication exists.

This boundary implements the authority handoff required by the
[reset/replacement policy](OFFLINE_MAP_SELECTOR_RESET_REPLACEMENT_POLICY_V0.md)
without implementing either destructive operation. It can mint one boot-local,
non-copyable permit for future same-device map-domain replacement or fresh-
device domain commissioning. Only the separate
[trust-domain provisioner](OFFLINE_MAP_SELECTOR_DOMAIN_PROVISIONER_V0.md) can
consume it.

## Two exact scopes

The authorizer derives the required scope by running the existing lifecycle
policy over the exact request and reviewed state:

- `replace_same_device_domain` requires `protected_source_replacement` plus
  `same_device_source_missing_or_replaced`; and
- `commission_new_device_domain` requires `whole_device_replacement` plus
  `new_device_unprovisioned`.

Ordinary factory reset, selector reseed, temporary source unavailability,
retained state on purported new hardware, unknown/future values, and every
contradictory route fail before backend access.

## Media and fresh-domain binding

Every request binds a nonzero 128-bit proposed map trust domain. Same-device
replacement also binds a nonzero retired domain that must differ from the
proposal; first commissioning requires the retired domain to be all zero.
Common code checks only these invariants and exact echo. A future target must
obtain the values from approved continuity/secure-random evidence, keep them out
of logs, and durably bind them to the protected source and selector lifecycle
before execution is possible. The separate canonical
[`OTMD/v0` record](OFFLINE_MAP_SELECTOR_DOMAIN_RECORD_V0.md) now defines that
lifecycle data, and its separate
[two-slot store](OFFLINE_MAP_SELECTOR_DOMAIN_STORE_V0.md) provides recoverable
host persistence. The provisioner adds bounded preparation ordering without
giving the authorizer or store a general reset API.

Same-device replacement may describe either:

- selector media independently verified empty with reviewed generation zero;
  or
- retained selector media quarantined with a nonzero reviewed generation.

Quarantined means evidence only. The records cannot be imported into the new
domain through this permit. Fresh-device commissioning requires independently
verified empty selector media and generation zero. Unknown media, an empty/
nonzero mismatch, retained/zero mismatch, or retained media on a new device
fails before backend access.

## Target-owned verification

The injected backend owns credentials, administrator policy, physical-presence
verification, challenge strength, replay persistence, audit, and the committed
local confirmation screen. It must atomically consume the opaque authorization
handle before returning an authorized grant.

The common boundary accepts only local USB for these domain-level operations.
Authenticated local wireless, remote radio, and unknown transport are rejected.
The grant must echo the handle, derived scope, boot session, complete binding,
and a nonzero committed local-confirmation revision. It must also attest all six
reviewed confirmations:

1. explicit operator intent;
2. physical access;
3. expected map unavailability;
4. retirement of the prior protected history;
5. prohibition on retained-selector import; and
6. creation of a fresh trust domain.

These fields are reviewed intent evidence, not credentials or proof that a
human was present.

## Time and ownership

The caller selects a nonzero maximum lifetime no greater than the hard v0 limit
of 300,000 ms. Issue must precede expiry, the checked boot-local authorization
time must fall inside the half-open interval, and the grant must fit both
lifetime limits. Exact expiry is rejected.

The permit contains only scope, exact operation binding, boot session, and time
bounds. It is non-copyable; movement transfers the sole owner and invalidates
the source object. Reauthorization first invalidates an existing output permit.
The provisioner is the only friend allowed to burn this permit and rechecks
binding, boot, and use time before any I/O. Binding, boot, early use, exact
expiry, and replay failures consume or reject authority without storage access.

## Current evidence and limits

Ten deterministic groups cover same-device replacement, distinct new-device
commissioning, invalid routes, invalid domain/media/policy/request preflight,
backend denial/readiness/failure, handle/scope/USB/boot checks, exact binding
echo including retired-domain absence/reuse/new-device contamination, all six
confirmations, local revision, time boundaries, backend replay, output
invalidation, and move-without-copy ownership.

All twenty-four map suites pass 100/100 focused repeats in the complete
82-executable host matrix under strict C++17 warnings-as-errors.

This is authorization-handoff evidence only. It does not prove device
continuity, secure randomness, credential strength, physical presence, audit or
replay durability. The record store has no permit input or erase/reset
authority. The provisioner can prepare only exact fresh or replacement-domain
state and cannot reset/rebind an initialized source, import selector records,
or expose a map. Concrete target credentials, protected-source and storage
adapters, target lock/task, ESP-IDF composition, power-loss result, and
on-device behavior remain unimplemented and unproven. The separate
[stable trust-domain activation coordinator](OFFLINE_MAP_SELECTOR_DOMAIN_ACTIVATION_V0.md)
can activate only the exact durable pending state that the provisioner leaves.
