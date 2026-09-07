# OT-170 / OT-178 automatic display clock and connected-device name

## Scope and checkpoint — 2026-09-07

The owner reported that the second Heltec displayed a time one hour off after
reconnection, and both phone connection cards said `Authorized device 1`.
Previously, reopening the connection did not synchronize the display clock;
the card used a phone-local authorization label instead of the device name.

This Android-only correction reads the current protected device name and then
synchronizes the display clock after authenticated Ready. It uses the existing
configuration protocol and saved ownership. It does not make app launch start
the Bluetooth service automatically or add radio messaging.

Firmware remains the accepted `ot178-phone-v1` application, 586736 bytes, SHA256
`43AC6DBC506C03FAA63AAE7A8E27195750598F06F3118BFFF1EF7AF84BC8D9F2`.
No firmware source, image, stored region, pairing policy or radio authority is
changed. Firmware build/flash preflight is not applicable to this APK increment.
Existing firmware evidence is retained in
[OT-178 phone status](OT-178-PHONE-STATUS-2026-09-07.md).

## Automatic configuration behavior

Each current authenticated configuration session queues one name read followed
by one clock synchronization. The runtime waits for an occupied action or
configuration lane instead of interrupting it. Clock changes coalesce while that
lane is busy. Responses and configuration timeouts advance queued work; an
uncertain operation is consumed without an automatic retry loop. Disconnect,
session replacement and lifecycle teardown clear the pending work and watcher.

Clock synchronization samples the phone's local civil time and 12/24-hour
preference through the existing challenge/response exchange. A session-owned
Android watcher requests another synchronization for system time or timezone
changes, locale changes, and changes to the explicit 12/24-hour setting. Minute
ticks detect an offset transition such as daylight saving time. A six-hour
refresh precedes the firmware's existing 24-hour expiration. Elapsed-realtime
checks on minute ticks also detect an overdue refresh after phone sleep; this
does not promise delivery while Android is asleep or the devices are disconnected.

The connection card and device-settings screen use the current protected name
readback. A name without a confirmed revision is not presented as current.
Pending readback displays `Connected to Trail device`; a confirmed empty name
displays `Connected to unnamed device` with a prompt to assign a recognizable
name. The presentation does not derive identity from a discovery ordinal or a
cached editable name draft.

## Android callback ordering

A stream indication may arrive before Android acknowledges the preceding GATT
write. The existing operation gate permits that indication, but a subsequent
write previously required the gate to be Ready already. Automatic chained
configuration makes that ordering relevant immediately after connection.

The adapter now retains at most one copied successor command per GATT lease.
The successful write acknowledgment consumes it once, after checking the current
GATT and characteristic, permissions, bond prerequisite and a five-second
elapsed-time deadline. Expiry, teardown or an uncertain write closes the lease
and discards queued bytes. Invalid or extra commands cannot replace the queued
successor. The queue is not persistent and does not replay uncertain commands.

## Validation checkpoint

Focused Android validation passed in two runs: 58 tests before the final queue
addition, then 56 focused tests covering the updated implementation. These are
separate run totals, not a sum of distinct tests. Queue coverage exercises both
callback orders, copied ownership, the one-successor limit, invalid input,
teardown and uncertain-write/duplicate-acknowledgment rejection. Independent
review checked runtime lane release, watcher lifecycle, current-name presentation,
deadline handling and protected queue draining.

The complete affected Android matrix passed with exit 0: protocol 51, debug 311,
release 310 and V1-Test 362, totaling 1,034 tests with zero failures, errors or
skips. All variant builds and lint, the debug Android instrumentation-test build,
and the release artifact audit passed. Building the instrumentation-test APK is
not a claim that those tests were executed on a phone.

The new V1-Test APK is 12495724 bytes, SHA256
`CCC4F1EBC544F5678E623E6AFD5346553EE259F39A3CDF40319E5FBAB87B1A93`.
It retains the accepted signer, V1-Test package and version 1. Both phone
installations passed exact installed-APK byte readback. Existing base apps, data
and phone settings were preserved. This increment did not access or flash either
board; both retain the previously accepted firmware.

## Physical acceptance

The updated V1-Test app was launched on the Note20 SM-N986U / Android 13 and S24
Ultra SM-S928U / Android 16. Each used the existing Device / Find device / Start
Bluetooth device service flow. No Read device or Sync display clock button was
pressed afterward. This retains the manual service-start prerequisite; it does
not claim zero-tap reconnection simply from launching the app.

The Note20 displayed `Connected to Trail Bench`, the matching last device
readback, and `Display clock synchronized.` automatically. The S24 displayed
`Connected to unnamed device`, its naming prompt, and `Display clock synchronized.`
automatically. Its unnamed result reflects the protected empty name rather than
an invented device number. The owner explicitly confirmed that the second Heltec
now displays the correct time.

The original Heltec display was not visually confirmed during this increment.
Live timezone/DST/locale/12/24-hour changes and a real six-hour refresh were not
physically exercised; those watcher paths retain software evidence only. No radio
exchange, pair-isolation, new firmware or release acceptance is added. V1 remains
exact 45.50 / displayed 46. The next work remains the OT-163 restart-acknowledgment
boundary and secure two-pair message path using current per-device recovery
images. Website updates and cold-power disassembly remain owner-deferred.
