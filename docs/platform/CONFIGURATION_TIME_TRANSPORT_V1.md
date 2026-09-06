# Configuration and time transport contract V1

Status: accepted implementation contract, 2026-09-06; no wire activation.
Work items OT-170/OT-178. [Decision 0107](../decisions/0107-configuration-time-transport.md).
This refines [OLED authority](OLED_CONFIGURATION_TIME_AUTHORITY_V1.md) after
[151-vector name parity](../testing/OT-170-NAME-PAYLOAD-PARITY-2026-09-06.md).
It freezes ownership, limits and outcomes for implementation, not tested device
behavior. Region/group mutations remain separate authority contracts.

## Compatibility and admission

Use a separately versioned normal companion profile. The existing normal 0.0
and restricted authorization 0.1 meanings remain unchanged. Neither unknown
capabilities nor existing action/result kinds may be reinterpreted. ProtocolInfo
is a peer offer, not proof of authorization: a supporting client must recognize
the exact profile, validate its limits, establish the current encrypted,
authenticated, application-authorized owner session and complete Snapshot/Ready
before configuration or time work. Select the recognized profile for that session,
including supported operation/schema, both-direction148-byte record capacities,
single-fragment transfers and indication support. Re-read and revalidate on every connection.
An old strict decoder may report unsupported; seamless old-client compatibility
is not promised. No fallback to claim, advertisements or a phone-local draft.

The first matched codec increment must allocate and test the successor version,
frame discriminators and capability encoding together. This document allocates
no numeric opcode, capability bit or UUID. Until that increment, the profile
cannot be advertised or dispatched. No runtime negotiation is claimed here.

## Fixed capacity contract

| Boundary | Frozen requirement |
| --- | --- |
| Envelope | Retain the 20-byte envelope and maximum 128-byte payload; maximum complete record 148 bytes. |
| Name payload | Preserve OTNCv1 exactly: 16-byte header plus at most 96 bytes, total112; maximum wrapped name record132. |
| Normal ATT admission | Require negotiated MTU at least151, matching the existing normal profile; do not weaken it to135 just because names fit. |
| Transfer | One complete request and one complete result record, fragment index0/count1; no application fragmentation or ATT long-write assembly for these operations. |
| Storage in transport owner | One owned request buffer148 and one owned response buffer148; one active operation. Reserve response capacity before accepting mutation. |
| Retries | One retained terminal result and its exact request identity/bytes. No unbounded history or dynamic per-request allocation. |
| Time | Time records must fit128 payload bytes; transmit challenge ID and civil fields, not the seven internal authority identifiers as caller-supplied proof. |

These are replacement-path requirements, not enlarged current buffers. The
current coordinator stores40 request/52 response bytes. Audit and adjust every
GATT copy, queue entry, result cache, Android callback, codec and target buffer
in the later integration; changing one constant is insufficient. The two148-byte
buffers are a minimum byte-storage budget, not total object/stack/RAM usage.
No hardware resource claim follows from this table.

Use one Write Request on the protected normal Command characteristic and one
indicated application response on Stream; require indication subscription first. A GATT
write completion acknowledges transport acceptance only. Application results
must match current session nonce and exchange ID and pass semantic decoding;
indication acknowledgement or UI observation is not durable commit evidence.
Larger MTU does not enlarge this profile. Reject Write Without Response and
prepared writes. Require the expected operation/schema as well as envelope
correlation. The existing indication timeout does not extend the time challenge.

## Shared operation ownership

One serialized application owner admits one configuration/time operation at a
time. A time challenge holds the slot until consumed, cancelled or expired;
configuration storage never blocks a NimBLE callback. Queue callbacks only copy
bounded immutable events. Busy or small output capacity rejects before mutation.
Reads share ordering with writes and reset; there is no concurrent unsynchronized
read of a partly committed record. Non-mutating preparation never reserves flash.

Bind every operation to trusted device/runtime/owner identity, owner generation,
transport generation, controller and session nonce, plus nonzero exchange ID.
Only session/exchange correlation belongs in the existing envelope; trusted
identities come from the real authority adapter. Device name/setup label is not
identity. Exchange IDs never wrap or restart within their accepted session.
Revalidate authority at consumption and immediately before commit. The dispatcher
must retain one terminal exchange fence even when no result could be encoded:
exact retry must never repeat ambiguous work; changed bytes at that ID conflict.

Only the most recent terminal request/result is replayable. A later admitted
exchange evicts that result; older IDs remain stale and cannot execute again.
Exact duplicates received while pending neither start a second operation nor
extend deadlines. They await the original result or get a non-mutating busy
transport disposition; no second application success is generated.

## Name transaction and durable readback

| State/operation | Required result |
| --- | --- |
| READ of known absent domain | SNAPSHOT revision0 and empty name. |
| READ of validated present record | SNAPSHOT with positive revision and exact name bytes. |
| Corrupt/unsupported/failed load | Unavailable/storage failure; never pretend absence or emit revision0. |
| WRITE expected revision differs | REJECTED STALE_REVISION, no mutation. |
| WRITE at UINT64_MAX | Reject unchanged; maximum revision remains readable. |
| Valid WRITE at r | Commit exactly r+1 with the exact validated bytes, then read back revision and bytes from durable storage. |
| Readback exactly matches | APPLIED r+1/name, only if original authority is still eligible for response publication. |
| Commit could have happened or readback fails | UNCERTAIN STORAGE_FAILURE; no applied receipt. |
| Proven failure before mutation | REJECTED with the applicable existing reason; do not label ambiguous I/O as proven unchanged. |

A same-value write is still a write and increments revision once. A duplicate
exchange replays its original result and never increments again. The existing
payload's REJECTED/UNCERTAIN intentionally carry no current revision; reconcile
with a fresh READ rather than adding incompatible fields. Unsupported/corrupt
storage must block writes until explicit recovery, not overwrite the domain as
if it were new. Revision is scoped to the reset/storage incarnation and is not
cryptographic rollback protection.

The persistence seam must distinguish known-unchanged failure from possibly
committed failure. Implement it first with deterministic fake storage. The real
driver later defines schema, power-loss integrity and migration in its own user
configuration domain; do not reuse the owner blob or existing two64-byte OTCF
slots. No new on-flash schema or namespace is allocated by this contract.

## Cancellation, uncertainty and receipts

Use a device-local admission deadline strictly below5000 ms for name work before
commit starts. Equality, local rollback or lost authority cancels before mutation.
This is a chosen bounded-admission policy, not measured flash latency. A storage
operation already started cannot be declared undone merely because time elapsed.
If completion arrives after expiry/disconnect, suppress successful receipt
publication, reconcile storage and retain uncertainty until an authoritative
READ. Never initiate another write while the prior storage call is unresolved.
The concrete driver must have its own bounded completion/recovery gate.

A phone timeout is independently local and never proves non-commit. No automatic
new-exchange write retry after a lost result. After reconnect, regain Ready and
read current durable revision/value. A matching read can establish current state,
but cannot prove that a historical timed-out write caused it. Keep confirmation
of current configuration separate from attribution to a pending write.

Before live issuance, replace the preliminary Android receipt with exact current
trusted context, pending exchange, expected revision, committed revision and
exact name binding. Consume the pending operation once. Reject a receipt from an
earlier request in the same session, even if name matches. Restored drafts cannot
restore receipts. Centralize lossless UTF-8 eligibility in V1DeviceName (currently
isolated surrogates can pass); preserve stricter app whitespace rejection and
exact Unicode without trimming/normalizing. OLED glyph fallback never rewrites
stored names. Neither name confirmation nor region selection grants radio TX.

## Time mapping

After Ready, the phone requests a challenge through the shared dispatcher. The
device owner issues its nonreused uint64 challenge ID and starts its own tick;
phone captures second-of-day0..86399 and 12/24 preference after receipt. A separate
correlated sample exchange echoes the challenge ID. That sample is the permitted
continuation of the held slot, not an unrelated new operation. Map the actual
current trusted context into OledTimeResponse; never deserialize authority flags.

Use OledTimeAdmissionOwner unchanged: consume at age strictly below2000 device ms,
call with current application tick, reject invalid input before clock mutation,
and preserve24-hour expiry/rollback containment. Neither duplicate challenge
request nor duplicate sample extends freshness. A replayed challenge result keeps
its original issue tick. If its result is lost, retain the held slot until the existing2000-ms
expiry or a genuine lifecycle close; only then issue a fresh challenge. The
current owner has no arbitrary per-challenge cancel API. Do not synthesize a
disconnect or reconstruct the owner to escape busy; an old sample remains invalid. A lost accepted-time result is not license to
resynchronize from cached civil time. Reconnect or phone civil-time/format change
starts a fresh challenge after Ready. No periodic timer is introduced here.

## Lifecycle ordering and reset

Revocation/reset before commit prevents mutation. Commit first may remain durable
but cannot publish into a replacement session. Disconnect cancels pending work,
retains validated configuration and a still-valid clock, and clears old receipt
eligibility. Revocation/reset/restart clear time. Stale lifecycle callbacks may
not cancel a newer owner, nor restore a revoked owner epoch.

Every new configuration domain must join
[factory-reset erase and absence verification](DEVICE_FACTORY_RESET_V1.md),
including interrupted-reset recovery. Block Ready/configuration visibility and
new enrollment until reset completion is verified. A reset may make revision0
valid only in a fresh trusted incarnation; pre-reset exchanges/receipts cannot
be reused against it. No claim of cold-power acceptance is made.

## Required implementation cases (not executed by this document)

| Gate | Cases |
| --- | --- |
| NT-01 compatibility/capacity | Old/unknown profile; lower MTU;132-byte name/148-byte buffer boundaries; fragmented and truncated records; output failure before mutation. |
| NT-02 ordering | Busy; exact pending duplicate; changed duplicate; terminal no-result fence; old ID after cache eviction; exchange exhaustion. |
| NT-03 durable name | Absent versus corrupt; initial0->1; same-value increment; stale revision; MAX-1->MAX then reject; exact readback mismatch. |
| NT-04 ambiguity | Failure before commit, possibly committed failure, deadline4999/5000, timeout during I/O, lost result and fresh read without blind retry. |
| NT-05 lifecycle | Revocation/reset before and after commit; delayed callback; reconnect same owner; reset revision-zero reuse; incomplete erasure. |
| NT-06 receipt | Wrong device/generation/exchange/expected/committed revision/name; duplicate consumption; restored draft; invalid Unicode. |
| NT-07 time | Challenge request replay,1999/2000 expiry, sample continuation while slot held, conflicting sample, queued old sample and lost result. |

Next implement one host-only fixed-memory name transaction owner with injected
trusted authority and deterministic fake persistence, proving NT-02/03/04/05.
No target linkage or live transport in that increment. Then implement typed
Android pending/receipt validation and matched successor wire codecs before
persistence/reset/target integration. Reuse accepted time admission; do not
repeat its implementation or claim planning as completion.

## Review scope

This increment changes documentation only. Source inspection, independent
transport/lifecycle review, local links, canonical history/weights and publication
safety are the applicable checks. No Android/host/firmware matrix is repeated;
no target modification activates firmware-porting or hardware execution gates.
V1 remains exact43.75/display44, target25/Android60. Website and cold-power work
remain owner-deferred. The preceding code commit's CI is recorded separately.

## Source inspection and validation receipt

Capacity inspection used companion_protocol.hpp (20/128/148/151),
companion_request_coordinator.hpp (40/52), the actual Heltec
companion_nimble_gatt.cpp Command admission, companion_gatt_session.cpp response
reservation, and Android CompanionProtocol/CompanionSemantics. Independent
transport and lifecycle reviewers confirmed the capacity mismatch and required
current-session/readback boundaries. Review corrected a proposed arbitrary time
challenge cancellation to expiry or legitimate lifecycle closure, matching the
existing owner API. No runtime implementation changed.

Local document links, publication-safety tracked/untracked scan, V1 positive
weights totaling100, unchanged milestone completions and append-only history
checks pass. All101 recorded owner-checkout hashes remain unchanged. The prior
code commit8d0dbe0 Host run34036092386 was still in progress at this checkpoint;
its successful local matrix remains evidence for that preceding code only.

## Host name-owner implementation checkpoint

The name transaction owner now implements the host-only transaction stage with
trusted-source and synchronous persistence seams. See [evidence](../testing/OT-170-NAME-TRANSACTION-2026-09-06.md)
for focused/final results and limitations. NT-02/03/04 host behavior and injected
NT-05 lifecycle ordering are covered; actual reset erasure and asynchronous or
physical I/O remain open. Typed Android receipts are next. Wire allocation,
shared dispatcher, real storage/reset and target integration remain unimplemented.

## Android name-receipt model checkpoint

The preliminary tuple-only receipt has been replaced by a one-use registered
receipt issued through the current pending transaction. Lossless device-name
UTF-8 validation is centralized. See [evidence](../testing/OT-170-ANDROID-NAME-RECEIPT-2026-09-06.md).
This addresses model-level NT-06; real authenticated adapter provenance, live
requests and persistence remain unimplemented. Numeric successor allocation and
matched codecs are next. Prior descriptions above retain historical context.

## Matched codec allocation checkpoint

[Decision0108](../decisions/0108-configuration-profile-codec-allocation.md) and
[profile0.2](COMPANION_CONFIGURATION_PROFILE_V02.md) now allocate the previously
deferred version/capability/kind and OTTC payload bytes. Matched independent
codecs pass shared vectors; [evidence](../testing/OT-170-178-CONFIGURATION-CODECS-2026-09-06.md)
records complete validation. These allocations do not activate an advertisement,
negotiated runtime, dispatcher or target. The shared host dispatcher is next.
