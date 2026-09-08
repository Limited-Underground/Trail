# OT-171 Pairing flow and bounded keypad investigation

## Accepted preceding hardware observations

On September 7 the owner authorized resetting the second Heltec V4 bench device
for a fresh Android pairing test. The S24 Ultra (Android 16/API 36) ran the
previously admitted C577 V1-Test APK with second-device `ot171-label-v1` firmware
(984E artifact). The original Heltec/Note20 pair remained untouched.

The app displayed Factory reset verified. No recognizable Trail or NimBLE entry
was visible in Android Settings; that observation does not establish old-bond
deletion. An initial authorization lost its connection; PIN-window expiry was
possible but not proven. A later attempt reached authenticated Ready, and the
app acknowledged automatic display-clock synchronization.

The matched setup label prefilled the still-unsaved device-name editor. Explicit
Apply saved the owner-selected Trail Bench 2 name, confirmed by protected
readback. A fresh region read confirmed US915 after explicit restoration. Radio
transmission remained disabled. This accepts the initial name suggestion on the
existing APK, not Activity rotation or a new firmware/app artifact.

## Keypad disposition

The owner requested a short final feasibility check and deferral if no simple
fix emerged. The September 8 source review found existing DisplayOnly I/O,
MITM protection and Secure Connections; Android calls ordinary createBond.
The metadata-only S24 probes yielded unavailable or ambiguous results. The
owner still observed a keyboard that was not numeric-only. Samsung's actual
pairing variant and editor input class remain unknown; no keypad fix is claimed.

Numeric Comparison needs real confirmation/rejection on both devices and is
not a constant-only change. Android setPin and setPairingConfirmation serve
different pairing variants; neither establishes a general app-side replacement
for this device's passkey dialog. Keypad-specific work is deferred. No additional
reset, static PIN, intercepted PIN, or weakened security is part of this change.

References: [Bluetooth Security Manager specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-61/out/en/host/security-manager-specification.html),
[Android BluetoothDevice API](https://developer.android.com/reference/android/bluetooth/BluetoothDevice),
[AOSP pairing controller](https://android.googlesource.com/platform/packages/apps/Settings/+/master/src/com/android/settings/bluetooth/BluetoothPairingController.java).

## Pairing-flow correction

Candidate cards now place the matched label and Authorize this phone action
before detailed instructions. Expandable pairing help follows the candidates.
The Bluetooth setup screen no longer exposes Disconnect and change mode;
normal Bluetooth disconnect and scan cancellation remain available.

Successful reset verification now atomically replaces its exact pending receipt
with a non-identifying fresh-setup marker. On later startup, pending reset
verification still takes priority; otherwise that marker opens fresh setup
instead of returning-owner discovery. A new authenticated Ready clears the
marker. Ordinary saved-owner recovery remains unchanged. A failed persistence
operation cannot claim completed reset cleanup. A failed marker-clear commit
keeps fresh setup required in memory.

The update cannot reconstruct wipes verified by an older app that did not save
this marker. It also does not delete Android bonds. The existing fresh-device
scan remains available for those cases.

## Validation and artifact

The focused runtime, receipt-storage and adapter/policy suite passes 71 tests.
The final affected matrix passes 1,104 tests across 120 reports with zero
failures, errors or skips. All debug/release/V1-Test lint and assembly tasks,
debug Android-test APK assembly, and the unsigned-release artifact audit pass.
All 143 tracked Android input hashes remained unchanged during the matrix.

The matrix used Microsoft JDK 17.0.20 and the repository Gradle wrapper with
`:protocol:test`, all three app unit-test and lint variants, and
`:app:assembleDebug`, `:app:assembleDebugAndroidTest`, `:app:assembleRelease`,
`:app:assembleV1Test`, followed by `Test-AndroidUnsignedReleaseArtifact.ps1`.

V1-Test APK: 12,505,942 bytes, SHA-256
`5FD10EF063FA3962C73D5431B8ECB8221EE076722D2596A9503F4F0A135834B4`.
Package `io.github.nbjelanovic.otclient.v1test`, version 1/1.0.0-v1test,
minimum/target SDK 26/35, verified v2 signature and unchanged signer.
The exact previous C577 installed bytes and recovery image were verified before
S24-only replacement; installed new bytes were independently read back and
matched. Base package, app data, phone settings and original pair were preserved.

The updated S24 app reached authenticated Ready without a new PIN in 31.015
seconds (session 14, discovery 62,427 ms to Ready 93,442 ms). The connected
header and protected readback both showed Trail Bench 2; automatic clock
synchronization was acknowledged, and a fresh region read confirmed US915.
Neither Heltec was restarted or flashed during this app-update acceptance.

Unsigned production APK: 8,722,116 bytes, SHA-256
`CC3016839A51B399DE2402198505D0F6A15AA1B9A35D4025D55116F6C8FAC88E`.
This is an audited unsigned artifact, not a signed production release.

Fresh post-reset behavior and candidate visibility at normal/large font sizes
remain physical acceptance gates. No further device wipe was performed for
this update. No V1 score change, website update or cold-power test is claimed.
