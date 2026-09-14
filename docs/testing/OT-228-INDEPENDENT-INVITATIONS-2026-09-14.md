# OT-228 Independent boot and clock invitation contract

## Accepted host result

The additive version-2 invitation authenticates each role's independent boot context and local expiry window. Both roles hash the same signed bytes into the Noise prologue. Durable role admission consumes an attempt before validating its signature and local window. The real three-message Noise exchange is tested using different boot generations and different clock values.

This implements the contract and durable admission layer. The frozen OT-227 endpoint still consumes version 1; binding a successor endpoint to version 2 is the next implementation step. No signed v2-to-v1 conversion or clock/boot override is used.

## Wire and trust contract

The canonical payload is 188 bytes, followed by a 64-byte detached Ed25519 signature. Integer fields use big-endian encoding. The magic is `OTEINV` followed by zero and version byte 2.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 8 | Magic and version |
| 8 | 8 | Group |
| 16 | 4 | Epoch |
| 20 | 32 | Signer public key |
| 52 | 32 | Initiator public key |
| 84 | 32 | Responder public key |
| 116 | 16 | Invitation nonce |
| 132 | 16 | Initiator boot context |
| 148 | 8 | Initiator local issued time in milliseconds |
| 156 | 4 | Initiator window in milliseconds |
| 160 | 16 | Responder boot context |
| 176 | 8 | Responder local issued time in milliseconds |
| 184 | 4 | Responder window in milliseconds |
| 188 | 64 | Signature over all payload bytes |

Each window is 1–60000 milliseconds and its addition to issued time must not overflow. Both windows must be structurally valid; only the selected role's window is compared with that device's trusted monotonic clock. Issued time is inclusive; deadline is exclusive. Clock rollback burns the gate. Every role verifies the pinned signer and both independently trusted peer pins. Received bytes cannot establish trusted pins, local role, clock or boot authority.

The prologue is SHA-256 of all 252 signed bytes, including both roles' boot/time bindings. The trusted inviter must obtain authentic current boot and local-clock observations; the codec does not implement that provisioning channel. Boot contexts inherit the existing ledger-scoped generation contract and are not global device identities. Payload size alone does not select a LoRa transport or prove radio acceptance.

## Durable admission

The new role authority uses isolated two-slot storage with version-2 record identity, a domain-separated binding digest, commit-last writes and exact readback of both slots. Version-1 role records refuse; this task does not migrate or erase them. A stable invitation copy precedes storage callbacks. One serialized owner is required; callback reentry fails closed and the latch is not a thread lock.

Invalid invitations and explicit cancellation consume the current boot/role before refusal when persistence succeeds. Failed writes never authorize; an error that leaves no durable bytes cannot prove consumption after power loss. Partial or ambiguous retained records refuse. A current-boot invitation cannot be reused after reconstruction. A strictly newer durable boot can admit a newly signed invitation for that boot, without automatic confirmation. The consuming endpoint must keep checking retained authority and its local clock; `current()` alone is not a clock or confirmation capability.

## Validation and limits

Publication CI's first core job reached its 20-minute limit while tests were still passing and the current-source security matrix had begun. The core job timeout is now 30 minutes; test commands, required checks and the other job limits are unchanged. The canceled run is not a passing CI result; publication requires a successful complete rerun.

The new suite passes 93 behavioral groups. One fresh full affected security matrix passes 729 groups across 11 suites plus 26 scalar controls, using the verified 733-file pinned dependency and fresh scalar objects. The [sanitized proof](../../tests/benchmarks/crypto/OT-228-INDEPENDENT-INVITATION-HOST-2026-09-14.json) records compiler, binaries and changed source hashes. Exact commands and the full source closure are retained privately.

Command: `python -X utf8 -B tests/host/security_current_source_ci.py --output-root <active-worktree>/.private/ot228-independent-invitations/final-matrix-reviewed` using the existing UCRT64 compiler configuration. Development-only compilation reuses prior scalar objects; final acceptance uses the fresh matrix. The initial full matrix passed before two final-review assertions were added for post-write readback failure and reconstruction over a partial record; the complete matrix was then repeated against the final source.

During development, a test call to global crypto initialization could not link against the deliberately bounded scalar fixture. The test retained the existing caller-owned initialization boundary. Draft expectations were corrected to reflect that rejected old invitations consume the current boot/role, and cancellation after consumption does not perform another write. Production checks were not weakened.

Frozen invitation, session, authority, confirmation, endpoint and target sources remain byte-identical. Firmware-porting lessons were reviewed: board, radio region, partition, firmware-image reproducibility, boot/USB, flashing and restoration gates are not applicable to this target-neutral host increment. No deployable target includes the new headers. Storage/entropy simulation does not establish physical interruption or entropy acceptance.

Next: bind the new contract and durable role authority into an independently owned endpoint, then connect actual provisioning, radio and phone flow. Full V1 testing readiness, V1 credit and public website status are unchanged. Implementation is local; commit/push/PR publication remains separately authorized.
