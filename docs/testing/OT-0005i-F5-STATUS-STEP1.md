# MeshTrail retained-pair status development candidate

D-113, local host/build work on `feat/f5-msg-step1`, based on main `4b7e5d5`.
This implements a bounded board-only status trial. It does not accept F5/F6,
production security, phone messaging, or live one-board restart recovery.

## Conversation identity

`GROUP_PAIRWISE_V1.md` names outside-group conversations by a fixed ID and both
full fingerprints. The original R2 record already authenticates the group value
and the two complete persistent public identities. `EnrollmentRecoveryOwner::inspect`
reconstructs and verifies that committed provenance. The F5 owner therefore retains
the R2 group value as the fixed conversation ID **only inside** an explicit
`OpenTrail F5 outside conversation v1` domain containing both complete ordered
public keys. The domain digest, ID and full identities enter signed fresh-session
control and the Packet-v1 context. A bare six-digit code is never a global identity,
a trust root, or a group-traffic binding. Existing R2 records are not rewritten.

## Lifecycle and authority

| Before / owner | Trigger | Required result | Failure handling |
| --- | --- | --- | --- |
| Unopened / board app | Link-test, current allowed region, no active first pairing | Probe retained journal and exact identity before radio messaging | An exactly absent pair releases storage for first pairing; uncertain state stays unavailable |
| Retained / F5 owner | Both boards start within the independent 60 s arm | Verify original committed provenance; reserve a durable local incarnation | No prior traffic keys resume; torn/unknown state fails closed |
| Rekey / F5 owner | Signed start and reply | Fresh ephemeral X25519, signed boot/challenges, exact next epoch and conversation domain | Wrong identity, altered records and old transcript controls refuse |
| Preparing / F5 owner | Transcript-bound confirmation and activation controls | Persist PREPARED, ACTIVATION_POSSIBLE, COMMITTED, then verify peer commitment before local packet capability | Interrupted commitments are retained and unavailable, never erased to retry |
| Ready / packet owner | One fixed OTQ0 OK status | Durable isolated DATA/ACK counters and replay records; exact sealed retry bytes | MTU/lifetime/authority checked before send; corrupt input cannot advance replay |
| Queued / board runtime | Exact owned DATA TxDone | Mark transmitted | ACK/rekey completion and CAD drop cannot mark DATA transmitted |
| Received / packet owner | Authenticated DATA and durable replay admission | Deliver one status and generate matching authenticated ACK | Duplicate exact DATA can repeat cached ACK, never delivery |
| Pending / packet owner | Matching authenticated ACK | Mark acknowledged | Missing ACK expires truthfully; no guessed delivery |
| Any live state / app | Region loss, deadline, reset or radio fault | Discard queued traffic, retire keys and prevent beacon fallback | No automatic erasure/reprovisioning or physical retry |

The development carrier is explicitly versioned OTF5/v1: 236-byte signed offers
and 104-byte signed phase controls. It is a new concrete implementation of the
approved fresh-session direction and requires independent security review before
adoption. The original GroupState carrier is coupled to signed rosters and is not
silently repurposed. An exact peer COMMITTED observation is local readiness;
a lost final record can still leave the other board unavailable.

## Storage and target boundary

The existing `ot_pair` namespace inside `ot_state` keeps R2 names 0..10. Name 11
holds the F5 journal at generation zero; names 12..15 isolate DATA TX, ACK TX,
DATA RX and ACK RX at local generations 1..32. Entries from prior generations
are retained, never repinned or cleared. Worst-case budget is 6,204 NVS entries
of 32,256. The 64-slot RAM cache covers the bounded retained/journal/packet
working set. Existing full `ot_state` factory reset includes these records and
retires the registered storage owner first. Old firmware's strict inventory
rejects the new names; rollback to an earlier image does not restore pairing
compatibility. Exhaustion or uncertain state needs a separately reviewed recovery
path; this candidate never resets user state automatically.

Only `OPENTRAIL_LORA_LINK_TEST` links the new runtime. It uses the actual Heltec
radio, receives before servicing retries/launching queued frames, and guards
launch against current region, storage and key authority. First-pairing runtime
files, coordinator, session adapter and Android are unchanged. New pairing in
the same boot needs an ordinary reboot to transfer the private storage owner.
One autonomous exchange runs per boot; both peers must start together. A single
peer reboot cannot trigger a new session on the still-running peer. Full live
supersession and one-step catch-up remain unimplemented.

Status policy is two DATA attempts, 4 s retry spacing, 30 s expiry; rekey controls
have two accepted sends per phase with a 1 s floor. These are bounded bench
settings requiring radio measurements. Queue drops before launch do not consume
a rekey send. Board-receipt ACK is not delivery to an Android UI.

## Reproduction and review

- `python -X utf8 -B tests/host/f5_rekey_status_ci.py --out build/f5-focused-final --sodium-source tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/managed_components/espressif__libsodium`
- `python -X utf8 -B tools/companion_packet_service_ci.py --out build/f5-companion-ci-2 --sodium-source tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/managed_components/espressif__libsodium`
- `python -B tests/host/heltec_v4_bench_target_tests.py`
- `python -B tools/check_repository_docs.py --base-ref 4b7e5d5` and `python -B tests/host/repository_docs_tests.py`.

The focused suite generates actual `EnrollmentCandidateSession` committed records,
then constructs the new owner from their retained stores. SDK/radio I/O is simulated.
The offline dependency option verifies all 733 files against the accepted pinned
inventory; it does not repeat official ZIP admission or download packages.

Detailed final results, image size/hash, all command records, reviewer checklist,
firmware preflight applicability and the proposed owner-present board procedure
are maintained in the task-local `out/REPORT-2.md`. No board session was run.
No public website status or weighted V1 score changed. GitHub tracking and
publication remain pending by explicit task restriction.
