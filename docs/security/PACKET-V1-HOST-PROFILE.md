# Packet-v1 isolated host profile

OT-0237e: retained November 4 profile with owner-directed full64-bit group identifiers. Its exact selected source passed eligible host-candidate admission on November 9. The historical technical identifier `group64-host-successor-unselected` is preserved; it does not activate a production profile. Production target/protocol-allocation and physical gates remain open. Experimental v0 is unchanged. The current local consumer is the [committed-owner service](#committed-owner-service-host-composition).

The accepted parent32-bit host vectors are retained separately in
`tests/host/fixtures/packet_v1_group32_parent_vectors.json`. The successor changes
implicit authenticated/KDF context, while retaining the44-byte host envelope.
Envelope version1 here is an isolated fixture value, not a production protocol
allocation or activation. Parent frames fail successor authentication; there is
no compatibility fallback. Final production profile/version mapping requires
owner ratification after admission.

## Envelope

All unsigned multibyte fields are big endian. Header44, ciphertext, tag16; no padding/trailing bytes.

|Offset|Bytes|Field|Values|
|---:|---:|---|---|
|0|2|Magic|ASCII OT|
|2|1|Version|1|
|3|1|Type/purpose|data1 ACK2|
|4|1|Flags|0|
|5|1|Header size|44|
|6|1|Forward policy|0 two-node unicast|
|7|1|Reserved|0|
|8|1|Fragment index|0 through count-1|
|9|1|Fragment count|1 through16|
|10|4|Epoch|nonzero exact context|
|14|8|Sender alias|nonzero exact context|
|22|8|Recipient alias|nonzero distinct exact context|
|30|4|Message ID|nonzero|
|34|8|Counter|nonzero|
|42|2|Ciphertext size|exact bounded payload|

Data is nonempty. ACK is unfragmented, exactly8 plaintext bytes: BE64 nonzero acknowledged frame counter. Later delivery integration must match both counter and message ID to pending send.

## Authentication and ownership

AAD164: label Packet-v1 plus NUL10, header44, sender full32, recipient full32, groupBE64, epochBE32, direction1(0/1), purpose1(type), transcript32. Full identities/transcript nonzero, identities distinct. Expected aliases/epoch come from trusted session owner. Aliases alone never authenticate. Separately derived directional data/ACK keys and domain prefixes come from the reviewed session KDF; this slice uses synthetic keys only. No new cryptographic algorithm, KDF or Noise implementation is introduced.

`Context::group` and `PacketSessionPort::bind` preserve `uint64_t`; the actual
HKDF info encodes the group in eight big-endian bytes after the existing label,
enrollment prologue and full identities. Epoch remains32 bits. Invitation,
possession, retained binding/evidence and journal owners already serialize the
group as64 bits, so this successor changes no persistent schema. Host fixtures
reconstruct those actual stores for high-bit and maximum identifiers, reject
same-low32 authority substitution and require fresh keys after retained rekey.

Nonce reuses existing aead_nonce: prefix4+BE64counter with matching full128bit lease/key domain. Noise CipherState nonces remain separate. New fragment means fresh durable counter; retry is exact retained bytes without resealing.

Decode grants no authentication authority. AEAD opens into private bounded scratch; validate plaintext, persist authenticated counter using existing replay owner, then release delivery/ACK authority. Failed authentication cannot advance replay state. Duplicate/old counter cannot redeliver. Memory-backed host replay evidence is not target durability.

## Bounds

Explicit MTU60..255; overhead60. MTU163 permits103 plaintext bytes, MTU255 permits195. Fragment count<=16, max logical payload3120 at255. Every nonfinal fragment must fill the admitted MTU plaintext capacity. Future reassembler must bound one in-flight message/direction, timeout incomplete state and reject inconsistent counts/indices/message metadata. Codec implements no reassembly, TTL, broadcast or mesh. Fragment index/count authenticated. Examples do not select RF MTU or claim airtime.

## Gates

Independent Python cryptography and pinned libsodium vectors must agree for data/ACK both directions. Strict malformed/version/context/tag/key/replay/retry tests use synthetic authority. The session/counter/replay binding and armed lifecycle below remain isolated host evidence; no production activation. Accepted OT-0237b is reused without repetition. OT-0237c/d target interruption/retirement, final admission, production composition/resource and owner acceptance remain open; existing physical restoration evidence keeps its original limits.

Run `python tools/packet_v1_group64_host.py --build-dir <task-owned-output> --source-root <accepted-package-source-root>`
for the focused successor matrix: eight passing binaries,50 independent AEAD
vectors over five groups, ten parent-profile refusals and seven full-group
owner/persistence cases. Object/result reuse requires actual dependency,
compiler, binary, log and fixture-input hashes. This runner does not repeat the
accepted Noise KAT, benchmark, OT-0237b or ten admission-collector checks.


## Committed-owner service host composition

The local OT-0005i [service](../../firmware/components/companion/include/opentrail/companion_packet_service.hpp)
uses the [private committed session](../../firmware/components/companion/include/opentrail/committed_packet_session.hpp)
and [selected Packet core](../../firmware/components/companion/include/opentrail/packet_v1_selected_core.hpp),
reusing the admitted KDF/framing rather than a caller-supplied authority flag.
It joins the real committed product owner to four distinct fresh counter/replay
stores and actual Heltec driver lifecycle. Guarded arm/time/budget checks,
authenticated data/ACK, exact retained retries and retirement remain bounded.
Cleanup reports failed retirement honestly and never stops or retires a resource
this instance did not acquire. One undrained plaintext slot blocks further data
admission until consumed; an arrived ACK is authenticated before a due retry.

The [68-scenario composed host suite](../../tests/host/companion_packet_service_tests.cpp)
passes; final independent behavioral review is pending. Actual driver code runs
against SDK/RadioLib I/O stubs, with a fixture peer, issuer and protected store.
This is local uncommitted source, not target durability, real BLE authentication,
production trust or Android interoperability evidence. No target startup or
production profile is registered. Existing predecessor suites below retain
their original source/fixture scope.

Use the current approved object/dependency closure, compiler and source-bound
reuse receipt; the [runner](../../tools/companion_packet_service_host.py) refuses
changed or missing inputs before/after execution:

`python tools/companion_packet_service_host.py --accepted-build <accepted-host-build> --reuse-readback <approved-reuse-readback.json> --out <fresh-task-owned-output>`

## Synthetic transport composition

The host-only synthetic adapter in `tests/host/packet_v1_synthetic_transport.hpp`
connects Packet-v1 to the existing outbound counter allocator, replay store and
DeliveryController. Its bindings are explicit synthetic session ports; no live
session key export, enrollment, key derivation or production activation is added.
A single pending unfragmented data frame uses the existing retry owner. Protected
ACK admission requires both pending message ID and frame counter. Data is released
only after authentication and replay persistence; retries retain exact bytes.

Run `python tools/packet_v1_synthetic_transport.py --source-root <pinned-source-root>`
for the synthetic matrix. Test stores simulate interruption; they do not prove
NVS durability, physical restoration or radio/Android compatibility. Fragment
reassembly and durable outbound/ACK caches remain outside this slice.

## Authenticated session binding candidate

The host-only `tests/host/packet_v1_session_binding.hpp` port binds the existing
IndependentAuthorizedPolicySession after signed invitation admission, Noise XK
handshake and transcript confirmation. It derives separate directional data and
ACK key/domain/nonce-prefix material with pinned HKDF-SHA256; raw Noise split keys
are not exported. The KDF binds protocol label, enrollment prologue, full sender
and recipient identities, group, epoch, direction, purpose and transcript.

The serialized session owner permits one packet binding for its lifetime.
The owner must outlive its noncopyable port and transport Nodes. Every bound
send, receive, service and retry checks current authority and active session
state. Retirement, cancellation and session replacement invalidate that path;
reconstruction does not authorize old-key resume or erased replay state.

Counters and replay state remain in independently owned test stores, using the
existing allocation and persistence interfaces. These tests do not change real
device keys or stores. Physical lifecycle, NVS durability, reset/recovery,
production enrollment binding and Android/BLE/LoRa interoperability remain open.
The product-owner constructor also requires ProductEnrollmentActivation readiness:
matching retained enrollment evidence, committed membership/journal and the current
source generation. Its host fixture performs the existing retained relationship
comparison, fresh exact-next-epoch authorization and activation. This is host
rekey evidence with synthetic storage, not physical rekey acceptance. The actual
product owner may retire simulated records on failure according to existing
semantics; no real persistent storage is supplied by these harnesses.

Run `python tools/packet_v1_session_binding.py --source-root <pinned-source-root>`
and `python tools/packet_v1_product_binding.py --source-root <pinned-source-root>`
for the session and product-owner matrices. Bound transport ownership remains
serialized, with one binding per session and explicit owner > port > Node lifetime.

## Firmware-facing host adapter

The isolated `tests/host/packet_v1_firmware_adapter.hpp` facade composes the
validated session/transport owners with the existing `radio::RadioTransport`
opaque-frame interface. `PairRadioDriver` derives from that interface; its
explicit start/deadline/transmission budget/stop lifecycle is exercised only
through the host-stub composition below. No production startup path is added.

The inspected Heltec pair-radio driver reports a 154-byte MTU: the 60-byte
Packet-v1 header/tag overhead leaves 94 plaintext bytes. The host adapter must
apply the runtime MTU before allocating a counter. The 163-byte example remains
an example, not the target limit. Radio queue acceptance is not peer delivery.

Counter leases, replay persistence and active session authority stay with their
existing owners. Host stores inject failure; no NVS backend is instantiated.
The evaluated `NvsCounterBackend` uses evaluation-only namespaces and its
constructor opens a read/write handle, so it is not production store selection.
The existing synthetic retry timing is not radio duty-cycle or field acceptance.

Target builds, stack/heap measurements, physical driver interrupt/rearm behavior,
physical persistence and Android/BLE/LoRa interoperability remain open. Host
`sizeof` values exclude injected owners/stores and are not ESP32-S3 RAM evidence.
The generic RadioTransport facade owns no driver startup or physical-stop
authority. Its logical stop invalidates and wipes its Node/cache; ciphertext
already queued in a driver cannot be withdrawn through RadioTransport alone. Owner > port > Node > Adapter
lifetime ordering is required. One retained ACK survives transient backpressure
with exact bytes and current-authority checks; no field ACK-expiry policy is
admitted by these host fixtures. DeliveryController and facade must use the
same RadioTransport instance, checked before use.

Current armed facade measurement on the host ABI: facade 384 bytes, Node
1,744 bytes and DeliveryController 2,640 bytes (4,768 combined), excluding injected radio,
session, counter/replay owners, persistence stores and temporary stack. This
is not a target linker-map, stack-watermark or heap measurement. No Xtensa
compiler was resolved from PATH in the earlier facade run; no target compiler
is needed or invoked by this host-only slice.

## Explicitly armed radio lifecycle (host only)

The PairRadioDriver-specific facade overload requires exclusive serialized
ownership: no external caller may start/service the driver or invoke its Node
behind the facade. The generic RadioTransport overload retains the prior facade
contract and is not an armed-driver admission API. The typed overload requires
explicit admission through `arm(deadline, maximum_transmissions, now)`. Construction and unarmed operations
do not start the driver, transmit frames or allocate packet counters. Current
session authority, a required injected fresh monotonic clock and the original
deadline guard arm, send, retry, service, receive and ACK handling. Clock samples
are rechecked after driver calls so elapsed startup/RX/TX time cannot retain an
expired session grant. The typed constructor requires a trusted serialized
`bool Clock(void*, uint64_t&)` callback sharing the gate context (or ignoring
it for a system timer). Missing, failed, backward or invalid samples terminate
the instance. Driver time remains
its trusted ESP timer; the injected clock must use that same boot-local
millisecond domain. No remote packet timestamp grants or extends authority.

Only one startup is permitted. Budgets remain one or two TX attempts, and the
window remains at most 60 seconds. Stop is terminal; a new attempt requires a
new separately admitted driver/facade instance. RX maintenance is not permission
to restart a stopped driver or replenish its TX budget. Final-attempt completion
terminates this bounded composition; reserve enough budget for the intended
data/ACK schedule. Driver queue acceptance is neither completed RF nor peer ACK.

Cleanup calls the actual driver stop separately from session closure and Node
shutdown, even when chip shutdown reports failure. Driver queues and Node/ACK
caches are cleared by their existing owners. DeliveryController can retain
opaque pending ciphertext because its existing interface has no cancellation
operation; the stopped facade prevents further servicing or reuse. This is not
a claim of whole-memory erasure. Already transmitted or in-flight RF ciphertext
cannot be retracted. An authority change during a driver call is contained at
the next checked boundary; it cannot undo that call's prior physical effects.
The real driver may briefly rearm RX internally after final TX completion before
the facade observes exhausted budget and stops it.

The runner compiles the unchanged HeltecPairRadioDriver implementation against
existing SDK/RadioLib stubs, runs its existing driver matrix and the expanded
facade matrix, and uses real Packet-v1 cryptography with synthetic session/store
authority. No target firmware is built, no NVS backend is instantiated and no
physical RF, resource, durability or interoperability acceptance is implied.
The affected host matrix passes 863 facade assertions across 37 lifecycle
groups, 318 synthetic transport assertions, and 44 existing driver groups.
Repository documentation checking and 18 documentation tests pass. Independent
security and test reviews cover admission, reentry, late failures, queue
containment, counter/replay ordering and harness fidelity. These counts overlap
and are not additive product qualification.

The later accepted host-only protected BLE command/event ingress composition is
documented in [its maintained profile](../platform/PACKET_V1_PROTECTED_BLE_INGRESS_V1.md).
Its40 cases remain host boundary evidence. This group64 successor reruns the
affected ingress/facade matrix; it supplies no Android runtime, target RF or
durable transport admission. The next missing c/d witness scope requires exact
owner authorization before new instrumentation is implemented.
