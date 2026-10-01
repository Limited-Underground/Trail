# OT-0264a Repeater prototype evidence and scope

Publication version, 2026-09-29: local workspace paths removed; dated technical
facts and evidence limits are unchanged. Original evidence is retained privately.

Observed 2026-09-23. Planning deliverable for owner review, not relay firmware
implementation, supported hardware or release acceptance.

## In plain English

A Trail Repeater would help pass radio messages onward. Some forwarding and
restart rules already exist as computer-tested components. Older physical tests
used MeshCore radios, so they do not prove finished Trail Repeater firmware.
No equipment or attendance is needed for this review.

## Authority, ownership and release boundary

Approved OT-0264a revision 1 covers the inventory. Existing outcomes
[OT-0264 through OT-0267](../../tasks/BACKLOG.md) own follow-on planning.
Engineering stays in OpenTrail, baseline
`46ca4af65d59377a3c068dfe69ae62182c99cbd9`; no new repository is implied.

[Decision 0033](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md)
supersedes [Decision 0004](../decisions/0004-immutable-first-release-forwarding.md)'s
initial-release repeater promise: current V1 is direct-only. Decision 0004's
immutable-forwarding analysis remains useful for later reviewed work.
[Future Concepts](../FUTURE_CONCEPTS.md) records optional client/repeater mode
as accepted direction, priority post-V1 candidate assessed for V1.5, but
unscheduled and unvalidated. It is not automatically deferred until after V2,
nor automatically added to V1.

## Evidence map

| Existing component or observation | Source / evidence | Layer and limit |
| --- | --- | --- |
| Early forwarding/TTL model | [policy](../protocol/FORWARDING_POLICY_V0.md), [tests](../../tests/host/forwarding_controller_tests.cpp) | Simulation/host policy using separate decrementing metadata. Not a production authenticated packet construction. An intermediary cannot rewrite end-to-end authenticated fields and preserve the same tag. |
| Single-repeater exact-byte policy | [forwarder](../../firmware/components/delivery/include/opentrail/single_repeater_forwarder.hpp), [contract](../protocol/SINGLE_REPEATER_FORWARDING_V0.md), [tests](../../tests/host/single_repeater_forwarder_tests.cpp) | Algorithm-neutral host queue, rate/age, context, source and duplicate checks. Its Boolean authentication/authorization fields are adapter obligations, not actual cryptographic evidence; raw parsed metadata cannot satisfy them in production. |
| Save before transmit | [replay coordinator](../../firmware/components/delivery/include/opentrail/single_repeater_replay_coordinator.hpp), [contract](../protocol/SINGLE_REPEATER_REPLAY_COORDINATOR_V0.md), [tests](../../tests/host/single_repeater_replay_coordinator_tests.cpp) | Host boot/repair and verified checkpoint save before releasing the queued frame. Failed or uncertain persistence stops forwarding. Saved-but-unsent RAM frames can be lost; this is not durable delivery. |
| Concrete storage shape | [checkpoint KV contract](../persistence/DUPLICATE_CHECKPOINT_KV_TARGET_ADAPTER_V0.md) | Backend-neutral exact namespaces/slot semantics. Not proof of protected target storage, physical interruption/endurance or a repeater boot implementation. |
| Secure transport obligations | [Decision 0035](../decisions/0035-host-tested-secure-lora-key-transport-contract.md) | Existing key/identity, epoch, replay and bounded-delivery boundaries. Direct pairwise enrollment/traffic evidence does not automatically authorize a group broadcast relay. The forwarding contract must reconcile the selected current security profile explicitly. |
| Historical physical candidate | [inventory](../../hardware/INVENTORY.md), [OT-009A soak](../../tests/hardware/OT-009A-2026-08-09.md) | Two Heltec companions plus Seeed SenseCAP Solar running MeshCore in a close-bench recovery/soak setup. Useful historical radio/harness evidence, not Trail repeater firmware, selected Trail RF configuration, field range or current connected-device inventory. |
| Combined client/repeater direction | [future register](../FUTURE_CONCEPTS.md) | Owner-accepted direction only. Simultaneous client fairness, relay scheduling, power and actual three-radio forwarding remain unproved. No general support claim for every existing board. |

No accepted deployable Trail Repeater target or combined client/repeater
physical acceptance was established in this source review. Historical physical
counts remain in their reports; they are not relabeled as Trail packet results.
No device or radio was accessed for this inventory.

## Complete flow and successor decisions

The reviewed future flow must be: restore authorized role/context while radio
forwarding is disabled -> authenticate eligible protected object -> admit current
sender/epoch/forward permission -> reject replay and overload -> admit/queue
exact bytes -> persist and readback-verify replay admission -> release for
bounded transmission -> restart/recovery.
Persisting a replay key is not a transmission receipt. Receiving or queueing
is not proof of delivery; a relay must not manufacture an endpoint ACK.

One configured authorized repeater and exact bytes are the historical policy
boundary, not a license for a mesh. More relays require separately reviewed
authentication/loop/airtime rules. Old mutable TTL logic must not be promoted
by convenience. Queue congestion, expiry, clock rollback, state corruption,
uncertain commits, loss of power and role change need explicit refusal outcomes.

- **OT-0264b:** reconcile a dedicated repeater versus optional client/repeater
  mode. The accepted combined-role direction remains available; it is not
  silently replaced with a dedicated-only product.
- **OT-0265a/b:** bind actual cryptographic evidence and secure forwarding
  construction, then finite queues/rates/expiry and fair client scheduling.
- **OT-0266a/b:** select exact hardware, antenna/region/power/enclosure and
  provisioning, diagnostic and durable recovery boundaries. Existing vendor
  hardware names are candidates, not selected supported targets.
- **OT-0267a/b:** define a real three-radio sender/relay/receiver path with
  relay-disabled negative control, packet/loss/duplicate/latency/airtime counts,
  simultaneous client traffic when claimed, restart and measured power evidence.
  Only then can deployment guidance make supported configuration claims.

## Validation and closeout

Read-only source/decision review and relative-link checks validate this map;
repository document checks and independent review precede submission. Historical
host/physical results were reused without rerunning them. No implementation,
hardware purchase, RF operation, V1 progress, public website status, Git
publication or successor authorization changes. This local report awaits owner
review and does not complete the Repeater product.
