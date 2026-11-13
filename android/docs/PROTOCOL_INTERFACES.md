# Protocol and interface map

[Android reading path](../README.md) · [Shared contract selection](../../docs/protocol/README.md)

Do not use this map as a new wire specification. Original contracts, their version
fields and explicit supersession retain authority. A protocol or security
selection change needs its own reviewed decision.

| Boundary | Actual implementation / contract | Required distinction |
| --- | --- | --- |
| Compose ↔ service | [ConnectedDeviceServiceContract](../app/src/main/kotlin/io/github/nbjelanovic/otclient/ConnectedDeviceServiceContract.kt), [session owner](../app/src/main/kotlin/io/github/nbjelanovic/otclient/ConnectedDeviceSessionOwner.kt) | Binder dispatch or UI enablement is not native completion or delivery. |
| Controller ↔ runtime | [TrailAppController](../app/src/main/kotlin/io/github/nbjelanovic/otclient/TrailAppController.kt), [BleCompanionRuntime](../app/src/main/kotlin/io/github/nbjelanovic/otclient/BleCompanionRuntime.kt) | Current generation/profile/authorization gates remain authoritative. |
| Runtime ↔ Android stack | [facade](../app/src/main/kotlin/io/github/nbjelanovic/otclient/AndroidBluetoothGattFacade.kt), [command queue](../app/src/main/kotlin/io/github/nbjelanovic/otclient/AndroidGattCommandQueue.kt) | Service discovery, characteristic operations and CCCD writes complete through owned callbacks. |
| Android ↔ firmware | [BLE Companion GATT](../../docs/platform/BLE_COMPANION_GATT_V0.md), [configuration profile](../../docs/platform/COMPANION_CONFIGURATION_PROFILE_V02.md), [public profile](../../docs/platform/COMPANION_PUBLIC_PROFILE_V1.md) | Standard BLE/ATT/GATT transports custom versioned Trail UUIDs/records; supported profiles are not interchangeable. |
| Typed application records | [CompanionProtocol](../protocol/src/main/kotlin/io/github/nbjelanovic/otprotocol/CompanionProtocol.kt), [semantics](../protocol/src/main/kotlin/io/github/nbjelanovic/otprotocol/CompanionSemantics.kt), [authorization](../protocol/src/main/kotlin/io/github/nbjelanovic/otprotocol/CompanionAuthorizationWire.kt) | Preserve existing `OTB0`, `OTC0` and related technical identifiers; prose naming never rewrites them. |
| Radio peer transport | [Shared security/protocol selection](../../docs/protocol/README.md) | Android's BLE link does not supply LoRa keys, peer membership, replay state or authenticated ACKs. |
| Support export | [support screen](../app/src/main/kotlin/io/github/nbjelanovic/otclient/V1SupportScreen.kt), [manifest/FileProvider](../app/src/main/AndroidManifest.xml) | User-reviewed Save/Share is not automatic transmission or unrestricted file access. |

## Setup, saved owner and current state

Setup discovery and saved-owner reconnection are separate paths. The accepted
pairing/reset decision uses D1 for a verified unowned pairing window and D0 for
the returning-owner route among bonded candidates. Saved-owner reconnect does
not initiate fresh bonding or infer Ready solely from a bond. Runtime code owns
the exact profile-specific read/security/MTU/authorization/subscription/Snapshot
sequence. Name/clock synchronization uses that existing authority; region
readback does not authorize RF.

[Android's BLE guidance](https://developer.android.com/develop/connectivity/bluetooth/ble/transfer-ble-data)
separates service discovery and characteristic/descriptor callbacks. The source
adapter adds generation, exact-GATT and in-flight-operation checks. Stream CCCD
completion and a current Snapshot are not replaced by historical Ready evidence.

## Diagnostic profile is a separate evidence boundary

The qualified private V1-Test source includes fixed-status diagnostic framing,
characteristic prefix `5e0f2a05` and standard CCCD `2902`, role/session checks and
subscription-aware Send readiness. It is not present as that accepted composition
in this checked-in foundation. Consult its exact preserved profile/firmware/source
descriptor rather than inventing fields from this summary.

The November 2 experiment used one session, a 60-second RF bound, 30-second DATA
permits, four total transmissions including ACKs and no retry. Those are retained
experiment limits, not new product retry policy or permission to rerun it. Actual
destination display plus matching diagnostic ACK was recorded in each direction.
Unauthenticated diagnostic ACK is not production authenticated delivery or reading.
