# OT-0238b retained identity binding - host candidate

Date: 2026-09-23. Local uncommitted candidate; OT-0238b remains in progress.

The owner accepted the [product design](../security/PRODUCT_ENROLLMENT_REKEY_DRAFT_V1.md).
This increment implements its cryptographic identity-to-session binding boundary,
not the complete product enrollment coordinator or a selected production algorithm.

## Behavior

`enrollment_identity_binding.hpp` verifies real detached signatures using the
existing admitted candidate library. Both locally supplied retained identities sign
the domain-separated ordered identity pair and complete canonical invitation.
The invitation issuer must be the pinned initiator. Packet contents cannot select
replacement roots. Group, epoch, fresh session contributions, nonce, both boot
contexts and both time windows are signed. Noncanonical encodings are refused.

First enrollment is epoch 1. A subsequent proof requires the prior opaque verified
binding and exactly epoch +1, refusing immediate-predecessor keys and nonce.
The result is assigned only after every check passes. A verified signature object
is explicitly NOT current durable membership authority or permission to transmit.

## Review and validation

Independent review identified that a third issuer was initially allowed. The final
code constrains issuer to the retained initiator. Its discriminating test supplies
valid signatures from both ORIGINAL retained identities and a valid third-issuer
invitation. Removing only the issuer-equality predicate in a private shadow copy
causes assertion failure (exit -1073740791); the worktree source was untouched.

The focused real-library suite passes, covering all 188 payload-byte mutations,
192 signature-byte mutations, swapped roles, invalid pins/group, replacement
identities, epoch skip, prior key/nonce reuse and unchanged output after rejection.
Commands: `.private/ot0238b-identity-binding/commands.json` in this checkout.
The suite is registered in `tests/host/security_current_source_ci.py`.
Final complete actual-source matrix: **45 suites passed**, including the new suite,
with fresh admitted-library compilation and unchanged tested source pins.
Command: `C:/Python314/python.exe -X utf8 -B tests/host/security_current_source_ci.py --output-root C:/lu/OpenTrail/.private/ot177-publication/.private/ot0238b-binding-final-matrix`
with `OPENTRAIL_MSYS2_ROOT=C:/msys64`. Exact commands, source/binary hashes and
outputs are in that directory's `commands.json` and `result.json`.

The initial focused fixture incorrectly regenerated the same identity; its failed
run was corrected before the final matrix. A stale focused execution status was
also corrected by freshly executing and recording both final positive and mutant
binaries in `.private/ot0238b-identity-binding/result.json`.

After the matrix, only the two added runner lines changed CRLF to LF to satisfy
`git diff --check`; Python AST equivalence was verified and hashes recorded in
`.private/ot0238b-runner-whitespace.json`. Tested C++ inputs did not change.
Repository documentation checker and 18 documentation regressions pass.

## Remaining acceptance boundary

No firmware target includes this new header yet, so there is no affected deployable
target build or physical claim. Trusted fingerprint display/input receipt production,
pre-invitation possession proof, current clock/context checks, durable invitation
consumption, pending/activation coordinator and restart-state comparison still need
implementation and composition tests. Copying or replaying the proof object must
never bypass those gates. Immediate-predecessor refusal is not durable full-history
reuse or rollback protection. SDK/entropy/storage seams remain host simulations.

Next implementation: bind the trusted local identity receipt and durable membership
owner to the verifier; require owned durable state for authenticated recovery
comparison, with pending/mixed/uncertain states remaining RECONCILE and no traffic.
Automatic repair is not implemented. No hardware, Git publication, V1 credit or
public website status change occurred.
