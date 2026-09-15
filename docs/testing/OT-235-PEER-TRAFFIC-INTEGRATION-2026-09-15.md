# OT-235 Durable peer activation and authenticated status integration

## Scope and contract

This target-neutral host candidate connects the independently confirmed OT-234
endpoint to real encrypted records. It reuses the existing Noise session,
directional keys, durable counters and authenticated replay store. It does not
select production cryptography or connect the phone Messages screens.

The trusted composition supplies an authenticated signer, exact peer pin, local
Ready/time authority, entropy and five isolated backing stores: boot, invitation
role, TX, RX and activation. Distinct wrapper addresses do not prove distinct
backing namespaces. One serialized owner initializes the crypto library and
owns every operation; reentry protection is not a thread lock. Peer names,
caller Booleans and transport receipt cannot provide trust.

`IndependentPeerTrafficEndpoint` performs this sequence:

1. Generate independent local identity/boot material and validate the signed v2
   invitation binding both identities, boots and local windows.
2. Complete the three-message handshake and require each exact owned local
   confirmation offer. Neither copied offers nor one local decision authorize
   peer traffic.
3. Exchange an encrypted local-confirmation control. Only its authenticated
   receipt permits the local activation ledger commit.
4. Commit and read back an activation digest binding the signed invitation,
   transcript and role; export an encrypted activation control only afterward.
5. Require both local activation export and authenticated peer activation before
   sending or releasing a fixed status identifier from 1 through 8.

Export is not delivery. Local readiness proves receipt of the peer's durable
activation, not simultaneous peer liveness or receipt of our final control.
Control loss needs a new attempt; this candidate adds no retries or ACKs.
Controls and statuses have distinct authenticated eight-byte encodings within
the existing evaluation record. These are host values, not selected Packet V1
bytes, a production KDF-purpose selection or a radio transport integration.

## Durable authority, lifetime and cleanup

`PeerActivationStore` accepts only two erased 64-byte slots. It writes the
activation body, sync, commit marker, sync and exact two-slot readback. Retirement
writes a tombstone in the second slot without erasing the original receipt.
CRC detects corruption; live ownership and exact readback establish authority.
Retained, partial, changed or retired records never authorize reconstruction.

The private endpoint record bridge stages all outputs and rechecks entropy,
durable boot/role authority, Ready context and the signed local deadline before
and after crypto/storage operations. The outer composition also rechecks the
activation receipt before releasing output. Invalid tags/metadata and replay
are bounded rejections; permanent authority/storage/clock faults close the owner.
Caller outputs remain unchanged on failure, including late callback failures.

The original invitation deadline also bounds this candidate's active traffic:
`now >= deadline` closes it, including through idle polling. It grants no new
long-lived membership lease. Close handles cancellation, revocation, disconnect
and reset preparation by attempting both durable retirement and secret cleanup;
uncertainty is reported even when secrets were wiped. It neither erases user
data nor implements the product factory-reset coordinator. Every session is
fresh-only; TX/RX and activation records cannot resume retained keys.

## Validation and target preflight

Focused development passed 66 activation-ledger groups, 27 two-peer traffic
groups and 32 adversarial record groups, using actual cryptography and injected
storage/entropy/clock boundaries. Tests cover both confirmation orders, traffic
before activation, cross-invitation isolation, all status values, malformed and
replayed records, torn writes, readback failures, late expiry, authority loss,
reentry, input mutation, silent expiry and failed retirement.

The final fresh affected matrix and regression build results are recorded in the
[sanitized proof](../../tests/benchmarks/crypto/OT-235-PEER-TRAFFIC-HOST-2026-09-15.json).
The maintained matrix builds fresh scalar dependencies and checks its source
closure; focused runs reuse unchanged objects only during development.

Firmware-porting preflight: board/region/partition/USB/display/radio/boot settings
are unchanged. The new peer composition is not included by any deployable
target. Existing USB/radio targets include the maintained handshake header, so
their builds and actual-startup regressions remain required. Two fresh radio
builds are compared without replacing accepted physical artifact hashes.
Hardware enumeration, flashing, reset, RF, physical entropy/interruption and
new stack measurement are skipped because this increment has no physical scope.

## Remaining product gate

Trusted signer/peer provisioning is still injected. Production work requires
the remaining Phase 3 admission and explicit suite/handshake/KDF/wire selection,
a target-owned trust issuer and retained membership/rekey/reset lifecycle,
protected BLE commands/events, radio framing and delivery acknowledgements.
Then a new exact proposal can exercise the complete two-phone/two-device path.
This candidate closes the bounded host activation/traffic composition gap;
it does not close those product gates or repeat the OT-234 hardware trial.

No V1 completion credit or public website status changed. Implementation
publication requires the separately authorized topic-branch/PR workflow.
