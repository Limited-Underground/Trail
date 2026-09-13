# OT-214 Device-owned group confirmation plumbing

2026-09-13. OT-214 connects the existing Android Group screen through Activity, binder and service ownership to a one-use device-offer coordinator. 1164 Android tests pass with zero failures, errors or skips; all three app variants pass lint/build and the release remains unsigned. Production has no offer adapter and stays unsupported. No membership, radio, device/UI acceptance, crypto selection, V1 credit or public website status change. Local/uncommitted.

## Behavior and limits

The existing Group page presents the complete adapter-admitted peer fingerprint and
confirmation, and returns the exact offer handle for confirm/cancel. Names, PINs and
local roster models do not supply this authority. An injected device adapter remains
disabled in production; this increment does not add a BLE wire format or firmware.

The service owns the current protected BLE session and original monotonic deadline.
Expiry, clock rollback, stale/reconstructed handles, changed authority, disconnect,
source failure and reentry refuse the attempt. Submission is one-use and does not
prove joining or cancellation completed. A submitted request still expires. Reset
intent retains the closed session so cancellation cannot reopen its offer. Inline
timer callbacks close safely. No confirmation state is saved by the UI.

One attempt is admitted per protected BLE session. New sessions require fresh device
admission; local code cannot prove persistent device consumption. Device-result
handling, durable commit/readback, both-node confirmation, join/message/revoke/rekey,
reset and remaining entropy/interruption/corpus gates are still required. No final
membership or traffic activation API is added.

## Validation

The offline Gradle matrix ran protocol tests, debug/release/V1-Test unit tests,
lint and APK assembly for all app variants, and debug instrumentation APK assembly.
1164 tests passed, zero failed/errored/skipped. Instrumentation was compiled,
not run on a phone. The unsigned-release audit passed. Exact commands, logs, XML
suite counts, source and artifact hashes are retained privately in
`.private/ot214-confirmation/result.json` and `run-validation.ps1`.

Initial Gradle version discovery was denied only on the cache lock by the sandbox;
the same existing offline cache succeeded with approved access. This was an
environment failure, not a compiler failure. Independent review identified and
corrected reset/reentry/timer lifetime issues before the final matrix.

## Next acceptance

Implement the device-backed offer/confirmation adapter on the selected-for-evaluation path, with authenticated protected-BLE admission and device-result handling; then validate the actual two-node workflow. Production crypto/traffic activation remains gated by Phase 3 and explicit selection.
