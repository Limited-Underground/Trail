# Device Factory Reset Contract v1

Status: `OWNER-APPROVED-DESIGN; PARTIAL-HOST-IMPLEMENTATION-EVIDENCE; PHYSICAL-ACCEPTANCE-PENDING`

Contract version: `1`

Work item: `OT-168`

Authority: [Decision 0103](../decisions/0103-adopt-ot168-v1-factory-reset-and-boot-pairing.md)

This document is the current product authority for resetting a Trail device
when its authorized phone is available and for physically resetting it when
that phone is lost or unavailable. It specifies observable behavior and the
logical data boundary. Final focused firmware tests, a pinned ESP-IDF target
build, and the post-audit Android foundation gate provide host implementation
evidence. The complete Host matrix, power interruption, both physical Heltecs,
and coherent two-phone operation have not yet passed this contract.

## Required outcomes

A completed factory reset makes the device logically equivalent to a new,
unowned Trail device while retaining only the non-user device fundamentals
listed below. A completed reset must:

1. remove the current phone owner and every application-authorization record;
2. remove every BLE bond and bond-derived private reference;
3. make every prior-user Trail record inaccessible to normal firmware, a newly
   paired phone, radio peers, maintenance UI, and ordinary diagnostic export;
4. restart the device in a verified unowned state; and
5. only after reset cleanup is verified, generate a fresh six-decimal-digit PIN,
   display it locally, and advertise the pairable device for exactly 60 seconds.

The fresh PIN is never restored or reused. If the 60-second window expires, the
device conceals the PIN and stops accepting a new phone. A later unowned boot
opens a new 60-second window with a newly generated PIN. An owned device never
opens this window during an ordinary restart and never shows a PIN to replace
its saved phone.

## Enrollment and returning-owner discovery

V1 keeps first-time enrollment separate from saved-owner reconnect:

- `5e0f2a00-7c6b-4ea3-a210-0c4f1f43b7d1` (`D1`) is the pairable
  advertising marker. It is present only during the automatic 60-second
  unowned-boot window. Android Add Device scans and revalidates `D1` only.
- `5e0f2a00-7c6b-4ea3-a210-0c4f1f43b7d0` (`D0`) is the protected normal
  companion service and the only advertising identity accepted for a
  returning-owner search. An owned ordinary boot advertises no `D1`, displays
  no PIN, and does not accept a new owner.
- A returning-owner search intersects observed `D0` advertisements with
  Android's current bonded-device authority and proceeds only when exactly one
  candidate remains. `D1`, a mixed `D0`/`D1` advertisement, an unbonded
  candidate, zero matches, or multiple matches fails closed.
- Returning-owner reconnect never calls `createBond`, never requests or shows a
  PIN, and does not persist a BLE address, MAC, or private pairing identifier in
  the app. Android bond state is only a prerequisite: the connection must read
  normal `ProtocolInfo` through the protected `D0` GATT service and pass the
  device's current owner authorization before the app may publish `Ready`.

These rules do not provide an ownership-conflict or lost-phone replacement
route. An unknown phone cannot use `D0` to become the owner, and an owned
device cannot be made pairable without the complete factory reset below.

## Authorized-app reset path

The currently authorized phone may start factory reset only through an
encrypted, authenticated, application-authorized controller session. The app
must show the destructive scope and receive explicit user confirmation before
sending the reset command.

After the device accepts that authenticated command, it completes the reset
without a button press or any other Heltec action. The app retains its local
device record until it observes an exact post-reset restart and verified
unowned-state readback. Disconnect, timeout, malformed readback, or an unknown
outcome must not be displayed as successful reset.

No unknown, merely BLE-bonded, nearby, direct-radio, group, or server client may
invoke this path.

### Privacy-safe reset receipt

The app-reset path uses one short-lived receipt to correlate a destructive
command across the reset and BLE-address change:

1. The app generates a random nonzero unsigned 64-bit value and encodes it
   little-endian in bytes 8 through 15 of the `OTA0/v0` request (the existing
   subject slot).
2. An admitted `OTR0/v0` response echoes those exact eight bytes. `ADMITTED`
   means only that the durable reset intent was accepted. It never means that
   deletion, verification, reboot, or reset completion succeeded.
3. The durable reset marker carries the receipt through cleanup. After every
   required domain and bond is verified absent, the next unowned 60-second D1
   window publishes D1 service data whose exact 13-byte payload is ASCII
   `OTRR`, version byte `0x01`, and the same eight little-endian receipt bytes.
4. The app accepts only an exact receipt match. A different or absent receipt
   cannot complete this reset verification. Unknown write/response outcomes
   keep the pending receipt and scan for the exact post-reset value without
   resubmitting the destructive command.

The current Heltec binding keeps intent, state, and receipt in one versioned
16-byte NVS blob under isolated key `ot_reset_v1/record_v1`: ASCII `OTFR`,
version `0x01`, one state byte, two reserved zero bytes, and the little-endian
u64 receipt. Every mutation is a single-key whole-record transition with
commit and exact readback. App reset moves absent to `intent_committed`, then
after verified cleanup replaces it with `receipt_pending`; physical reset uses
receipt zero and erases the record after cleanup. Boot verifies user and bond
absence before consuming `receipt_pending`. Consumption erases the record,
commits, reads exact absence, and only then publishes the receipt retained in
RAM in the exact `OTRR` scan response. A transient or uncertain consumption
erase waits a bounded one second and reboots into fresh executor
reconciliation rather than opening BLE or normal operation. Power loss can
therefore expose only the old or new whole record. If consumption committed
but power failed before RAM publication, the app truthfully remains unknown,
while the device remains safely unowned and pairable. Unreleased legacy split
intent/receipt key shapes fail closed without mutation.

This receipt is correlation data only. It is not a phone, user, device, or
owner identifier; grants no pairing, GATT, reset, or application authority;
contains no PIN or Bluetooth address; and is never accepted on an owned boot.
The app may persist only the receipt and one coherent issued/expiry window of
at most 120 seconds in app-private storage. Clock rollback, incoherent bounds,
or expiry must clear it fail closed. It may not persist the selected endpoint, MAC,
private pairing identifier, phone identity, or PIN. The physical reset path
uses no nonzero app receipt and advertises no app-reset receipt.

Verified reset does not guarantee that Android removed its system bond. The app
must not use hidden or reflective bond-removal APIs. If the stale bond remains,
the app directs the user to Android Bluetooth settings to forget it after reset
verification.

## Lost-phone physical reset path

The physical path requires local access to the Trail device and uses the
designated Heltec user control:

1. Press and hold the control continuously for at least 10 seconds.
2. At the 10-second threshold, the LCD displays `Erase all Trail data?` while
   no user data has been changed.
3. Release the control. Release after the threshold arms one 10-second
   confirmation window.
4. Short-press and release the control once before or at the confirmation
   deadline to confirm factory reset.
5. The LCD displays an in-progress state while protected operation remains
   blocked.
6. After verified reset completion, the device restarts unowned and enters the
   fresh 60-second PIN and advertising window described above.

Releasing before the initial 10-second threshold never starts reset. Failing to
confirm before the second 10-second deadline cancels the request. An additional
hold, repeated press, button bounce, stale event, clock failure, or event from a
different boot cannot substitute for the required sequence. The target adapter
owns electrical polarity and debounce, but it must emit this exact semantic
hold, release, and short-press sequence using a nondecreasing checked monotonic
clock.

The physical path is destructive recovery, not phone replacement. It never
transfers the previous owner's authority, preserves a candidate new owner, or
reveals prior-user information.

## Authoritative wipe inventory

Factory reset deletes or makes permanently inaccessible every user-associated
Trail record, including:

- the durable phone-owner record, application authorization, BLE bonds, and
  bond-derived references;
- the editable device name, operating region, user preferences, radio policy,
  discoverability choices, direct-contact permissions, and location-sharing
  permissions;
- device and group cryptographic identities, private keys, group secrets,
  membership, roles, aliases, invitations, epochs, revocations, and recovery
  material;
- direct and group contacts, messages, quick statuses, alerts, receipts,
  pending transfers, retry/outbox/queue state, acknowledgements, replay and
  duplicate checkpoints, and user-associated counters;
- saved positions, breadcrumbs, archives, routes, location history, and
  location-bearing diagnostic or audit records;
- user-selected or user-transferred offline map packages, selectors, indexes,
  activation history, and map-recovery metadata; and
- any other persisted value created by, derived from, or capable of identifying
  the prior user, phone, contacts, groups, activity, or configuration.

Old cryptographic identities and keys must become unusable before their related
counters or replay state may be reset. No newly provisioned identity may reuse
an old key/counter domain.

An implementation may retain an immutable factory-supplied public map asset
only when it is byte-identical across devices of the same release and carries
no prior-user selection, usage, location, or activation metadata. Everything
else in a map persistence domain is user data and must be covered by the reset.

## Data retained across factory reset

Only these non-user device fundamentals remain:

- installed application firmware, bootloader, partition layout, and required
  signed-update trust or anti-rollback state;
- immutable hardware identity and hardware-profile information; and
- manufacturer or board calibration that contains no user, phone, group,
  message, location, or usage information.

Firmware version, hardware identity, and calibration must not be repurposed to
retain ownership or user history. Factory reset does not downgrade firmware,
restore an older image, alter eFuses, or erase the bootloader.

## Commit, interruption, and recovery semantics

The implementation must separate preparation from the single durable logical
reset commit.

- Before that commit, the prior owner and complete prior-user state remain the
  only authoritative state. Early release, cancellation, confirmation timeout,
  validation failure, storage failure, or power loss before commit preserves
  them; no partial empty state may be published.
- The commit atomically changes the authoritative device state to reset
  committed and makes the prior user domain inaccessible. It must not publish a
  new owner, new PIN, normal radio operation, or ordinary application access.
- After commit, interrupted physical cleanup is not rollback. Boot enters a
  reset-cleanup-required state, blocks all protected and user-data access, and
  resumes deletion and exact verification.
- Pairing, advertising, normal display state, radio traffic, and app control
  remain blocked until every required user namespace and BLE bond is verified
  absent and the unowned record is verified coherent.
- Only that verified completion permits the unowned restart and fresh
  60-second pairing window.

If the storage design cannot provide one atomic cross-domain cutover, it must
use a durable reset marker or generation/root scheme that provides the same
observable guarantees. A reset may not be reported complete merely because an
erase call returned success.

## Relationship to existing contracts

This v1 contract supersedes only the current-product behavior that allowed an
owned device to open a phone-replacement window. A lost or unavailable phone is
handled by the destructive physical reset above; there is no V1
`replace-lost-phone` ownership-transfer path. Historical OT-090 / `OTBP0/v0`
pairing and replacement evidence remains an immutable record of what those
tests proved, but its replacement behavior is not current implementation
authority.

For current V1 behavior, this contract also supersedes the ordinary-factory-
reset preservation route in
[`OFFLINE_MAP_SELECTOR_RESET_REPLACEMENT_POLICY_V0.md`](../maps/OFFLINE_MAP_SELECTOR_RESET_REPLACEMENT_POLICY_V0.md).
That document remains historical host evidence for its exact policy version,
but user-associated selector records, protected map history, packages, and
metadata may not survive this whole-device factory reset.

The authorized-app and physical paths differ only in authorization and user
interaction. After authorization, both must converge on the same wipe
inventory, commit boundary, cleanup verification, unowned state, and fresh
pairing behavior.

## Required validation

Host and target tests must cover at least:

- release just before and at the 10-second hold threshold;
- warning presentation without mutation;
- confirmation immediately, at the exact deadline, and after the deadline;
- cancellation, button bounce, duplicate events, stale events, restart, checked
  clock failure, and simultaneous ordinary input;
- authorized-app reset without a physical confirmation request;
- nonzero little-endian app receipt generation, exact request/response echo,
  intent-only `ADMITTED`, exact post-cleanup D1 `OTRR`/v1 match, wrong/expired
  receipt rejection, and no receipt on physical reset;
- process/service restart with only receipt plus coherent issued/expiry
  persistence; unknown command outcomes must verify without resubmitting reset;
- power loss and storage failure before commit, at commit uncertainty, and
  after commit during every cleanup stage;
- exact deletion and absence verification for every inventory item and BLE
  bond;
- an owned reboot remaining closed and PIN-free;
- a verified reset reboot opening exactly one fresh 60-second window;
- expiry clearing the PIN and pairable advertising, and a later unowned boot
  using a different fresh sample; and
- rejection of the old phone and every unknown phone until the fresh pairing
  flow succeeds.

Physical acceptance on each supported Heltec target must verify the displayed
warning, timing, cancellation, reset progress, restart, old-owner rejection,
complete prior-user-data inaccessibility, and new 60-second pairing window.

## Explicit non-goals and nonclaims

- This is not a lost-phone ownership transfer or PIN bypass.
- It does not remotely erase information already copied to a lost phone,
  another Trail node, an archive, or another external system. Group peers must
  separately revoke and rekey a lost node when applicable.
- It does not claim forensic secure erasure of raw flash cells, resistance to
  invasive hardware access, rollback-proof ownership under reflashing, or
  protection against restoration of an old flash image.
- It does not authorize firmware installation, downgrade, bootloader changes,
  eFuse changes, or arbitrary maintenance commands.
- It does not make a device supported, production-ready, secure, or field-ready
  without the required host, Android, target, power-interruption, and physical
  evidence.
