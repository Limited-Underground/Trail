# Product scope and demonstrated boundary

[Android reading path](../README.md) · [Existing V1 criteria](V1_ACCEPTANCE.md)

## Accepted V1 intent

Limited Underground Trail Android App is the phone companion in an offline,
direct two-phone/two-Heltec system: one authorized phone per device. Android
renders authoritative state and sends bounded user intent; the device owns
radio membership, secrets, queues and delivery facts. Internet, a Server,
Repeater, Gateway, Display, Tracker, Console and built-in maps are not required
for this V1 path. An iPhone client is outside current V1; it is a future
possibility, not a prerequisite or implemented Android capability.

The controlling requirements are [Decision 0033](../../docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md),
the [V1/V1.5 acceptance scope](../../docs/testing/V1_V1_5_ACCEPTANCE_SCOPE_V0.md),
[Decision 0103](../../docs/decisions/0103-adopt-ot168-v1-factory-reset-and-boot-pairing.md)
and [V1 user experience](../../docs/product/V1_USER_EXPERIENCE.md).
0103 supersedes the older pairing/replacement clauses; it does not discard the
other V1 constraints. No lost-phone transfer/replacement route is implied.

## Keep implementation and evidence separate

| Boundary | Current fact | Limit |
| --- | --- | --- |
| Checked-in foundation | Compose screens, connected-device service ownership, Kotlin companion codecs, BLE facade/runtime, setup/settings, consent/group/support models exist. | A screen, model or codec is not a completed protected product flow. |
| Bounded physical configuration | Named historical records demonstrate pairing/reconnect and protected name/time workflows on exact phone/device sets. | They are not universal phone support, cold-power, reset or operational release acceptance. |
| Qualified diagnostic APK | A fixed status and reply were displayed on the destination phones on November 2; diagnostic ACKs matched. | RF was unauthenticated. The qualified private source differs from this public tree. |
| Reconstruction | Qualified unsigned source build, 43 tests and identical non-signing APK payload. | No signed-container equality, new device acceptance or runtime/data restore. |
| Protected product messaging | Existing requirements and partial components remain. | Final selected secure composition, truthful production delivery and coherent candidate acceptance remain open. |

Use [quality evidence](QUALITY.md) and the [current handoff](../../docs/PROJECT_STATUS.md#current-recovery-handoff)
for provenance. The documented diagnostic receipt is not a generic production
“Delivered” or human-read receipt.

## Product semantics retained

Queued, Sent, Delivered, expired/failed and human reading are different facts.
GNSS loss must not prevent messaging or create a fabricated current location.
Direct-chat location starts OFF; group sharing follows the accepted informed
Required/Optional policy. Persistent block and deliberate support export retain
their agreed criteria. Local templates are local content, not delivery evidence.
[Retention reconciliation](../../docs/testing/OT-0300-RETENTION-RECONCILIATION-2026-09-23.md)
resolves older transient-only promises against later accepted requirements.

Trail is a supplemental communication aid, not guaranteed rescue. No field range,
endurance, supported-device expansion, store distribution or release promise is
created by these docs. Product development remains frozen.
