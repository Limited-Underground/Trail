# Android architecture

[Android reading path](../README.md) · [Shared architecture](../../docs/architecture/README.md)

This source map follows the twelve [arc42 sections](https://arc42.org/overview/).
It documents existing code and accepted constraints, not a new architecture or
production acceptance. ISO/IEC/IEEE 42010 is a review reference, not a conformance claim.

## 1. Introduction and Goals

The participant needs truthful connection, message, privacy and location state.
The maintainer needs source/artifact identity and recoverable, bounded failures.
[Product scope](PRODUCT_SCOPE.md) and [existing criteria](V1_ACCEPTANCE.md) retain
the accepted requirements. The direct V1 path has exactly two phone/device pairs.

## 2. Architecture Constraints

Android renders device authority; bond state, a UI Boolean, a name or region
cannot create radio membership or delivery authority. The code compiles for
SDK 35/JVM 17 with minSdk 26; the operational pilot's API31 boundary and exact
supported-phone qualification are separate. Current work cannot change behavior,
security selections or hardware. See [security](SECURITY.md) and [build pins](SOURCE_BUILD.md).

## 3. System Scope and Context

Use C4 `TrailContext` and `TrailContainers` in the [shared model](../../docs/architecture/workspace.dsl).
They represent deployable types, not a new service per phone or a required cloud.
The phone uses standard Android Bluetooth APIs and custom companion GATT records
to its one device. Optional sharing hands reviewed content to Android's external
document/share UI. The diagnostic bench laptop was an experiment dependency,
not part of the intended field topology.

## 4. Solution Strategy

Separate Compose/activity binding, the service-owned controller graph, the BLE
state machine, the Android GATT adapter/serialized native lane, and pure Kotlin
records. Preserve device-issued state and exact generation/operation ownership.
Local fake/test transport and the qualified private diagnostic profile remain
distinct from a selected production transport.

## 5. Building Block View

The source-backed C4 `AndroidComponents` view is in [android-components.dsl](android-components.dsl).
It refines the checked-in application container only; it does not represent the
private overlay as merged code. [C4 component views](https://c4model.com/diagrams/component)
describe responsibilities inside one container; Kotlin modules are not extra deployed services.

| Building block | Source | Responsibility / limit |
| --- | --- | --- |
| UI/activity binding | [MainActivity](../app/src/main/kotlin/io/github/nbjelanovic/otclient/MainActivity.kt), [V1HomeScreen](../app/src/main/kotlin/io/github/nbjelanovic/otclient/V1HomeScreen.kt), [TrailActivityController](../app/src/main/kotlin/io/github/nbjelanovic/otclient/TrailActivityController.kt) | User intent, Compose presentation and lifecycle binding; no radio authority. |
| Service/session owner | [TrailConnectedDeviceService](../app/src/main/kotlin/io/github/nbjelanovic/otclient/TrailConnectedDeviceService.kt), [ConnectedDeviceSessionOwner](../app/src/main/kotlin/io/github/nbjelanovic/otclient/ConnectedDeviceSessionOwner.kt) | Explicit user-started connected-device foreground service; owns one controller/runtime/facade graph and exact-once teardown. |
| Controller/presentation | [TrailAppController](../app/src/main/kotlin/io/github/nbjelanovic/otclient/TrailAppController.kt), [ConnectedDevicePresentation](../app/src/main/kotlin/io/github/nbjelanovic/otclient/ConnectedDevicePresentation.kt) | Maps current runtime/device results to UI and delegates intent. The service's local production transport is explicitly unavailable. |
| BLE state machine | [BleCompanionRuntime](../app/src/main/kotlin/io/github/nbjelanovic/otclient/BleCompanionRuntime.kt) | Discovery, profile/authorization/Snapshot gates, bounded operations, generation checks and close. |
| Platform adapter/native lane | [AndroidBluetoothGattFacade](../app/src/main/kotlin/io/github/nbjelanovic/otclient/AndroidBluetoothGattFacade.kt), [AndroidGattCommandQueue](../app/src/main/kotlin/io/github/nbjelanovic/otclient/AndroidGattCommandQueue.kt) | Android callbacks and exact GATT ownership; native operation completion is distinct from UI dispatch. |
| Companion records | [protocol module](../protocol/src/main/kotlin/io/github/nbjelanovic/otprotocol/) | Pure Kotlin codec/configuration/authorization/semantic records; no independent RF or platform ownership. |
| Local models/adapters | [V1SetupDraft](../app/src/main/kotlin/io/github/nbjelanovic/otclient/V1SetupDraft.kt), [V1SupportScreen](../app/src/main/kotlin/io/github/nbjelanovic/otclient/V1SupportScreen.kt), [V1SynchronizedInbox](../app/src/main/kotlin/io/github/nbjelanovic/otclient/V1SynchronizedInbox.kt) | Pending choices, local templates, reviewed export and typed inbox models. [AndroidV1InboxStorage](../app/src/main/kotlin/io/github/nbjelanovic/otclient/AndroidV1InboxStorage.kt) is explicitly unwired and unencrypted. |

## 6. Runtime View

1. Visible user intent starts/binds the connected-device service. `onStartCommand`
   rejects unrelated starts and failed permissions/foreground promotion; it is `START_NOT_STICKY`.
2. The service constructs the facade/runtime/controller and attaches one generation-bound owner.
3. The runtime selects setup versus saved-owner discovery. The facade performs
   Android discovery/read/MTU/descriptor operations and returns owned callbacks.
4. The admitted profile controls exact authorization/subscription order. Mandatory
   Snapshot/current-state gates precede Ready; a bond alone cannot bypass them.
5. UI intent passes back through the live service/controller/runtime. Typed results
   and stream updates return to presentation. [Interface mapping](PROTOCOL_INTERFACES.md)
   points to the codecs; it is not an alternative sequencer.
6. Disconnect/close invalidates ownership and closes the exact graph. An activity
   binding and service ownership are separate lifetimes; process death can prevent a final log.

The qualified diagnostic runtime additionally required fresh common session/roles,
diagnostic CCCD and Send readiness before its bounded four-frame exchange. Its
successful run is [evidence](QUALITY.md), not the checked-in production sequence.

## 7. Deployment View

`:app` and `:protocol` package into one APK. Android OS supplies BLE, permission,
foreground-service, storage and share facilities. The peer firmware is a separate
embedded runtime with device-owned durable state. There is no required Server,
repeater, internet service or maintenance laptop in direct V1.

[Source/build](SOURCE_BUILD.md) binds variants, dependencies and exact diagnostic
artifacts. Private qualified base/overlays and this checkout must not be conflated.

## 8. Crosscutting Concepts

Generation/operation ownership, serialized native callbacks, versioned defensive
codecs, bounded deadlines, truthful unknown/failure states and explicit user intent
cross all layers. BLE identity/ownership and LoRa peer security are independent.
[Retention](SECURITY.md#privacy-and-storage) and [recovery](OPERATIONS_RECOVERY.md)
separate app data, bonds, device state, external copies and protected signing inputs.

## 9. Architectural Decisions

Retain [0009 device authority](../../docs/decisions/0009-one-phone-companion-authority.md),
[0033 V1 boundary](../../docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md),
[0103 pairing/reset](../../docs/decisions/0103-adopt-ot168-v1-factory-reset-and-boot-pairing.md),
[0104 UX](../../docs/decisions/0104-freeze-ot169-v1-user-experience-profile.md)
and the configuration/time decisions selected by the [protocol index](../../docs/protocol/README.md).
No newer candidate or timestamp silently supersedes them.

## 10. Quality Requirements

Stale callbacks cannot publish Ready or commit current settings; wrong peers and
unauthorized content cannot become delivery; interrupted operations preserve
uncertainty; location loss degrades independently. [Quality evidence](QUALITY.md)
and [V1 criteria](V1_ACCEPTANCE.md) identify implemented, host-tested, physical
and still-open boundaries without creating new thresholds.

## 11. Risks and Technical Debt

Private diagnostic source differs from public source. Complete protected transport,
durable encrypted app retention, group/contact/location binding, exact supported-phone
lifecycle/accessibility and signed operational release remain open at their existing
tasks. Hidden cache-refresh reflection is diagnostic and platform-dependent.
Reconstruction does not prove whole signed APK equality or runtime/data restoration.

## 12. Glossary

| Term | Meaning |
| --- | --- |
| Ready | Current admitted BLE state; not LoRa membership or delivery. |
| Native lane | The adapter's in-flight Android GATT operation ownership. |
| Snapshot | Device-provided current state required by the admitted profile. |
| Diagnostic ACK | Correlation in the unauthenticated fixed-status experiment. |
| Delivered | Existing product requirement: authenticated recipient ACK, not human reading. |
| Qualified source | Exact preserved base/overlays/recipe at its evidence boundary; not automatically current public main. |
