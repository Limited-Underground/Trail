# Decision 0019: Retain the exact installed application privately for recovery

## Status

Accepted on 2026-08-17 for one bounded read-only capture only.

## Decision

Retain the exact 470,928-byte OT-064 factory application read from
`OT-DEV-001` as a private, Git-ignored recovery artifact. Admission requires
SHA-256 `A7D8E672CF9169F1D1D4E86EEFF80399C47A145E7D64904C207DD5F1B23F359B`
after an independent post-close reread. The mismatched source reconstruction
remains rejected and is not a substitute for the installed bytes.

The public recovery manifest records only the accepted size, digest, fixed
result category, and sanitized evidence reference. It does not publish the
binary, a local path, port, device identifier, or private operation identity.

## Boundaries

- The one-use reader is deleted after the single authorized attempt.
- No persistent flash/eFuse write, erase, reset, RAM stub, key operation, or
  partition transition is part of the capture.
- The private artifact grants no restore or write authority.
- Recovery remains denied until an exact route is accepted and a later
  operation binds fresh installed-table, source-region, and recovery evidence.
- Protected key roles, an independent rollback floor, exact-unit recovery
  validation, and separate physical-write authority remain open.

## Next gate

Define and validate the exact recovery route without performing a write. Only
after that gate, protected-key and rollback-floor decisions, and fresh unified
evidence may a separate operation request physical partition-write authority.
