# Security and privacy boundaries

[Android reading path](../README.md) · [Shared security selection](../../docs/protocol/README.md)

## Ownership and authority

Standard Bluetooth security/bonding and Trail's application authorization are
distinct. A current protected read, admitted profile, generation and device state
must pass; Android's bond database is not a cached authorization token. See
[Decision 0103](../../docs/decisions/0103-adopt-ot168-v1-factory-reset-and-boot-pairing.md)
and the [GATT contract](../../docs/platform/BLE_COMPANION_GATT_V0.md). The public
facade documents the limits of Android-side security evidence; a protected
characteristic's target enforcement cannot be replaced by a phone-side Boolean.

Radio membership, private/conversation/nonce secrets, durable counters/replay
state and authenticated peer ACKs belong to the device/security boundary.
[OTSL0 requirements](../../docs/security/SECURE_LORA_KEY_TRANSPORT_V0.md) and the
[threat model](../../docs/security/THREAT_MODEL_V0.md) retain their scope.
Production selection/admission remains the existing shared OT-0237e dependency;
diagnostic success and a conditional host profile do not select or activate it.

The current V1 physical-access limitation remains: firmware writing/old-flash
restoration can reset or roll back ownership. No new rollback-proof physical
attacker guarantee is introduced.

## Privacy and storage

[OT-0300 reconciliation](../../docs/testing/OT-0300-RETENTION-RECONCILIATION-2026-09-23.md)
resolves the historical transient-only release promise against later accepted
ownership, persistent block/group, consent and export rules. Preserve that policy;
do not infer a permission to retain/export data from a model's existence.

- Pending setup and local templates are phone-local choices, not verified device commits.
- [AndroidV1InboxStorage](../app/src/main/kotlin/io/github/nbjelanovic/otclient/AndroidV1InboxStorage.kt)
  explicitly says its AtomicFile bytes are unencrypted and the adapter is unwired;
  production controllers must not instantiate it for real message content.
- [The main manifest](../app/src/main/AndroidManifest.xml) disables backup and
  scopes a non-exported FileProvider. This is source configuration, not complete
  installed-candidate privacy or removal evidence.
- Support preview/Save/Share and user notes require their existing deliberate
  controls. App cache, external saved/shared copies, OS bonds and device data have
  different cleanup boundaries; no automatic email or forensic erasure is claimed.

## Diagnostic and release separation

The accepted V1-Test APK and recorder are not production admission. Diagnostic RF
was unauthenticated. One explicitly consented cache refresh was qualified on each
recorded phone using private diagnostic-only source; it was not a bond/data reset.
Reflection on hidden `BluetoothGatt.refresh()` is not a supported public API
contract. Android documents [non-SDK restrictions and compatibility limits](https://developer.android.com/guide/app-compatibility/restrictions-non-sdk-interfaces).
Do not turn that experiment into automatic production recovery or a current command.

Signing keys, passwords, raw private captures, grants and recovery payloads stay
outside source/handbook publication. Unsigned payload equality does not establish
the signer or permission to upgrade a phone. Follow [security reporting](../../SECURITY.md)
for sensitive findings and [existing operational criteria](V1_ACCEPTANCE.md) for
candidate-bound privacy, signing, lifecycle and release acceptance.
