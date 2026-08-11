# Offline Map Selector Boot Coordinator v0

Status: deterministic host-tested ordering boundary, 2026-08-11

The boot coordinator closes the restart ordering gap between the
[`OTM0/v0` checkpoint](OFFLINE_MAP_SELECTOR_CHECKPOINT_V0.md), the
[two-slot store](OFFLINE_MAP_SELECTOR_STORE_V0.md), and the
[map activation guard](OFFLINE_MAP_ACTIVATION_GUARD_V0.md). It keeps restored
guard state private until any boot-created state change has been committed and
verified. It does not choose or implement a physical storage backend.

## Release ordering

The live guard must be stopped when boot begins. The coordinator then:

1. restores the newest unique checkpoint into a private guard while enforcing
   the caller's optional minimum generation;
2. revalidates the exact policy plus selected and prior package evidence;
3. leaves stable active and already-fallback-required state unchanged;
4. for a resumed trial, saves the incremented trial-boot count as a new
   commit-last record only if the generation restored at preflight is still
   current;
5. when the trial boot limit is reached, saves the transition to
   fallback-required as a new record;
6. waits for the store's exact committed readback; and
7. publishes the private guard only after all required persistence succeeds.

The candidate map can be exposed only from `active_ready` or `trial_ready`.
An existing fallback requirement is published with no map, and a new boot-limit
fallback is published only after the transition is durable.

The exact-generation save closes the read-only-restore-to-save gap but is not
a lock. The target must keep exclusive selector-store ownership for the whole
boot operation.

## Typed outcomes

| State | Map exposure | Meaning |
| --- | --- | --- |
| `mapless_ready` | no | no checkpoint exists; the map subsystem is explicitly mapless |
| `active_ready` | yes | stable state and exact package evidence restored |
| `trial_ready` | yes | resumed trial count was incremented, committed, and read back exactly |
| `fallback_required` | no | the exact prior package must be restored through the lifecycle guard |
| `service_required` | no | storage, conflict, rollback, checkpoint, or persistence evidence failed |

For storage failure, invalid media, generation conflict, rollback-floor
rejection, checkpoint mismatch, prepared-write failure, uncertain commit, or
bad readback, the restored candidate is never assigned to the live guard. When
the policy itself remains valid, the coordinator publishes a fresh mapless
guard with unreadable or ambiguous selector status so the UI can remain
fail-visible. A dirty caller-supplied live guard is never replaced.

An unreadable slot is service-required even if its peer is valid because it
could conceal a newer committed record. A known empty/invalid peer may allow a
unique valid active checkpoint to run with `repair_required` set. Repair is not
performed silently at boot.

## Trusted-generation boundary

The caller may supply a minimum generation. Restore below it is rejected and a
trial/fallback save advances beyond both the valid local generation and that
minimum. The coordinator does not create, persist, authenticate, advance, or
protect the external floor. This is ordering evidence, not anti-rollback
protection.

## Data and authority limits

The interface carries typed policy, package evidence, generations, guard/store
results, and booleans only. It contains no path, filename, geographic content,
participant/device identity, credential, key, URL, or free text. It cannot open
or render a map, authenticate a package, mount or repair storage, erase map
data, control a radio, stop messaging, alter alerts or position sharing, or
change USB recovery. Map failure remains independent of communications.

## Current evidence

Ten deterministic host groups cover stable release without rewrite,
persist-before-release trial resume with an external floor, existing fallback,
persisted trial-limit fallback, prepared-write failure, commit ambiguity and
corrupt readback, empty mapless boot, unreadable/conflicting/stale media,
package rejection, invalid policy, and dirty live ownership. The coordinator
passes 100/100 focused repeats under strict C++17 warnings-as-errors.

No ESP32 task binding, NVS/flash/SD adapter, atomic-byte guarantee,
wear/endurance result, protected generation source, package authentication,
physical power interruption, filesystem, renderer, display, or on-device boot
result is claimed.
