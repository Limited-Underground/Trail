# Decision 0002: Separate Identity, Alias, Name, and Membership

Status: accepted architecture boundary; cryptographic mechanisms pending,
2026-08-08

## Decision

OpenTrail treats long-term device identity, authoritative identity fingerprint,
group-scoped network alias, display name, group membership, group epoch, and
administrator authority as separate concepts.

- Renaming changes only presentation.
- A short alias collision never merges identities; full fingerprints resolve it.
- Joining and activation are explicit states, not side effects of hearing a
  packet or matching a name.
- Revocation advances the group epoch and stops group traffic; it cannot erase
  old secrets from a lost or malicious member.
- Configuration reset preserves device identity but clears membership and user
  configuration.
- Factory reset erases identity and membership, requiring new provisioning or an
  explicit authenticated recovery import.

## Why

Conflating human names, routing identifiers, cryptographic identity, and group
authorization creates unsafe rename behavior, collision bugs, accidental trust,
and unrecoverable lost-node scenarios. These boundaries let routing remain
compact while retaining an authoritative identity for verification and
collision resolution.

## Consequences

- Packet v0's ephemeral 32-bit IDs remain test-only and cannot become packet v1
  identities by default.
- The eventual join design must mutually authenticate node and group before
  releasing current traffic keys.
- Persistence must distinguish identity secrets, group secrets/epochs, and
  ordinary configuration.
- UI text must make configuration reset, identity rotation, leave, revoke, and
  recovery visibly different operations.
- Exact algorithms, alias derivation, administrator model, and recovery quorum
  remain open OT-005 gates.
