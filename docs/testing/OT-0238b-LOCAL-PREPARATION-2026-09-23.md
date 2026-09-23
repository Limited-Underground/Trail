# OT-0238b local review and durable preparation - host checkpoint

Date: 2026-09-23. Product enrollment remains in progress under the
[accepted design](../security/PRODUCT_ENROLLMENT_REKEY_DRAFT_V1.md).
This extends the [signature-binding component](OT-0238b-IDENTITY-BINDING-2026-09-23.md).

## Implemented boundaries

`EnrollmentFingerprintReview` owns the candidate local review lifecycle. It renders
all64 hexadecimal identity digits with a fixed candidate domain and ordered role,
requires a released-button baseline on the peer page, then a fresh1000Ã¢â‚¬â€œ3000ms
hold/release. Page changes clear confirmation. Context, monotonic time, display
revision and the separate120-second preparation window are checked at transitions.
Binding consumes the attempt before signature work; it requires both real identity
signatures and rechecks context/display/clock after delegated work. Local invitation
boot/time must match and issuance cannot precede confirmation. No packet-level
confirm Boolean is accepted.

The device port remains simulated in host tests. Its target-owned GPIO/display
adapter, provisioned local identity and pre-invitation possession exchange are NOT
implemented by this component. It is not installed or connected to product commands.
The candidate full-key/domain rendering does not select final fingerprint encoding.

`EnrollmentCommitCoordinator` is deliberately a durable uncertainty journal only.
It owns a distinct five-domain/two-slot OTEC record. Preparation binds both retained
identities, group, epoch, generation, operation and signed-evidence digest; exact
readback produces an opaque receipt. A second commit-last transition records
activation intent BEFORE its caller may send activation. Interrupted records are
unavailable or reconciliation-only on reconstruction. Public persisted state cannot
reconstruct a traffic key or claim peer commit. It exposes no completed-membership
or traffic API, and generation allocation remains an upstream requirement.

| Transition | Required result | Rejection/recovery |
|---|---|---|
| Begin and render own/peer identity | Exact complete frame and display revision | Failed rendering/context/clock ends attempt |
| Peer display and fresh gesture | Internal confirmation bound to same review | Held-at-entry, short/long hold or page change gives no confirmation |
| Bind signed invitation | Consume attempt; verify roots, bytes and local time; recheck after callback | No output on cancellation, reentry, expiry or changed context |
| Empty journal to prepared | Exact committed readback before receipt | No receipt after uncertain write; do not erase/retry to grant authority |
| Prepared to activation intent | Exact receipt and current store; commit/readback intent | Stale owner or storage fault refuses; restart reconciles |
| Restart or mixed/partial record | Public uncertainty only | No restored traffic, guessed commit or automatic repair |

## Validation and review

Focused real-library tests pass33 fingerprint review groups and991 journal groups.
Actual components execute against simulated device/storage seams. Journal injection
covers read/write/sync/erase failures before and after mutation, every committed
byte corruption, stale owners and reentry at final write/sync/readback. Fingerprint
cases cover both roles/full rendering, held buttons, clock boundaries, display and
request changes, cancellation, invalid signatures, consumed attempts and late
callbacks. Independent review caught a final-sample reentry leak; requiring retained
confirmation before publishing output and its regression correct it.

Focused commands and outputs are under `.private/ot0238b-fingerprint` and
`.private/ot0238b-journal` in the active checkout. Final actual-source matrix: **47 suites passed**, using fresh real candidate crypto
objects, GCC16.1.0 and unchanged source pins. Command:
`C:/Python314/python.exe -X utf8 -B tests/host/security_current_source_ci.py --output-root C:/lu/OpenTrail/.private/ot177-publication/.private/ot0238b-preparation-final-matrix`
with `OPENTRAIL_MSYS2_ROOT=C:/msys64`. Its result/commands/source and binary pins
are retained in that private directory. Documentation checker,18 document
regressions, publication-safety scan and diff check pass.

A private shadow-header build removing ONLY the final retained-confirmation check
fails the reentry regression (Windows assertion exit3221226505); actual worktree
source remains unchanged. Exact commands/output:
`.private/ot0238b-fingerprint/reentry-negative.json` and accompanying log.
No deployed target includes the new headers, so no firmware binary changed in this
increment. Prior radio fixes retain their separately recorded builds/trial evidence.

## Outstanding integration

Implement the actual trusted input/display producer and persistent identity owner;
prove fresh pre-invitation possession; connect the durable session allocator and
invitation consumption; implement authenticated peer commit and full durable
membership coordinator. Recovery comparison must originate from owned durable
state and uncertain records must remain blocked. Then run the complete composed
first-enrollment/rekey/revoke/restart failure matrix. These components alone do not
satisfy OT-0238b acceptance and earn no V1 progress credit.

No hardware, battery/case work or public website update occurred. Publication of
this checkpoint does not promote it to production readiness.

## Publication hygiene

Final index validation found trailing whitespace/extra EOF blank lines in the
identity header/test, replay Python file and remaining-work plan. Only those bytes
were removed after the matrix; all non-whitespace bytes are identical. Exact
before/after hashes are in `.private/ot0238b-staging-whitespace.json`. No tested
behavior was changed. The staged full-PR content scan covers136 paths with zero
findings; private runtime/captures and generated binaries are excluded.
