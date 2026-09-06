# Decision 0106: Device-confirmed OLED configuration and time authority

- Status: Accepted implementation contract; implementation pending
- Date: 2026-09-06
- Work items: OT-170, OT-172, OT-178

The OLED consumes coherent device-owned facts. Phone drafts, raw BLE connection,
generic action acknowledgement and the clock's authorization boolean cannot
establish durable configuration or current session authority.

Adopt [the configuration/time contract](../platform/OLED_CONFIGURATION_TIME_AUTHORITY_V1.md):
exact session/generation correlation through queued work; revisioned commit and
readback for settings; independent group and radio authority; volatile civil
time admitted through a current challenge younger than 2,000 device-local ms.
Clock synchronization uses the application owner's current monotonic tick,
never Android elapsed time. Reject stale external input before calling the
presentation clock; reset/revocation/expiry and local rollback still invalidate.

Preserve and review the existing unpublished device-name draft instead of
inventing a competing codec. No wire version, frame kind, capability, storage
namespace or schema is allocated here. The current 40/52-byte coordinator is
not implicitly enlarged by the protocol envelope's 128-byte payload capacity.
The existing name draft requires a separately reviewed transport extension.

The next increment is the host-only bounded time-admission owner and adversarial
lifecycle tests. This decision does not change target source, firmware, phone,
radio behavior, physical acceptance or any V1 completion value. Website and
cold-power work remain owner-deferred.

## Host implementation checkpoint

The time-admission owner now passes 21 focused groups and the complete host
matrix. See [bounded evidence](../testing/OT-178-TIME-ADMISSION-2026-09-06.md).
This accepts the host portion only; negotiated transport, durable configuration,
real authority mapping and physical integration remain pending.
