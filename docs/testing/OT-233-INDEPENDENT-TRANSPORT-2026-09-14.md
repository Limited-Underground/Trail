# OT-233 Independent endpoint transport

## Scope and current status

OT-233 implements a bounded host-only adapter between the actual independent
handshake endpoints and the maintained `radio::RadioTransport` interface. It
reuses the `protocol::experimental_probe` codec. Focused validation passes 39 actual independent-transport groups, and
independent review found no blockers. All final software gates now pass. The adapter remains host-only and is not
wired into the target.

The [OT-232 USB case](OT-232-PAIR-STARTUP-FRAMING-2026-09-14.md) established
two real-device endpoint handshakes and separate physical local confirmations,
followed by independently verified original restoration. Its accepted image
and evidence remain historical artifacts. OT-233 does not change that physical
acceptance into radio acceptance or alter the image that was actually tested.

## Transport and authority boundary

Each encoded frame is at most 154 bytes; there is no fragmentation. Source,
network and message identifiers are routing selectors only. They do not
authenticate a peer, select a trusted signer, grant membership or replace the
endpoint's authenticated invitation, boot context and independent local clock.

Silent packet loss is bounded through an additive endpoint `poll()` that uses
the existing trusted role, Ready and time guard. Duplicate or out-of-order
frames, malformed input, short sends and transport errors fail closed. The
adapter adds no retry, acknowledgment, membership or application-traffic API.
Local confirmation remains a separate device decision; transport delivery
cannot stand in for it.

The adapter requires an exclusive fresh transport lease. The current transport
interface cannot cancel public handshake frames that are already queued. A
closed endpoint does not prove that those queued frames were drained or erased;
no drain guarantee is claimed. Tests must preserve this interface limitation
while proving endpoint cleanup and suppression of further adapter actions.

The adapter is not wired into a Heltec target in this increment. Host tests
exercise the actual endpoint and codec through the transport interface; they
cannot establish on-air transmission, reception, RF configuration, range,
phone UI behavior or product trust provisioning.

## Validation gates

All 39 focused real-endpoint transport groups pass. An initial trailing-octet
fixture changed the send length and was correctly refused by the send guard;
the fixture was corrected to exercise a declared-payload-length mismatch. This
was a test-fixture correction, not a production defect. The fresh complete matrix passes 896 C++ groups, including the 39 new groups,
26 scalar controls and 47 Python tests, plus actual two-process crypto
interoperability. All 954 source pins were reverified. Both fresh pair-target
regression builds pass, with all seven raw artifact pairs byte-identical. The
[host/build proof](../../tests/benchmarks/crypto/OT-233-INDEPENDENT-TRANSPORT-HOST-2026-09-14.json)
records final evidence. The compile regression is required because
the additive endpoint polling entry point changes a header shared by the
existing pair target. A successful regression build is software evidence only;
the OT-232 physical artifact remains bound to its original source and image.

The regression application is 460,832 bytes, SHA-256
`53821f58fec7f8bc917310cdf7669cb887d772b2d98717cbd1b25909968299f6`.
It is not the image physically tested in OT-232. The maintained toolchain is
ESP-IDF 6.0.2, Xtensa 15.2.0, CMake 4.0.3 and Ninja 1.12.1; the project version
remains `ot232-pair-sync-v1`. This build still has no new adapter target wiring.
The 291-entry raw-byte check passes, all four affected text attributes are
verified unset, and the diff check passes.

## Applicable porting checklist

Apply the [mandatory porting lessons](../firmware-porting-lessons.md) to the
affected boundaries without inventing hardware scope.

| Checklist | Applicability and gate |
| --- | --- |
| Real target boundary | Host-only adapter; no target wiring in OT-233. Existing pair-target pins, flash profile, partitions and version are unchanged. Physical radio and UI checks are skipped because those paths are not wired or exercised. |
| Reproducible bytes/builds | Bind the new source and host evidence; compile the existing pair target after the shared-header addition. Both fresh regression builds pass with seven raw artifact pairs equal; source/byte admission passes. Preserve OT-232's accepted image and hashes. |
| Transport lifecycle | Exercise actual framing, bounded sizes and exclusive fresh-lease requirements. No USB reset or serial handle lifecycle is changed. Hardware endpoint tests are outside this increment. |
| Ownership and event order | Prove loss/expiry, duplicate/order, malformed input, short sends and errors against the actual adapter and endpoint; keep local confirmation separate. 39 focused groups and the final affected matrix pass. |
| Persistence and cleanup | Existing durable authority remains owned by the endpoint. Verify fail-closed cleanup; do not claim cancellation of already queued public frames. |
| Composed target validation | Focused actual host composition and the final affected matrix pass. Target adapter wiring is deferred; compile regression covers the shared endpoint header only. |
| Hardware authority | No flashing, RF transmission, phone actions or physical trial is authorized by host implementation. A later target-wiring increment requires build/proposal gates and fresh bounded physical-radio authorization. |

## Next gate

With host acceptance complete, wire this adapter into the actual target while preserving
endpoint authority and cleanup. Build and review an exact bounded proposal
before seeking physical radio authorization. That later acceptance must measure
actual two-node exchange; the current host result cannot substitute for it.
No V1 completion or public website status changes in OT-233.
