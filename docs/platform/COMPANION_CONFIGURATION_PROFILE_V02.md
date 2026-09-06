# Companion configuration/time profile 0.2

Status: codec implementation specification, 2026-09-06; not advertised or deployed.
See [allocation decision](../decisions/0108-configuration-profile-codec-allocation.md).
Refines [Decision0107](../decisions/0107-configuration-time-transport.md).
Old normal0.0, restricted claim0.1 and OTNCv1 remain byte-for-byte unchanged.
A separate strict codec recognizes0.2; old decoders must reject it. This is not
backward-compatible negotiation with old clients and adds no live dispatcher.

## ProtocolInfo allocation

Retain16-byte OTB0 layout: magic0..3; major0/minor2 at4/5; role1 at6;
capabilities at7; uint16LE payload limit128 at8; uint16LE minimumMTU151 at10;
fragment count1 at12; controller count1 at13; reserved zero14/15.
Existing base capability mask0x2f retains meaning. Allocate name0x40 and time0x80;
known mask0xef (0x10 remains unknown). At least one of name/time must be offered.
Limits are exact, not negotiable downwards. A pure transport-compatibility helper
requires a valid offer, requested name or time capability, actualMTU151..65535,
local send/receive record capacities>=148 and indications enabled. It establishes
neither authenticated ownership nor Ready. Those remain mandatory runtime gates.

## Envelope allocation

Retain20-byte OTC0 layout with major0/minor2 at4/5, kind6, reserved zero7,
uint32LE nonzero session8 and exchange12, index0/count1 at16/17 and uint16LE
payload size18. Exact length20+payload, maximum128 payload/148 total.
No fragmentation, long writes or Write Without Response. The later transport
uses one Write Request and indicated result on the protected normal service.

Normal base kinds1(snapshot request),2(action request),0x81(snapshot),
0x82(action result),0x83(event) retain existing payload semantics; this envelope
codec treats their bytes as bounded opaque payloads, and existing semantic
validation remains mandatory before dispatch. Claim kinds3/0x84/0x85 are forbidden.
Allocate name request4, time request5, name result0x86 and time result0x87.
No other kind is accepted. New operation payloads are semantically validated:
name request accepts OTNC READ/WRITE; name result accepts OTNC SNAPSHOT/APPLIED/
REJECTED/UNCERTAIN. OTNC bytes are preserved. Capability, current session and
expected operation/exchange must be checked separately by the future dispatcher.

## Civil-time payload OTTCv1

Exactly24 bytes, little-endian integers. MagicOTTC0..3, payload version1 at4,
kind5, code6, format7, uint64 challenge8, uint32 local second-of-day16,
and four reserved zero bytes20..23.
Kinds:1 challenge request,2 challenge,3 sample,4 result.
Codes:0 accepted,1 unauthorized,2 busy,3 expired/no pending,4 invalid sample,
5 conflict,6 exhausted,7 contained,8 unavailable. Unknown codes reject.
Formats:0 absent,1 hour12,2 hour24. No timezone/date or monotonic epoch on wire.

| Kind | Exact semantic rules |
| --- | --- |
| Challenge request1 | code0,format0,challenge0,second0. |
| Challenge2 | code0,format0,nonzero challenge,second0. |
| Sample3 | code0,format1 or2,nonzero challenge,second0..86399. |
| Result4 | format0,second0; code0 requires nonzero challenge; error codes1..8 allow zero (challenge request rejection) or nonzero (sample rejection). |

Time request outer5 accepts inner1/3; time result outer0x87 accepts inner2/4.
A response to a sample must echo its exact challenge; a rejection of challenge
issuance has challenge0. This correlation is a runtime gate beyond stateless
codec validation. Codes carry disposition only and never imply civil-time accuracy.
The device still admits samples strictly younger than2000 device-local ms;
no wire field supplies authority or freshness. Duplicate responses never extend
clock validity. Format mappings must explicitly translate wire1/2 to the existing
clock enum rather than assume enum numeric identity. No live mapping is added.

## Acceptance boundary

Implement independent fixed-bound C++ and Kotlin codecs and one shared synthetic
corpus with semantic-field expectations and exact re-encoding. Reject wrong
versions, role/capabilities/limits, reserved fields, sizes, zero IDs, fragmented
records, claim/unknown kinds, malformed inner payloads, time bounds and wrong
outer/inner direction. Retain existing codec tests and prove old decoders reject
new0.2 info/envelopes. Check output buffers unchanged on C++ failure and Kotlin
owned payload bytes. Negative capacity/MTU/indication/feature helper cases must
not be confused with authentication tests.

No target source/linkage/buffer, Android BLE dispatch, profile advertisement,
wire request issuance, storage schema or hardware change is accepted here.
