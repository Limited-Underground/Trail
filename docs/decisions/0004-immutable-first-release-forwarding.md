# Decision 0004: Immutable First-Release Forwarding Boundary

Status: accepted architecture direction; cryptographic and target implementation
pending, 2026-08-10

## Decision

The initial OpenTrail release will support direct client communication and at
most one authorized repeater per group. The sender-protected packet object is
immutable in transit. A repeater may validate an eligible packet and rebroadcast
the exact same bytes once; it does not decrement a packet TTL, replace the
sender, rewrite fragment fields, or create a new end-to-end tag.

The production forwarding path must:

1. validate the complete protected object and current group epoch;
2. require cryptographic source-authentication evidence appropriate to the
   packet's claimed sender, not a caller-supplied name or Boolean;
3. verify current sender membership/authorization and an immutable forwarding-
   permission field;
4. suppress a previously seen `(epoch, sender, message ID/counter)` before
   queueing;
5. enforce one configured authorized repeater, fixed queue/rate/expiry limits,
   and a client role that never forwards; and
6. enqueue and transmit the exact validated frame bytes.

There is no mutable hop-limit field in the first-release protected object. The
current v0 host forwarding controller's separate decrementing metadata remains
simulation evidence only and cannot be presented as a production authenticated
packet design.

Multi-repeater forwarding is deferred. It requires a separate reviewed outer
forwarding construction or a new packet version, explicit on-path
authentication/authorization and replay rules, an updated byte/airtime budget,
and failure/field evidence. It cannot be enabled by silently reinterpreting a
reserved bit or unauthenticated byte.

## Source authentication is distinct from group access

A symmetric AEAD key shared or derivable by all current group members can
protect against outsiders and prove access to group material. By itself, it
cannot prove which current member created a broadcast: another member able to
derive the same sender key can forge that claim.

Therefore:

- any packet/UI/log that claims an individual sender requires cryptographic
  source authentication beyond possession of common group material;
- the leading broadcast candidate is an Ed25519-style signature or
  countersignature over the protected object, subject to target benchmark and
  wire/airtime acceptance; and
- pairwise DH-derived AEAD remains a comparison for unicast, but pairwise fanout
  is not treated as one group broadcast.

If source-authenticated broadcast is too expensive on the selected target, the
project must change the feature/threat claim or topology explicitly. It must not
silently label group-key possession as individual sender authentication.

## Why

The packet-budget work exposed a contradiction: an end-to-end AEAD tag cannot
remain valid if a repeater changes authenticated routing fields. Established
standards separate end-to-end protected content from fields intermediaries need
to process:

- [OSCORE RFC 8613](https://www.rfc-editor.org/rfc/rfc8613.html) classifies
  encrypted/integrity-protected inner fields separately from outer proxy fields
  and treats inner and outer block-wise fragmentation independently.
- [IP Authentication Header RFC 4302](https://www.rfc-editor.org/rfc/rfc4302.html)
  explicitly excludes mutable fields from normal end-to-end authentication
  processing.
- [COSE Countersignatures RFC 9338](https://www.rfc-editor.org/rfc/rfc9338.html)
  defines abbreviated countersignatures for encrypted group messaging where
  originator identification is required.
- The RFC Editor's [final-review text for Group OSCORE / RFC 10021](https://auth48-transition.rfc-editor.org/authors/rfc10021-diff.html)
  separates signed group mode from DH-derived symmetric pairwise mode. Group
  mode supplies source authentication to multiple recipients; pairwise mode is
  for two members and shorter tags.

OpenTrail is not adopting OSCORE, CoAP, COSE, or IPsec by this decision. These
are primary-source design comparisons showing why one mutable field cannot be
both freely rewritten by an intermediary and end-to-end authenticated without
additional construction.

The user's planned progression already gives a safe product boundary: four
standalone clients first, followed by four clients plus one repeater, then eight
clients plus one repeater. The initial release does not need an unbounded mesh or
multi-repeater TTL to run those tests.

## Candidate cost exposed by the budget model

At the 163-byte example MTU:

- the corrected base 44-byte authenticated header plus 16-byte AEAD tag leaves
  103 bytes after explicitly charging the destination alias;
- adding a 64-byte Ed25519 candidate source signature raises overhead to 124
  bytes and leaves 39 bytes;
- a 16-byte position then occupies 140 bytes with 461,312 us theoretical
  airtime at the bench PHY; and
- an additional 16-byte forwarding wrapper would raise overhead to 140 bytes
  and leave 23 bytes.

The exact signature encoding, whether it is encrypted, target CPU/power cost,
and final transport MTU remain benchmark gates. The 16-byte wrapper is only a
model input for later multi-repeater comparison, not a selected design.

## Consequences and remaining gates

- First-release repeater configuration must fail closed if zero/multiple
  repeater authority is presented to a repeater-role runtime.
- A host coordinator now saves each newly observed eligible replay key before
  releasing its queued frame and restores/repairs the two-slot checkpoint before
  operation. The save-before-transmit order is accepted even though power loss
  can discard a saved-but-not-yet-transmitted RAM frame. Protected target
  persistence, rollback resistance, power-cut/endurance evidence, and any
  durable frame outbox remain field-release gates; receiver replay protection
  remains independently mandatory.
- The two-slot `ODS0/v1` host record now embeds the exact group-context ID and
  epoch, rejects mismatched media without overwrite, and routes legacy unbound
  v0 media to service. This narrows accidental cross-group restore risk but does
  not supply authenticated target storage or anti-rollback protection.
- Exact-byte forwarding does not stop an outsider from replaying RF energy or
  jamming. Validating before forwarding limits amplification but cannot provide
  availability.
- A malicious current member remains able to transmit group traffic; individual
  source claims require the separate source-authentication construction above.
- An algorithm-neutral exact-byte single-repeater host policy now enforces this
  order; its authentication/authorization inputs remain adapter obligations.
- Actual cryptography, direct-radio binding, target timing/resources, protected
  state, reboot/power loss, and four/eight-client field evidence remain open.
