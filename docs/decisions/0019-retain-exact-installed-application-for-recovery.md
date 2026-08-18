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
- Decision 0020 accepts the exact route offline, but physical recovery remains
  denied until redundant private custody, fresh unified evidence, and separate
  physical authority are accepted.
- The protected-key role and rollback-floor requirements are defined, while
  concrete providers, provisioning, exact-unit validation, and physical-write
  authority remain open.

## Next gate

Select and review concrete providers for both protected-key roles and the
independent rollback floor. Then prove redundant private application custody
and collect fresh unified evidence before a separately authorized physical
candidate-transition/recovery rehearsal may be requested.
