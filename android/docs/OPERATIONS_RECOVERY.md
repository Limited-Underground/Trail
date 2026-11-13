# Operations and recovery boundaries

[Android reading path](../README.md) · [Source/build](SOURCE_BUILD.md)

This is a reading procedure, not permission to operate a phone or board. Each
device session needs the owner's approval first under the [working contract](../../CONTRIBUTING.md#working-contract). Do not reinstall, refresh a cache, clear data or
bonds, reenroll, reset or flash to follow this page.

## Existing normal lifecycle

Use the saved-owner service-start route for an owned device; setup discovery is
for the accepted unowned pairing path. The [service](../app/src/main/kotlin/io/github/nbjelanovic/otclient/TrailConnectedDeviceService.kt)
requires explicit visible-user start, permissions and foreground promotion, and
uses `START_NOT_STICKY`. A stopped or intentionally disconnected app is not a
failed connection. UI binding and service graph ownership are separate.

Fresh readiness is profile-specific: exact target, current owned GATT, compatible
ProtocolInfo, required MTU, authorization/subscriptions and mandatory Snapshot.
Diagnostic CCCD/native-lane/Send prerequisites exist only at the exact qualified
diagnostic source boundary. They never authorize RF by themselves.

The [owner](../app/src/main/kotlin/io/github/nbjelanovic/otclient/ConnectedDeviceSessionOwner.kt)
invalidates and closes its graph once. Android process death can prevent a final
record; closure must be observed when a future scoped trial requires it, not
inferred from a historical log. No repeated attempt is implied after failure.

## Five separate recovery claims

| Claim | Retained result | Limit |
| --- | --- | --- |
| Source/history retrieval | Preserved source/history and exact Android base/overlays were retrieved and checked. | A public clone lacks the private qualified source/evidence. |
| Accepted binary retrieval | Exact APK/firmware bytes were independently hash-verified in preservation/restore. | A filename/version alone is not identity or current installed state. |
| Source rebuild | Qualified unsigned Android build and 43 tests passed using declared inputs. | No new OS bootstrap, dependency refetch or complete hermetic proof. |
| Byte equality | Every non-signing APK entry matched the accepted APK. | Whole signed APK/signer equality not established; firmware raw equality failed. |
| Runtime/data restore | No new phone/device/NVS restoration in this work. | Source/build success cannot restore ownership, app data, bonds or durable device authority. |

Keep originals and hashes; never overwrite accepted binaries with a rebuild.
Private signing continuity backup is separate from authorization to use a key.
Future data-preserving upgrade evaluation must bind exact target, package,
predecessor, candidate bytes and signer before installation, under applicable
owner authority. An unsigned reconstruction is not an upgrade artifact.

## Data domains and existing fallback

App-private drafts/templates, OS bonds, device ownership/NVS, external support
copies and firmware recovery images have different owners and cleanup rules.
The manifest disables backup; there is no accepted automatic full-state restore.
The AtomicFile inbox adapter is unwired/unencrypted, not a production recovery store.

[Decision 0103](../../docs/decisions/0103-adopt-ot168-v1-factory-reset-and-boot-pairing.md)
and [reset contract](../../docs/platform/DEVICE_FACTORY_RESET_V1.md) define exact
reset domains/receipt and physical-presence routes. Historical device ROM-entry,
readback and application restoration evidence belongs to the firmware recovery
owner; [firmware recovery](../../docs/firmware/BUILD_AND_RECOVERY.md) retains those
limits. Do not substitute Android cache refresh, unpairing or data clearing for it.

For a future failure, preserve the first actual operation/callback/refusal and
fresh evidence, contain the unsafe action and resume only authorized host work.
Existing evidence can be reused when its inputs/boundary remain applicable.
No physical retry, secret use or restoration authority arises from diagnosis.
