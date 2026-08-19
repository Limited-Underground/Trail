# Decision 0034: Host-tested BLE pairing and replacement contract

- Status: Accepted host-only contract and validation
- Date: 2026-08-19
- Work item: OT-090
- Scope: V1 Companion physical-presence BLE pairing, reconnect, and phone replacement

## Context

[Decision 0033](0033-permanent-v1-v1-5-scope-and-security-boundary.md)
adopts practical physical-presence authorization for V1 and requires its exact
PIN, timeout, confirmation, persistence, replacement, restart, and failure
semantics to be frozen and host-tested before target or Android implementation.
The earlier rollback-floor design remains useful historical stronger-design
evidence, but an independent monotonic floor and secure element are not V1
prerequisites under the disclosed physical-firmware-access limitation.

The owner authorized the next host-only contract increment. That authority does
not include a target write, phone installation, Bluetooth operation, PIN
display or entry, bond/key operation, pairing, replacement, radio transmission,
signing, upload, or distribution.

## Decision

OpenTrail accepts `OTBP0/v0`, the exact V1 BLE pairing/replacement state
contract documented in
[`BLE_PAIRING_REPLACEMENT_V0.md`](../platform/BLE_PAIRING_REPLACEMENT_V0.md)
and represented by
[`OT-090-BLE-PAIRING-REPLACEMENT-CONTRACT-V0.json`](../../tests/release-plans/OT-090-BLE-PAIRING-REPLACEMENT-CONTRACT-V0.json).

The contract is normally closed. Holding the designated target-neutral local
input for at least 3000 ms and then releasing it opens exactly one 30-second
new-pair or replacement window; OT-090 selects no GPIO, button, or electrical
mapping. Every window receives a fresh device-generated six-decimal-digit
passkey that is displayed only on the Heltec and is valid only for that window.
V1 requires Bluetooth LE Secure Connections only, MITM-authenticated passkey
pairing, bonding, and an exact 16-byte/128-bit encryption key. Legacy pairing, `Just Works`,
static or debug passkeys, and any weaker or ambiguous security result are denied.

A coherently unowned device may admit one new controller and only one pairing
attempt per window. A coherently owned device permits ordinary reconnect only
for its saved current bond after exact link-security and separate application-
authorization checks; reconnect does not rewrite ownership. Opening a
replacement window releases the live controller lease but retains the durable
prior owner. Replacement may proceed only through the distinct physical
replacement action and rejects the same private bond as its candidate. After
the candidate completes the required secure bond, replacement confirmation
requires a second hold of the same designated local input for at least 3000 ms
followed by release before the original 30-second deadline. It does not extend
or restart that deadline.

A phone enters the passkey only through the Android system pairing UI. The
OpenTrail app never receives, stores, displays, or logs it. The OS bond callback
must be bound to the exact candidate and transport generation, and bond state
alone is never application authorization. App cancellation, Bluetooth
permission loss, foreground-service stop, or disconnect closes the attempt with
no automatic retry.

A replacement candidate receives no protected authority before exact candidate
owner commit and readback. That exact commit invalidates the prior application
authorization; the old bond must then be removed and its absence verified
before the new controller is published. Abort, expiry, interruption, or known
pre-mutation failure preserves the exact prior owner only after the candidate
bond is removed and its absence is verified. Ambiguous commit, readback,
candidate cleanup, or old-bond cleanup publishes neither controller and
requires reconciliation rather than guessing which authorization won.

Timeout, passkey mismatch, authentication or bonding failure, cancellation,
unexpected disconnect, stale event, replay, second candidate, clock failure or
rollback, restart during a window, and incoherent or unreadable persisted state
all fail closed. A pairing attempt consumes its window. A restart never resumes
a transient window or passkey. A saved current bond may reconnect only after
coherent persisted ownership is restored. Transient passkeys are cleared when
the secure bond completes and on failure, expiry, disconnect, restart, or fault;
pending candidate state is cleared on every terminal path. Neither enters
ordinary logs or public evidence.

The Android system bond remains a transport prerequisite, not application
authorization by itself. Device-owned one-controller authorization and
protected GATT access remain separate target/app integration gates. BLE
authorization also does not secure LoRa; the distinct versioned LoRa
key-provisioning and authenticated/encrypted transport contract remains the
next host-contract checkpoint.

## Accepted V1 limitation

Factory reset, reflashing, invasive physical access, or restoration of an old
flash image may reset or roll back V1 ownership. Ordinary application-protected
storage is permitted under that disclosed boundary. OT-090 does not claim
rollback-proof ownership against a physical firmware-writing attacker.

## Authority and evidence boundary

OT-090 accepts deterministic host-contract evidence only. It does not implement
or prove an ESP-IDF/NimBLE state machine, Android workflow, physical gesture,
OLED passkey display, secure connection, bond storage, pairing, reconnect,
phone replacement, protected GATT access, `Ready`, supported hardware, secure
LoRa, signed release, or coherent two-pair V1 acceptance.

PINs, bond keys, phone identifiers, BLE addresses, device-specific identifiers,
private bond references, private owner bindings, boot/session challenges,
physical-event tokens, authorization correlations, and private storage contents
remain prohibited from ordinary logs and public evidence. Exact implementation
and every physical, device-write, phone, key, signer, radio, account, upload,
and distribution operation require their own bounded authority.

## Progress

This host-only contract closes no scored implementation or physical evidence
gate. Android remains 60%. V1 Companion remains exact 43.75% and displayed 44%.
V1.5 remains unmeasured.
