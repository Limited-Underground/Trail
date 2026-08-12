# Revision-Bound Local Breadcrumb Archive Consent v0

Status: **host-tested local consent/control boundary over an explicit durable
session range; no rendered UI, target composition, or physical claim**,
2026-08-12.

## Purpose

The private archive runtime owns execution, but it must not be callable from a
radio message, server request, automatic recovery path, or stale local event.
This boundary defines the only current common-code entry for archive start and
stop: an action already resolved by `CheckedLocalInterface` against the exact
active local confirmation-frame revision.

## Canonical local frames

The local interface now recognizes one archive-confirmation screen with two
canonical forms:

| Operation | Notice | Slot 0 | Slot 1 | Gesture |
| --- | --- | --- | --- | --- |
| Start | `archive_start_confirmation` | `confirm_archive_start` | `cancel` | Hold required for Start |
| Stop | `archive_stop_confirmation` | `stop_archive` | `cancel` | Activate/tap for Stop |

Both use informational attention and exactly two enabled slots in that order.
Start requires a target that supports hold. Archive start/stop actions are
invalid on home, status, system-fault, or any other screen.

As with every local action, the physical event must name the exact successfully
presented frame revision and action slot. Stale, disabled, out-of-range,
wrong-gesture, failed-input, and pre-frame events do not resolve.

## Start behavior

`BreadcrumbArchiveConsentController` accepts only a successful
`ResolvedAction`. A confirmed start:

1. reads `CheckedMonotonicClock` once;
2. defers without a runtime call when the clock is temporarily not ready;
3. fails without a runtime call on rollback/source fault;
4. submits the next nonzero session ID within its caller-supplied inclusive
   lease range and the checked time through the private serialized runtime
   owner; and
5. consumes the session ID only after successful start or an uncertain
   post-operation unlock failure.

Runtime lock contention does not consume the candidate ID, so a new explicit
local hold may retry it. Session rejection (for example, already active) also
does not consume it. A session ID is never reused after an uncertain result.
Consumption of the lease's final ID permanently exhausts Start for that
controller instance. It never wraps, extends, or silently obtains another
range. Target boot composition must first obtain the explicit first/final
bounds from the
[restart-safe archive session lease store](../persistence/BREADCRUMB_ARCHIVE_SESSION_LEASE_STORE_V0.md).

The session ID is opaque boot-local ordering metadata, not a user/device/account
identity, key, credential, or remotely accepted authorization token.

## Stop and cancel behavior

Stop is deliberately clock-independent so the user can request privacy stop
even when the time source is unavailable. It still passes through the private
serialized runtime lock. Cancel performs no clock read and no runtime call.

A completed Stop is idempotent at the underlying capture session. Lock
contention defers. Lock/unlock failure stays typed by the private owner and
latches the optional runtime closed; this controller does not issue an unsafe
unlocked compensating command.

## Authority boundary

The controller has no radio, group packet, server, endpoint, remote-command,
reboot, timer, or automatic-start input. It does not expose capture service,
upload, outbox, discard, export, deletion, retention, or retrieval operations.

This is a common-code structural boundary, not proof that target firmware has
no second call path. The selected ESP32 composition must keep the private
runtime owner inaccessible except through approved local consent and its
non-control scheduling/UI services.

## Host evidence

Eleven deterministic scenario groups plus 100/100 focused repeats cover:

1. canonical hold-only Start presentation and resolution;
2. canonical immediate Stop presentation and resolution;
3. invalid revision/mode/capability and wrong-screen rejection;
4. stale and unsupported actions making no clock/runtime call;
5. checked-time Start and nonzero session sequence consumption;
6. Cancel making no clock/runtime call;
7. clock-independent local Stop;
8. clock not-ready/failure containment;
9. runtime contention preserving the candidate for a new explicit retry; and
10. uncertain session consumption and zero-seed rejection; and
11. inclusive durable-range exhaustion, including invalid range rejection.

The complete 103-executable host matrix and Python evidence checks pass. This is
not rendered-display, physical button/touch, localization/accessibility,
distracted-use, target lease/workflow composition, ESP-IDF, or on-device
evidence.

## Next gates

- Keep the host-tested
  [complete UI workflow](BREADCRUMB_ARCHIVE_WORKFLOW_COORDINATOR_V0.md) as the
  only target composition path to this controller.
- Decide how the local menu exposes archive controls without making the optional
  service prominent, confusing, or accidental.
- Preserve the host-tested
  [lease-to-workflow bootstrap](BREADCRUMB_ARCHIVE_WORKFLOW_BOOTSTRAP_V0.md)
  so a committed non-identifying lease exists before the target makes archive
  Start available.
- Bind and evaluate physical touch/button behavior, readable consent wording,
  accessibility, and explicit active-state indication on frozen hardware.
