# Companion one-phone authorization v0

Status: host-tested target-neutral policy, fixed C++/Kotlin authorization wire
and trackers, plus a build-linked restricted device lifecycle and
production-shaped Android orchestration; both live paths remain disabled, with
no running provisional GATT or physical BLE evidence,
2026-08-15.

## Authority boundary

The LoRa device, not the Android app, owns the authorized-phone decision. A
trusted future BLE bond store supplies a stable opaque 128-bit bond identity
token. The token must be nonzero, unique per bond, stable across reboot, and
unavailable to the peer. It must never contain a raw BLE address, public device
identifier, client-supplied bytes, or cryptographic key material.

One externally serialized `CompanionAuthorizationAuthority` instance is kept
for one complete boot challenge. It is not a thread-synchronization primitive.
Recreating it under the same boot challenge would discard replay state and is
forbidden by the target contract.

## Claim and ownership rules

- Restore must complete before any claim or authorization.
- Claims require encrypted and authenticated bond evidence supplied by the
  trusted adapter, never decoded from phone payload.
- Boot challenge, strictly increasing nonzero session challenge, and private
  nonzero controller binding must match the active controller context.
- Exactly one owner and one active controller are admitted.
- A local physical-input owner opens an exact claim, revoke, replace, or reset
  window for at most 30 seconds. Claim/replace binds the exact candidate bond;
  revoke/reset carries no candidate.
- Physical event tokens increase strictly for the boot. Invalid action enums,
  replay, clock rollback, stale session, wrong controller, second-controller
  use, same-owner replacement, and exhaustion reject without granting access.
- Revoke, reset, reclaim, and replace preserve the boot-wide session high-water
  mark so an older session challenge cannot become valid again.

## Persistence contract

The injected backend owns record encoding, confidentiality at rest, atomicity,
and rollback-resistant trusted-generation storage. `commit_and_verify` must
compare the expected generation, commit the complete candidate, advance the
trusted floor, read back, and return the exact verified record. An exact
`failed` commit result guarantees no durable change. An uncertain commit, conflict, rollback,
invalid record, or generation exhaustion latches the authority closed for the
boot.

The public status/result surface omits the owner token, physical event token,
boot/session challenge, and inactive controller binding. No implementation may
log or display those values.

## Fixed authorization wire

The codec-only authorization phase uses one complete `OTC0/v0` fragment in
each direction. All multi-byte envelope fields are little-endian and retain the
existing nonzero session nonce and exchange-ID rules.

| Direction and kind | Payload | Exact bytes | Fixed fields |
| --- | --- | ---: | --- |
| Client to device `0x03` | `OTL0/v0` Claim Start | 8 | magic 0-3, version 4-5, purpose 6, reserved zero 7 |
| Device to client `0x84` | `OTP0/v0` Pending | 24 | magic 0-3, version 4-5, purpose 6, state `1` at 7, correlation 8-23 |
| Device to client `0x85` | `OTF0/v0` terminal | 28 | magic 0-3, version 4-5, purpose 6, outcome 7, reason 8, reserves zero 9-11, correlation 12-27 |

Purpose is `1` Authorize or `2` Replace. Terminal outcome is `1` Accepted,
`2` Denied, or `3` Replaced. Accepted is coherent only with Authorize and no
denial reason; Replaced only with Replace and no denial reason; Denied accepts
either purpose and requires exactly one nonzero closed reason: Unknown,
Unsupported, Physical Presence Required, Physical Presence Expired, Owner State
Conflict, Policy Denied, Persistence Unavailable, or Internal Failure.

The device issues one nonzero opaque 128-bit correlation. It is boot-privately
bound to the exact provisional session, exchange, and purpose. It is not a bond
identity, BLE address, public/device/client ID, cryptographic key or secret,
physical-event token, owner record, or private boot challenge. Neither side may
display, log, or persist it.

## Provisional response-tracker boundary

The fixed-memory C++ tracker and bounded-state Kotlin tracker are externally
serialized pure state machines, not live transports or device authorization authorities. Opening a
provisional observation requires a locally nonzero transport generation,
nonzero device-issued session, encrypted link, authenticated bond, and explicit
future negotiated authorization-claim support. It does not itself prove any of
those facts. A Start binds one exact nonzero exchange and purpose. One matching
Pending must arrive before exactly one matching terminal; malformed, duplicate,
out-of-order, stale, or mismatched generation/session/exchange/purpose/
correlation fails closed. Timeout or loss is a local unknown-authority state,
not the wire Unknown denial enum, and must never be invented as authoritative
Unsupported or Denied.

Accepted or Replaced records only the client's exact observation of a terminal
device result. The device authority independently decides and persists access.
Cancel cannot overwrite a terminal observation, and Pending, Denied, or cancel
cannot silently replace an unclosed transport generation; exact generation
close is the sole release before another connection may open. A transport owner
must call that exact close on disconnect; this codec/tracker slice provides no
disconnect callback wiring. Exact close clears provisional state, and stale
terminal data can never authorize a new connection or generation.

## Restricted provisional transport contract

OT-050 defines a separate exact 20-byte `OTB0/v0.1` Protocol Info record for
the provisional phase. Bytes 0-3 are `OTB0`; major/minor at 4-5 are `0/1`;
role at 6 is the existing screenless client; capabilities at 7 are `0x1F`,
adding claim bit `0x10`; maximum fragment payload at 8-9 is little-endian and
must be 28 through 128; normal minimum ATT MTU at 10-11 must be at least 151;
maximum fragments at 12 must be 1 through 16; controllers at 13 is one; the
nonzero device-issued provisional session nonce occupies 14-17 little-endian;
18-19 are zero. The current OT-050 lifecycle advertises exactly MTU 151 and 16
fragments.

An exact encrypted, authenticated-bond connection may read this 20-byte record
at the default MTU before application authorization. The client should request
the advertised normal MTU immediately. Claim admission is nevertheless
possible only at MTU 51 or greater, because the largest fixed authorization
response is one 48-byte `OTC0` terminal envelope. It also requires the exact
registered Protocol Info, Command, Stream, and Stream CCCD handles plus current
Stream indication subscription; no handle may be inferred.

One Claim Start reserves response capacity before correlation issuance and
sends exact Pending without invoking device authority. Only exact Pending
indication confirmation enters the physical/device-authority waiting phase. A
later connection/generation/session/exchange-bound resolution reserves terminal
capacity before authority mutation. Accepted/Replaced promotes only after the
exact terminal indication is confirmed. MTU 51 through 150 may observe that
promotion but normal snapshot/action admission remains closed until the same
connection reaches MTU 151. The Android client then sends an explicit Snapshot
Request; no unsolicited snapshot is assumed.

Denied, local claim/indication timeout, security loss, unsubscribe, congestion,
negative confirmation, submission failure, disconnect, or stale callback never
promotes. A failure after durable authority mutation releases only the live
controller lease and tombstones the transport until exact disconnect; it does
not claim rollback or delivery. Timeout or transport failure remains local
unknown authority, never an invented authoritative Unsupported or Denied.

The target build links this lifecycle only into a deterministic fake-authority
self-check. Its live NimBLE definition still exposes baseline v0.0 and its
AUTHOR callbacks deny provisional traffic; controller/service/advertising
startup is absent. Android mirrors the operation order, but `MainActivity`
still injects the disabled claim client and its default security authority is
deny-all. These are integration-ready boundaries, not a live transport.

## Accepted evidence and exclusions

Sixteen strict C++17 scenario groups and 100/100 focused repeats pass, including
restore, claim, duplicate authorization, second-controller denial, expiry,
clock rollback, replay, revoke, replace, reset, persistence failure/uncertainty/
conflict/rollback, invalid physical actions, re-entrant persistence callbacks,
and boot-wide session high-water preservation. The complete 118-executable host
matrix passes.

OT-048 adds fourteen strict authorization-wire/tracker groups, ten shared
golden rows, and 100/100 focused repeats; the complete 119-executable OT-048-era
C++ host matrix passes. OT-049 mirrors those bytes and transitions in Kotlin;
the combined Android gate passes 77 JVM tests across eight suites (protocol
suites 6, 10, and 10; application suites 6, 17, 11, 1, and 16) and lint. Its
9,627,825-byte debug APK has SHA-256
`967FCD7A032ECED63789378F5B3C0F6AC86D06CE9CF3B6B16205E7C49B8093A3`.

Because the generic target already links the shared protocol, semantic-
dispatcher, and coordinator sources touched by OT-048, two pinned ESP-IDF
v6.0.2 rebuilds reproduce a 157,957-byte image and 158,080-byte BIN. The link
map excludes the new authorization wire/tracker source. This is build-linked-
not-run recognition/rejection evidence only; see
[OT-048 target evidence](../../tests/hardware/OT-048-2026-08-15.md).

OT-050 adds twenty strict provisional-lifecycle groups with exact 27/28 payload
boundary coverage and 100/100 repeats. The complete current 120-executable host
matrix, target-local self-check 100/100, and static admission 3/3 pass. Two
pinned ESP-IDF v6.0.2 builds reproduce a 165,349-byte image and 165,472-byte BIN
with SHA-256
`E2ACF6672925D2FF298BD58E7C7BCBA564D46F1B7A6853D67865CE62F09D12B9`;
the link map retains the authorization wire and restricted lifecycle. See
[OT-050 target evidence](../../tests/hardware/OT-050-2026-08-15.md).

OT-051 raises the Android gate to 90 JVM tests across ten suites (protocol 6,
10, 10, and 3; application 7, 9, 17, 11, 1, and 16), with zero failures,
errors, or skips and clean warning-as-error lint. The 9,644,209-byte debug APK
has SHA-256
`28ED3014ACE420F8C531625211D26BD3FB9D522F1349BACA0878F94726534D8A`.

This evidence does not provide a live NimBLE bond identity adapter, protected
NVS backend, physical-input binding, live provisional GATT event integration,
enabled Android claim composition, phone bond,
device runtime, lost-phone field recovery, or support claim. Those remain
required before enabling the Android production claim client.
