# Decision 0109: Bind companion name and time configuration

- Date: 2026-09-06
- Status: software validation accepted; final reproducible builds and physical acceptance pending
- Work items: OT-170, OT-178

Use the allocated profile 0.2 for normal protected companion traffic after the
existing ownership/authorization path succeeds. Re-read protected ProtocolInfo
after claim promotion. Keep one shared exchange sequence and one reserved
148-byte response lane across base, name and time operations. GATT callbacks
copy bounded requests; the app task executes the dispatcher and storage; a
NimBLE host event submits the exact response. Callback/lifecycle loss cannot
authorize queued work or publish a stale completion.

Store the current device-name SNAPSHOT as one versioned OTNCv1 blob in isolated
default-NVS namespace/key `ot_name_v1/record_v1`. Preserve its revision and exact
UTF-8 bytes. Keep the owner record, raw configuration store and partition layout
unchanged. An uncertain mutation requires fresh read reconciliation. Factory
reset erases and verifies the entire name namespace, including unsupported or
corrupt shapes, before unowned boot. No flash call runs in the GATT callback.

Clock state remains volatile and owner/session-bound, using the existing device
challenge and monotonic deadlines. Phone civil time and its 12/24-hour preference
are presentation input, not secure time or radio configuration. The region-required
OLED surface retains its two warning rows and may show the verified name and
clock below them. No region, group or radio transmission authority is inferred.

The old reconnect firmware may restore a failed installation before the new
application boots or writes name data. Once new name data exists, that older
firmware is not an accepted whole-user-data reset implementation because it does
not know the added namespace. A later downgrade therefore needs a separately
reviewed compatible recovery path; the initial install's automatic rollback
does not authorize such a downgrade.

See [integration and validation evidence](../testing/OT-170-178-LIVE-INTEGRATION-2026-09-06.md).
Website publication and physical battery-disconnection testing remain deferred.
