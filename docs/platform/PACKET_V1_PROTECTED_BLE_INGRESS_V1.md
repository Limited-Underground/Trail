# Protected BLE ingress to Packet-v1: isolated host candidate

Status: provisional host engineering only. Profile 0.6 and OTE0 are not final
production wire approvals. No GATT registration, firmware activation, Android
runtime change, persistent-device write or production admission is supplied.

## Reused boundaries

The host [ingress adapter](../../tests/host/packet_v1_companion_ingress.hpp)
composes the existing CompanionActionAuthority/request coordinator, trusted
DeviceNameAuthoritySource context, OTQ0 codec, typed armed Packet-v1 facade,
Node, counter/replay owners and real Heltec driver against existing SDK/RadioLib
stubs. The production codecs and interfaces remain unchanged. The profile6
wrapper validates the exact canonical envelope before presenting compatible
legacy semantic records to the existing coordinator; this is not a downgrade
or a new production transport. [Existing GATT](BLE_COMPANION_GATT_V0.md),
[request coordination](COMPANION_REQUEST_COORDINATOR_V0.md) and the
[Packet host profile](../security/PACKET-V1-HOST-PROFILE.md) retain their roles.

Only OTA0 QUICK_STATUS1..4 is mapped to the existing twelve-byte OTQ0 payload:
I'm OK, Need assistance, Anyone online?, Available to help. It carries no peer,
group, key, counter, nonce or raw Packet-v1 input. The device-selected admitted
pair binds full identities, group, epoch and transcript. The authenticated
Packet-v1 frame is72 bytes (44 header+12 plaintext+16 tag), below MTU154.
No text, fragmentation, mesh or new BLE radio-arm command exists.

## Admission, ownership and failure

Prepare is pure. Complete result capacity is checked before commit. Commit
rechecks current Ready BLE owner/controller/session generations, the pinned
packet pair context and fresh independently armed radio authority before host
operation IDs or counters. BLE cannot arm/start the radio, extend its deadline
or renew its one/two-attempt budget. Radio and BLE lifetimes are distinct.

Exact command duplicates use the existing cached response without another send.
Conflicting/lower/exhausted IDs refuse. A possibly mutating false facade result
is outcome_unknown, with no false successful result or automatic resend. Host
IDs and receipt interruption inputs are injected volatile test fixtures; this
slice defines no durable ID, receipt store or restart reconciliation design.
Uncertain/invalid ID issuance latches ingress refusal for that runtime. Invalid
or backward trusted BLE time also terminally contains ingress; restoring the
clock cannot renew that grant. Neither containment implicitly stops radio work.

Disconnect closes only companion ownership and Stream routing, cancels
uncommitted ingress and preserves independently accepted device radio work.
Radio revoke/disarm/expiry/fault retains its own terminal cleanup. Previously
transmitted/in-flight ciphertext cannot be retracted. A stopped Packet facade
cannot service retained opaque DeliveryController ciphertext.

## Provisional event schema

OTB0/OTC0 major0 minor6, capabilities0xff, payload128/record148/ATT151 is a
host-only negotiated profile. Existing profiles2/3/5/127 keep their prior rules.
OTC0 EVENT0x83 is exactly one fragment, twenty-byte envelope+forty-byte OTE0.
Requests/results keep their existing OTA0/OTR0 formats and correlation rules.

| Offset | Bytes | OTE0 field |
|---:|---:|---|
|0|4|ASCII OTE0|
|4|2|Semantic major0/minor0|
|6|1|Kind1 outbound,2 received,3 transport,4 gap|
|7|1|Quick status1..4 for kinds1/2; otherwise0|
|8|8|LE64 receipt: nonzero kinds1/2; zero kinds3/4|
|16|8|Nonzero LE64 transport-runtime generation|
|24|1|State1 queued,2 TX complete,3 peer ACK,4 received,5 failed,6 unknown,7 disarmed,8 expired,9 gap|
|25|1|Reason0 none,1 policy,2 not-ready,3 expiry,4 queue-full,5 storage,6 radio-I/O,7 interruption,8 stale,9 unsupported|
|26|6|Reserved zero|
|32|4|Origin BLE nonce: nonzero kind1; otherwise0|
|36|4|Origin request ID: nonzero kind1; otherwise0|

Kinds1/2/3/4 permit states {1,2,3,5,6}/{4}/{7,8}/{9} respectively.
States1..4 require reason0; state5 allows1..9; state6 allows1/3/5/6/7/8;
state7 allows1/2/7/8; state8 requires3; state9 requires7. Every other pairing,
version, kind, reserve, zero/nonzero violation, length or trailing byte rejects.
Caller output remains unchanged on codec refusal. The OTN0 snapshot revision
advances on changed coherent radio/queue facts, preserves unchanged observations
and fails closed at exhaustion; coordinator duplicates remain byte-identical.

The device graph is queued -> TX complete -> protected peer ACK, with queued
or TX complete able to become failed/unknown. Those terminal states cannot be
revived by a late ACK. Definite pre-RF expiry is failed/expiry; possible side
effects mean unknown/expiry. A received event follows authenticated replay
persistence and one OTQ0 delivery. Packet ACK is not human acknowledgement.

Each current authorized companion session has an independent monotonically
increasing EVENT cursor. Snapshot response17 does not invalidate EVENT1.
Outer nonce belongs to the current connection; origin fields are correlation,
never authority. Missing observations do not manufacture predecessor evidence.

## Bounds, interruption and privacy

One active operation, sixteen terminal receipts, one outstanding indication and
an independent sticky gap latch bound memory. Terminal insertion17 evicts the
oldest terminal record; active work cannot be evicted. Loss/eviction sets a
nonwrapping gap revision. Indication completion clears a gap only when both
its correlation and captured revision match; an older completion cannot clear
a newer loss. Exhaustion fails closed instead of wrapping. A retired runtime
generation can report disarmed/gap without an active pair.

Events are LIVE-ONLY: no receipt-summary/history replay or implicit recovery
barrier. Reconnect establishes a new nonce and current OTN0 snapshot; unresolved
per-operation delivery remains unknown, without automatic command resubmission.
Android Activity/service transitions are exercised only by host boundary
surrogates here, not actual Android execution. Existing Live Android changes
must be reconciled before a later isolated Android integration task.

Diagnostic observations contain only closed categories and bounded counts;
no key, packet, plaintext, full identity or location is logged. The candidate
exposes a copied fixed metadata value and introduces no observer callback path;
production observer failure handling is not exercised here. Unauthorized Stream
sinks receive nothing.

## Committed-owner host successor

The inactive OT-0005i [owner-channel handler](../../firmware/components/companion/include/opentrail/companion_packet_owner_channel.hpp)
now implements the separately frozen OTY0 query/response against the
[committed Packet service](../../firmware/components/companion/include/opentrail/companion_packet_service.hpp).
It requires the same concrete GATT callback/persistent authority, actual promoted
controller and inner-session security/subscription/MTU state, fresh authorization
before and after the observation, exact context/nonce and current real enrollment
plus service/driver state. Metadata is not accepted as cached authority. Existing
OTA0/OTR0/OTE0 definitions in this document are unchanged; this adds no BLE arm
command or target registration.

The [composed host suite](../../tests/host/companion_packet_service_tests.cpp)
passes 68 scenarios including malformed/stale/changed/refused channel states,
store and lifecycle failures, receive-slot ownership and ACK/retry ordering.
Independent behavioral review passed for this bounded host composition. The issuer, protected-store,
peer and hardware I/O are fixtures around actual state-machine implementations;
this does not establish a target issuer, physical BLE identity pin, installed
profile or Android production provider. The handler is local/unpublished and
inactive on targets. Full OT-0005i/OT-0312 and target security/physical acceptance
remain open; see the [current pickup](../PROJECT_STATUS.md#firmware-host-composition).

## Validation route and limits

Run `python tools/packet_v1_companion_ingress.py --source-root <approved-pinned-source-root>`.
The runner verifies independent Python struct/CRC vectors, compiles the actual
Heltec driver with existing stubs, executes forty named ingress cases and the
affected companion/OTQ0/Packet-v1/driver regressions. `--focused` runs the new
matrix only while correcting a concrete failure. It accesses no hardware.

[Golden vectors](../../tests/fixtures/packet_v1_companion_ingress_vectors.csv)
cover OTA0,OTQ0,OTR0 and provisional OTE0. Cross-language production event codec
parity remains a later gate because Android production codecs are unchanged.
Final affected matrix: all40 named cases,42,052 ingress assertions and119
independent Python/C++ vectors pass. Existing Packet facade863 assertions/37
lifecycle groups, synthetic transport318 assertions and real driver44 groups
pass. Eight companion/OTQ0 regression binaries pass101 scenario groups, including
the configuration corpus89 accepted/342 rejected records. Documentation checking
and18 documentation tests pass. Independent security and test reviews pass with
no remaining blocking findings. Counts overlap and are not additive product
qualification. The [progress log](../PROGRESS_LOG.md) records the bounded result.

Production gates remain explicit OT-0237e selection, OT-0005i final enrollment/
identity/counter/replay binding, durable messageID/receipt/generation and restart
reconciliation, OT-0005j protected two-phone interoperability, target resource/
timing/durability/restoration and separately authorized physical acceptance.
This host slice adds no V1 progress or public website capability credit.
