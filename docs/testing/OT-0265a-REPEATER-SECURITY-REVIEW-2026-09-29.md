# OT-0265a Secure forwarding contract review

Prepared 2026-09-29 under approved revision 1. This resolves planning precedence
and proposes review requirements; it does not amend a normative protocol or
select cryptography, keys, hardware or a deployed relay format.

## Review result

The accepted first-role proposal uses one authorized dedicated repeater between
two endpoints, preserving the later combined client/repeater direction. Current
OpenTrail V1 still has no relay. Its direct-only secure traffic cannot simply be
passed to an old forwarding helper and called supported repeater operation.

Future relay work must authenticate the eligible protected object and its claimed
source, verify current membership and explicit forwarding permission, preserve
the protected bytes and suppress replay before bounded transmission. Successful
forwarding is not endpoint delivery. A repeater cannot invent an endpoint ACK.

The [accepted role proposal](OT-0264b-REPEATER-ROLE-PROPOSAL-2026-09-29.md)
establishes the planning baseline. No advance hardware or algorithm choice is
needed to finish this review; those remain later explicit gates.

## Supersession resolved

| Authority | Current meaning | What must not be imported into this work |
| --- | --- | --- |
| [Decision 0004](../decisions/0004-immutable-first-release-forwarding.md) | Preserves useful immutable single-repeater analysis, individual-source authentication, permission, replay and save-before-transmit obligations for future reviewed relay work. | Its initial-release repeater promise and historical four/eight-client sequence do not define today's V1. Its signature candidate is not an algorithm selection. |
| [Decision 0033](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md) | Explicitly supersedes the initial-release relay promise: base V1 is two phones/two Heltecs over protected BLE and authenticated direct LoRa. Relay is optional later work. | No repeater runtime dependency, hardware gate or group-broadcast authority may be added to base V1 through this plan. |
| [Decision 0035](../decisions/0035-host-tested-secure-lora-key-transport-contract.md) and [OTSL0/v0](../security/SECURE_LORA_KEY_TRANSPORT_V0.md) | Current accepted host security contract is exact pairwise-unicast, with no relay/group broadcast; versions, reserved fields, membership/epoch, replay and ACK checks remain mandatory. | Do not reinterpret reserved bits, widen role admission, export pairwise secrets to a relay or infer source-authenticated broadcast from pairwise test success. |
| [Future Concepts](../FUTURE_CONCEPTS.md) and accepted role proposal | Preserve dedicated-first planning and optional combined client/repeater capability as a separate future path. | Accepted direction does not validate simultaneous client fairness or authorize unscheduled implementation. |

Therefore there is no conflict to solve by weakening the current parser:
OTSL0/v0 remains direct-only. Any production relay needs a separately reviewed,
versioned relay-capable construction and compatibility decision. This review
records that requirement; it creates no wire identifier and changes no normative
file. Historical tests remain evidence of their original host policy only.

## Proposed admission boundary

| Input/evidence | Required check before granting relay authority | Refusal |
| --- | --- | --- |
| Local role | Exactly the accepted relay role and one configured authorized repeater for the selected context. | Disabled, absent, multiple or ambiguous role authority; combined mode without its separate policy. |
| Protected object | Bound version/type/length and exact authenticated byte span under a reviewed adapter. | Unknown format, malformed lengths, unsupported frame class or unverifiable construction. |
| Claimed source | Cryptographic source authentication appropriate to the sender claim. | Parsed names/aliases, a caller Boolean, radio metadata or common group-key access alone. |
| Membership/context | Current source membership, exact group/context and current epoch; role change retires old work. | Wrong/revoked member, stale epoch, mismatched context or unresolved rekey. |
| Forward permission | Immutable authenticated permission for this object under the selected relay construction. | An unsigned flag, nonzero reserved bit or mutable TTL cannot grant permission. |
| Replay/duplicate | Admitted identity/context/epoch/message binding, with exact-byte conflict detection where required by the eventual construction. | Replay, reflection, conflicting duplicate, expired admission or uncertain durable state. |
| Scheduling | Finite queue/rate/age/airtime constraints, with deliberate overload behavior. | Queue/rate refusal is not a promise to forward later; no unbounded flood or hidden retry. |

Possession of a group symmetric key does not prove which group member originated
a broadcast. The relay's ability to authenticate and its ability to decrypt are
separate decisions. Recommend that forwarding require no application-plaintext
access, but the eventual reviewed construction must demonstrate how it supplies
source/membership/permission evidence. This recommendation does not select a
signature, wrapper, encryption scheme or key-distribution protocol.

Preserve the sender-protected object byte-for-byte. If a future construction needs
an outer authenticated relay envelope, its distinct authentication, replay,
overhead and endpoint handling require explicit review; it cannot rewrite the
inner protected object or be hidden in reserved fields. No envelope is selected
by this task. Exact budgets and selected algorithms remain their existing gates.

## One complete forwarding operation

| Transition / owner | Required effect | Failure and recovery |
| --- | --- | --- |
| Boot / role and persistence owner | Keep forwarding disabled until exact role/context/epoch and retained replay state verify. | Invalid-only, mismatched or ambiguous storage requires service; empty state is not first-provisioning authority. |
| Receive / secure adapter | Parse bounded input and authenticate before creating trusted evidence. | Reject without granting permission or poisoning another sender's replay identity. |
| Eligible object / replay owner | Apply membership, permission, duplicate and monotonic-time checks before bounded queue admission. | Old epoch/replay/reflection is rejected; clock regression cannot extend age or revive a frame. |
| Admitted key / persistence coordinator | Save and readback-verify replay admission before releasing exact bytes. | Failed or uncertain save disables release, including work already in RAM. |
| Release / radio owner | Submit only current eligible exact bytes within original finite budget; preserve real receive/rearm ownership. | Queue acceptance is not transmit completion. Disconnect/timeout is not proof no RF emission occurred; no automatic fresh attempt. |
| Receiver / endpoint owner | Independently authenticate, suppress replay and admit the message; only its exact protected ACK can prove endpoint admission. | Repeater receipt/transmit cannot acknowledge on its behalf or imply phone display/human read. |
| Disable/rekey / authority owner | Stop admission, retire queued ownership and old context, preserve replay safety. | Stale callbacks or old-role frames cannot restart forwarding. |
| Restart / recovery owner | Reconcile verified replay state before re-enabling the explicitly authorized role. | Saved-but-unsent work can be lost; do not roll back replay protection to recover delivery. |

The [host forwarder](../protocol/SINGLE_REPEATER_FORWARDING_V0.md) accepts verified
metadata whose truth is an external adapter obligation. The
[replay coordinator](../protocol/SINGLE_REPEATER_REPLAY_COORDINATOR_V0.md) persists
new observations even when queue/rate pressure drops them. Its at-most-once
opportunity tradeoff intentionally permits a lost saved-but-unsent frame. Keep
that behavior explicit; a durable outbox would be separate reviewed work.
CRC-protected host checkpoint structure is not authenticated target storage or
proof of rollback resistance. No stronger threat claim is granted here.

## Negative cases and remaining gates

OT-0265b should specify bounded scheduling tests for authenticated duplicates,
conflicting bytes, reflections, stale epochs, wrong membership, overload,
queue expiry, clock rollback and relay disappearance. Include exact-byte
comparison and assert that no unsupported direct-only frame becomes forwardable
through a Boolean or reserved field. Host fakes must not silently deliver queued
frames or retain radio receive readiness after an operation that consumes it.

OT-0266a supplies candidate board/region/antenna/power/recovery evidence.
OT-0266b maps actual cryptographic producer, role and storage owners and freezes
implementation successors only after the versioned construction is accepted.
OT-0267a retains real sender/repeater/receiver and relay-disabled controls with
packet/loss/duplicate/latency/airtime accounting; combined mode adds concurrent
client evidence. OT-0267b may make support claims only from those accepted results.
See the existing [task plan](../../tasks/OPTIONAL_PRODUCTS_PLAN.md).

This review completes the planning precedence and refusal analysis. It does not
claim cryptographic, firmware, target, physical or field validation. Document/link
checks and independent review are recorded at batch closeout. No new task IDs,
normative changes, security selection, V1 credit, hardware or publication occurred;
public website capability status is unchanged.
