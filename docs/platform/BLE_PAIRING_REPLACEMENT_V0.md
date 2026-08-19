# BLE Pairing and Replacement Contract v0

Status: `CONTRACT-FROZEN-HOST-ONLY`

Schema: `OTBP0/v0`

Plan ID: `OT-090-BLE-PAIRING-REPLACEMENT-CONTRACT-V0`

Work item: OT-090

Authority: [Decision 0034](../decisions/0034-host-tested-ble-pairing-replacement-contract.md)
under the permanent [Decision 0033](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md)
V1 boundary.

## Purpose and nonclaim

This contract freezes the practical physical-presence pairing, reconnect, and
phone-replacement semantics that must precede target and Android implementation.
It is deterministic host evidence, not a Bluetooth, phone, firmware, storage,
display, input, or physical acceptance result.

The machine-checkable authority is
[`OT-090-BLE-PAIRING-REPLACEMENT-CONTRACT-V0.json`](../../tests/release-plans/OT-090-BLE-PAIRING-REPLACEMENT-CONTRACT-V0.json).
If prose and that artifact disagree, validation must fail; neither side may be
silently relaxed during implementation.

## Required states and admission

- Pairing and replacement admission is closed after boot and after every
  terminal path.
- A new-pair window is eligible only when durable ownership is coherently
  unowned. A replacement window is eligible only when one coherent current
  controller exists.
- The exact target-neutral physical action is holding the designated local input
  for at least 3000 ms and then releasing it. That release opens one exact
  30-second window for the requested purpose. OT-090 does not select a GPIO,
  button, polarity, pull configuration, debounce circuit, or electrical mapping.
  Remote traffic, an Android request, advertising, connection, retry, restart,
  or an existing bond cannot open or extend the window.
- At most one window, one candidate, and one pairing attempt exist at a time.
  Other than the exact replacement-confirmation gesture described below, a
  second opening action, candidate, or overlapping attempt is denied without
  replacing the active attempt.
- The device generates one fresh uniform six-decimal-digit passkey from its
  approved secure-random source for every admitted window and displays it only
  on the local Heltec display. It is never accepted from the phone or reused as
  an owner/controller identifier. Every window requires a fresh sample; the
  contract does not guarantee that two independently sampled numeric values
  differ.
- Only Bluetooth LE Secure Connections with MITM-authenticated passkey pairing,
  bonding, and an exact 16-byte/128-bit encryption key is eligible. Legacy pairing, `Just
  Works`, unauthenticated security, a static/debug passkey, missing MITM
  evidence, a stale passkey, or a passkey from another window is denied.

## Pairing, reconnect, and replacement

On an unowned device, a successful authenticated bond becomes only the
candidate for the exact new-pair window. Protected application authority may be
published only after the device's one-controller owner state is durably and
coherently committed and reread.

The phone enters the displayed passkey only through the Android system pairing
UI. The OpenTrail app never receives, stores, displays, or logs that passkey.
The OS bond callback must be bound to the exact active candidate and transport
generation; OS bond state alone is never device-owned application
authorization. App cancellation, Bluetooth permission loss, foreground-service
stop, or disconnect closes the attempt without automatic retry.

An owned device stays closed to new pairing. Ordinary reconnect requires the
saved current private bond binding, a fresh boot-local session, an exact recheck
of Secure Connections, MITM authentication, bonding, and the exact
16-byte/128-bit key, plus the separate device-owned application authorization.
It does not display or request a new passkey and does not rewrite ownership.

Replacement is a separate flow. Opening its window releases the live controller
lease but retains the durable prior owner. The first 3000-ms hold/release opens
the exact 30-second replacement window. The same private bond is not a valid
replacement candidate. After the candidate completes the required secure bond,
explicit local confirmation requires a second hold of at least 3000 ms and
release before the original deadline. Confirmation never extends or restarts
the window. The candidate has no protected authority while staged. Exact
candidate-owner commit and readback precede invalidation of the prior
application authorization. Old-bond removal and exact absence verification
then precede publication of the new controller. Abort, expiry, interruption, or
known pre-mutation failure preserves the exact prior owner only after the
candidate bond is removed and its absence is verified. If commit, readback,
candidate cleanup, or old-bond cleanup is uncertain, neither old nor new
controller may be published and the device enters a reconciliation-required
closed state.

## Timeout, restart, and failure closure

The 30-second deadline starts on release of the first qualifying hold. No event
extends or restarts it. A replacement confirmation hold must begin and its
release must occur before the original deadline. The checked monotonic clock
must be available and nondecreasing; missing time, rollback, arithmetic
overflow, or an event after the exact deadline closes the attempt.

Passkey mismatch, authentication or bond failure, cancellation, unexpected
disconnect, stale or replayed event, wrong purpose, second candidate, malformed
state, and storage absence, corruption, conflict, unreadability, or uncertainty
all deny authority. App cancellation, permission loss, service stop, and
disconnect never auto-retry. One pairing attempt consumes its window. The
passkey is cleared when its secure bond completes and on failure, expiry,
disconnect, restart, or fault. Every terminal path clears pending candidate
state.

Restart closes every transient pairing/replacement window. A passkey or pending
candidate is never restored. Coherent persisted unowned state returns closed and
unowned; coherent current-owner state returns closed and permits only the saved
current-bond reconnect path. Ambiguous persisted state returns closed and
reconciliation-required with no controller published.

## Reset and physical-access boundary

Ordinary connection loss releases only the live controller lease. It does not
erase durable ownership, change LoRa/group security, discard queues, or open a
pairing window.

Factory reset, reflashing, invasive physical access, or restoration of an old
flash image may reset or roll back ownership. V1 does not claim resistance to
that physical firmware-writing attacker. A secure element and independent
monotonic authorization floor remain optional future hardening rather than V1
requirements.

The ordinary application-protected owner schema must remain explicitly distinct
from the historical floor-based `OAP0/v0` records. It stores only an opaque
private bond reference as the owner binding; a BLE address, phone name, peer-
supplied value, or raw key is never owner identity.

## Privacy and diagnostics

Public and ordinary diagnostic output may report only fixed coarse states and
reasons. It must never include a passkey, bond key, phone identifier, BLE
address, device-specific identifier, private owner token, private bond
reference, boot/session challenge, physical-event token, authorization
correlation, storage content, or raw Bluetooth trace. A rejected secret-bearing
record is not made publishable merely by redacting it afterward.

## Implementation and physical acceptance still required

Later separately authorized work must bind the frozen contract to the exact
Heltec physical control, OLED, secure random source, checked clock,
ESP-IDF/NimBLE security and bond-store APIs, ordinary application-protected
owner storage, Android pairing/replacement UX, protected GATT authority, and
cleanup/recovery behavior. The coherent physical V1 run must still prove two
distinct fresh PIN flows, expiry, restart reconnect, cross-pair denial, one
complete replacement, and old-authorization rejection.

Secure LoRa key provisioning and authenticated/encrypted packet transport remain
a separate contract and evidence gate.
