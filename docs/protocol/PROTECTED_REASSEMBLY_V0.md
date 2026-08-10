# Verified Protected Reassembly v0

Status: algorithm-neutral bounded host policy, not cryptographic or packet-v1
evidence, 2026-08-10

## Boundary

`ProtectedReassembler` accepts only adapter-produced
`VerifiedProtectedFragment` values. A future crypto adapter may create one only
after authenticating the protected header, source claim, group/epoch, fragment
metadata, and plaintext. The reassembler never parses raw radio bytes, decrypts,
verifies signatures, or turns caller-supplied names/flags into trust.

The component releases plaintext to the application only after every fragment
for one exact `(sender alias, message ID)` is present and consistent. Therefore
an `OGA0` alert cannot reach semantic ingress and an `OGK0` ACK cannot complete
delivery from a partial message.

## Fixed bounds

- four concurrent reassembly sessions;
- 16 fragments per message;
- 103 plaintext bytes per fragment;
- 1,648 plaintext bytes per complete message; and
- one nonzero policy timeout supplied by target composition.

These are explicit first host bounds, not measured ESP32 resource acceptance.
The signed-group candidate currently carries only 39 plaintext bytes per
fragment; the larger 103-byte storage ceiling preserves the base protected
profile without dynamic allocation.

## Processing rules

Before allocating a session, the policy rejects zero/invalid fields, null or
oversized plaintext, wrong group context or epoch, invalid fragment index/count,
and monotonic-time rollback.

Fragments may arrive out of order. An exact repeated fragment is idempotent and
does not extend the timeout. A changed byte at an already received index or a
changed fragment count for the same sender/message is a conflict and clears the
whole session. Full capacity rejects a new message without evicting active work.
At `age >= timeout`, the session expires and its capacity becomes reusable.

On completion, fragments are concatenated strictly by index, metadata is
returned with the message, and the session is erased. No partial plaintext is
returned in incomplete, duplicate, rejected, conflict, capacity, or timeout
results.

## Host evidence

Ten deterministic scenario groups cover single-frame completion, the real
39+25-byte two-fragment alert shape in reverse arrival order, exact duplicates,
changed-byte and changed-count conflicts, invalid/wrong-context refusal, four-
session capacity, exact timeout, clock rollback, and maximum 16x103-byte
reassembly.

## Remaining gates

This policy does not settle the packet header offsets, per-fragment versus whole-
message signature construction, AEAD, nonce/counter allocation, receiver replay
checkpoint, radio retry policy, persistent partial messages, target memory/task
behavior, power interruption, or field/regulatory performance. A production
crypto adapter must make `VerifiedProtectedFragment` unforgeable at the
composition boundary rather than exposing it to raw packet callers.
