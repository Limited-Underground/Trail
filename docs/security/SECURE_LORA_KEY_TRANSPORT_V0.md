# Secure-LoRa Key Provisioning and Transport Contract v0

Status: host-frozen algorithm-neutral contract; not implemented, 2026-08-19

Schema: `OTSL0/v0`

Plan ID: `OT-091-SECURE-LORA-KEY-TRANSPORT-CONTRACT-V0`

Work item: OT-091

Authority: Decision 0035 under Decision 0033

## Purpose and boundary

This contract freezes the V1 two-node key-lifecycle and protected-transport
semantics before implementation. It composes existing host-tested identity,
group/epoch, traffic-context, durable-counter, nonce, duplicate, delivery, and
radio boundaries without treating any of them as cryptographic implementation
evidence.

Decision 0003 remains controlling. The exact crypto suite, library, handshake,
KDF, packet-v1 bytes, MTU/PHY, target storage, and production timing are not
selected. The only accepted result is `CONTRACT-FROZEN-HOST-ONLY`; packet v1
and secure physical radio operation remain unavailable.

## V1 conversation boundary

- exactly two current Heltec members;
- exact pairwise authenticated unicast in each direction;
- one explicit destination per protected object;
- no V1 broadcast, relay, server, internet, or V1.5 authority;
- BLE ownership and LoRa cryptographic membership remain independent; and
- phones never receive LoRa identity-private, conversation, traffic, nonce, or
  counter-domain secrets.

An authorized phone may request or present the workflow. It cannot supply
authentication evidence, select a peer by display name/alias, provision raw
keys, or implicitly rotate radio membership when phone ownership changes.

## Authority sources and key separation

Only a future typed result from the selected audited cryptographic adapter may
establish authentication. Caller Booleans, host proof tokens, names, aliases,
QR parsing, radio receipt, BLE bond state, or phone authorization are not
cryptographic proof.

Host reference evidence is admitted only through exact built-in types and
known categorical values. A subclass or otherwise noncanonical value cannot
manufacture authority even when it compares equal to an accepted category.

Distinct purpose domains are required for device identity signing, device key
agreement, ephemeral handshake contributions, pairwise conversation roots,
directional data protection, directional acknowledgements, nonce prefixes, and
counter-domain identifiers. The derivation chain binds:

1. contract and protocol version;
2. nonzero group ID and epoch;
3. both full authoritative device fingerprints;
4. ordered sender-to-destination direction;
5. exact output purpose; and
6. the authenticated provisioning/rekey transcript.

The existing `OTKD/v1` encoder remains a required inner host boundary where
applicable, but its group/epoch/sender/purpose fields do not alone bind the
destination. Pairwise composition must establish that exact parent binding or
use a later reviewed context revision. Names and short aliases are never
substitutes for full fingerprints.

## Initial provisioning

One authenticated invitation contains no long-lived group or traffic secret.
It binds its version, exact group and epoch, inviter full identity and key-
agreement binding, requested candidate role, fresh nonce, and bounded deadline.
It is single-use, admits one candidate and one attempt, and expires
at `now >= deadline`. Invitation open, handshake, and confirmation each require
an exact, nondecreasing clock still strictly before the original deadline. An
invalid clock, rollback, or expiry burns the invitation and cannot be
resurrected by a later earlier timestamp. Restart closes boot-local invitation
state.

The candidate exchange must mutually authenticate the exact identities and
bind the complete invitation, roles, epoch, ephemeral contributions, and
transcript. Both devices must present the same transcript-derived human
confirmation and receive explicit local confirmation. Candidate material is
not released before those gates.

Candidate state is staged privately and becomes usable only after exact durable
commit/readback and exact peer activation. A known failure before mutation
retains the coherent prior state. Possible mutation or uncertain activation
publishes no traffic authority and requires reconciliation. Authentication
failure, wrong binding, expiry, replay, cancellation, confirmation rejection,
disconnect, or restart consumes the attempt and clears volatile material.

When an exact valid invitation sequence accompanies malformed, noncanonical,
busy-state, or re-entrant evidence, that sequence is burned before the evidence
is rejected.

Replaying the same sequence while its invitation is active burns and closes the
active attempt. A different valid sequence presented while busy is consumed
without opening a second invitation; its exact clock observation still advances
the active attempt's monotonic time and is checked against the original
deadline. Clock rollback, invalidity, or expiry during that re-entry burns the
active attempt.
Premature confirmation, repeated/out-of-phase handshake, and any other wrong-
phase invitation action also burn and close the active attempt; they do not
remain available for a later correctly ordered retry.

## Epoch replacement and revocation

Replacement uses an exact nonzero 32-bit epoch and advances by exactly
`current epoch + 1`; zero, reuse, skip, or wrap is denied. It uses fresh
distinct material and admits only retained exact member identities. Revoked or
removed identities cannot authenticate a replacement merely because they
possess old material.

Routine traffic is blocked while replacement is unresolved. Before any
possible activation, a verified no-change abort may restore the exact old
epoch. After possible activation the implementation never guesses or falls
back. Exact new activation immediately denies old-epoch send and receive; no
silent grace period exists. Logical old-key retirement is verified through the
ordinary application storage interface, without claiming physical secure erase
from flash media. Any commit, activation, roster, retirement, or cleanup
ambiguity enters reconciliation with no traffic.

## Outbound protection

Outbound admission requires active current-epoch membership, the exact current
peer/direction, an accepted crypto adapter, coherent key/domain state, and a
durably reserved nonzero counter. The existing `OTCN` store remains the
counter-leasing obligation: it commits a range before use and wastes unused or
uncertain ranges after restart rather than reusing them.

Each valid message ID and counter is assessed independently. When its own
durability evidence is literal `True`, malformed durability evidence or an
invalid identifier for the sibling resource cannot undo its consumption, and
neither can any later context, direction, lease, nonce-binding, or frame-
admission rejection. A later attempt must use fresh values.

Those independent burns occur before ready/pending transport-state rejection,
so an unexpected or re-entrant outbound begin cannot release newly presented
durable resources or upgrade state.

The leading nonce boundary remains the exact host-tested 96-bit composition:
matching nonzero 128-bit lease/key domains, an adapter-supplied 32-bit prefix,
and a nonzero 64-bit counter in network byte order. There is no random-nonce,
zero-domain, mismatch, reset-under-same-key, or counter-reuse fallback. A suite
that cannot honor this construction requires a new reviewed contract version.

The semantic protected header binds version, packet type, flags/reserved
policy, header length, group epoch, sender alias, destination alias, nonzero
message ID, nonzero frame counter, and ciphertext length. Every header bit plus
the implicit group/full-identity/direction context is authenticated, and
application content is encrypted. Exact offsets and algorithm identifiers stay
unselected.

The checked host model's opaque-frame storage ceiling is 255 bytes. That bound
does not select the target radio MTU, packet-v1 size, PHY, or regulatory-safe
payload.

One protected object is sealed once. Every bounded retry transmits the exact
same bytes. Changed plaintext, header, destination, policy, or correlation uses
a fresh message ID and counter; it is never resealed under an old counter.
Packet v0 and plaintext fallback are prohibited.

A report that the radio queue accepted anything other than the exact pending
sealed bytes is ambiguous and enters reconciliation with no traffic. The same
applies to queue acceptance reported with noncanonical byte evidence, without a
pending object, or after the finite attempt limit. A definite queue rejection
with no accepted bytes remains a bounded failed attempt.

## Inbound admission, replay, and duplicates

Inbound processing is ordered:

1. bounded structural/version/type/length checks;
2. wrong-group, wrong-epoch, and wrong-destination rejection using only a
   candidate current context and without authority publication;
3. complete cryptographic authentication, integrity verification, and exact
   source binding into private scratch storage;
4. replay or byte-identical duplicate classification;
5. durable cryptographic replay-state admission;
6. plaintext release exactly once; and
7. creation of only the protected, bound acknowledgement.

No failure before step 6 releases plaintext, and no failure before step 7
creates a positive acknowledgement. Failed authentication, corruption, wrong
network/epoch/identity/destination, replay, malformed input, and packet v0 do
not poison replay state.

Cryptographic replay protection requires a durable current-key/direction/epoch
counter high-water/sliding-window record. The existing time-based
`DuplicateWindow` and `ODS0` checkpoint remain application duplicate evidence;
they cannot replace counter replay protection. Missing, corrupt, wrong-bound,
rolled-back, or uncertain replay state blocks receive under the same key and
cannot be reset without rekey.

A byte-identical authenticated retry is not delivered twice and may receive a
new protected acknowledgement. Reusing a message ID with different
authenticated bytes/counter, or a counter with different content, is a
conflict and is denied.

## Acknowledgement and bounded delivery

An acknowledgement must itself pass authenticated and encrypted protection,
reverse direction, current group/epoch, replay, destination, and exact pending-
message correlation. It is accepted only after at least one radio send and
before the delivery deadline. A rejected transport attempt creates no
acknowledgement eligibility; the exact sealed frame must first be accepted into
the radio queue. Plaintext, forged, wrong-peer, wrong-epoch, early, stale,
unknown, late, or conflicting acknowledgement input cannot complete delivery.

The existing delivery controller's caller-facing acknowledgement method is
therefore usable only behind the protected-ACK admission boundary. A positive
LoRa acknowledgement means `peer_device_durably_admitted`; it is not a user-
read receipt or proof that the destination phone displayed the message.

Radio send acceptance means locally queued. Retry/failure is finite, a late
acknowledgement cannot complete delivery, and deployed attempt counts and
intervals remain blocked on direct-radio/regulatory measurement. Restart with
an unresolved pending delivery reports an interrupted/unknown outcome, performs
no automatic reseal or retry, and never reuses its durably consumed message ID
or counter.

An exact expired acknowledgement deadline is terminal even when sibling ACK
evidence is malformed: it closes the pending delivery as a bounded failure,
cannot be resurrected by a later ACK, and does not release consumed values.

## Restart and failure behavior

Boot begins in reconciliation with radio traffic disabled. Operation requires
one exact coherent active group/epoch/roster/key state, matching derivation and
counter domains, and usable outbound-counter and inbound-replay records.
Transient invitations, confirmations, handshakes, plaintext scratch, and live
delivery authority never resume across restart.

Direct-transport restore cannot move the remembered epoch backward or silently
forward. A forward move is admitted only as the exact next epoch with explicit
verified rekey evidence; any other epoch transition enters reconciliation.

Unreadable, conflicting, future-version, wrong-bound, partially committed,
rolled-back, or otherwise ambiguous key/counter/replay state publishes no
traffic authority. Entropy failure, monotonic-clock rollback, exhaustion,
unexpected re-entry, and adapter failure also fail closed.

Restoring an inactive lifecycle requires exact group absence, epoch zero, and
exact absence/coherence proofs for state, peer activation, traffic context,
counter lease, and replay checkpoint. An out-of-order categorical result that
claims a verified mutation/commit forces reconciliation; an out-of-order known-
no-change result cannot upgrade authority.

Factory reset, reflashing, invasive physical access, or old-image restoration
may reset or roll back membership and cryptographic state. V1 makes no secure-
element, physical secure-erasure, or rollback-proof claim against that attacker.

## Privacy and evidence

Ordinary logs contain categorical states and aggregate counters only. Public
evidence excludes keys, secrets, private fingerprints, aliases, group/epoch
values, invitation values, human confirmation values, message IDs, counters,
packet bytes, plaintext/ciphertext, tags, correlations, storage contents,
addresses, device-specific identifiers, and local paths.

Validator and command-line usage failures emit fixed categorical diagnostics;
they do not echo hostile argument text, supplied values, or local paths.
Direct validation rejects cyclic or excessively nested containers, and the
file loader performs only one bounded maximum-plus-one read before rejecting an
oversize contract.

## Host evidence and remaining gates

The focused machine validation result and canonical contract digest are
recorded in [OT-091 evidence](../../tests/hardware/OT-091-2026-08-19.md).
This validates only exact contract shape, state ordering, fail-closed outcomes,
privacy, and absence of execution authority.

The next gate is the exact OT-005 target benchmark and suite/wire selection.
Later separately authorized work must still implement and physically accept the
selected crypto adapter, entropy, invitation/handshake, target key/counter/
replay storage, packet codec, direct radio, protected acknowledgements,
delivery, Android flow, and coherent two-pair V1 path.
