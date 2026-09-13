# OT-206 Durable invitation lifecycle

2026-09-12. Software implementation and actual-crypto host integration passed.
Target boot/storage wiring and physical acceptance remain pending. The prior
OT-203/204 candidate, receipt path and accepted restoration evidence are unchanged.

## Behavior

A durable boot owner reserves a generation using the existing counter allocator.
Each local role has its own isolated consumption ledger. A signed invitation can
be attempted once per role/generation, including cancellation, invalid signatures
and expiry. A reconstructed object cannot recover spent authority; a later boot
has a different context and refuses an older invitation. Legitimate initiator and
responder use is preserved. No caller-supplied freshness Boolean or ledger reset
is available.

The session wrapper generates its real identity before binding trusted peer pins.
It commits and verifies invitation consumption before beginning Noise. Every
handshake/message operation checks that the boot and role ledgers remain current.
Cancellation or stale/uncertain authority wipes the core; stale authority does not
write session state. Valid active cancellation also retires RX state.

Only authorization ledgers mutate before admission. Refusal does not activate
traffic or modify TX/RX session storage. If a failed write leaves no durable bytes,
a reconstructed attempt can retry: no earlier session was admitted. Partial or
uncertain retained records refuse. The owner is sequential; this does not provide
cross-thread/process compare-and-swap. The boot token is public and deterministic,
unique within the retained ledger rather than globally or cryptographically random.

## Evidence

The [result and source pins](../../tests/benchmarks/crypto/OT-206-INVITATION-LIFECYCLE-2026-09-12.json)
record 239 total groups:91 lifecycle,41 authorized-session,10 Ed25519 known-answer,
40 existing invitation,31 existing session and26 independent Noise controls.
The host run used GCC/G++16.1.0 and the admitted scalar libsodium1.0.22 sources.
New C++ test suites compiled with warnings as errors; the frozen upstream scalar
control retains unused-function warnings, listed in the result.

Fault injection found a corrupted pre-reservation read of the overwrite slot
could escape the original check. Both prior slots now require blank bytes or an
exact boot record before reservation; the full suite passes after this correction.
The retained fault matrix includes9 pristine retries and19 retained-state refusals.

An ESP32-S3 compile-only integration probe also passed with the existing IDF6.0.2
build flags/toolchain. It is not a linked firmware build or physical execution.
The first compiler launcher attempt was denied filesystem access; the identical
compile-only command passed with sandbox approval. No board was accessed.

## Validation command

From the active worktree, run `C:\Python314\python.exe -X utf8 -B
 tests/host/security_policy_invitation_lifecycle_tests.py --output-root <new-absolute-worktree-build-directory>`.
The runner builds the crypto objects once for all five suites, retains commands
and results, and is registered in `tools/Test-Host.ps1`.

## Target preflight and remaining acceptance

Firmware porting lessons were reviewed. Items1/3/6/7 concerning exact board
configuration, transport, linked image and hardware recovery are not executed:
this increment adds reusable components and host integration, not a deployable
target. Item2 source bindings and compiler identity are retained; no two-image
reproducibility claim is made for the component probe. Items4/5 are addressed by
serialized ownership, exact persistence ordering, cancellation and injected failure
checks. Target admission must bind isolated namespaces and initialization order,
then build the actual consuming target before any physical trial.

This closes the bounded software reconstruction/restart proof. Human confirmation,
actual NVS power interruption, factory-reset identity/ledger coordination and
cross-node membership/rekey remain product/physical gates. No new hardware grant,
crypto selection, V1 score credit or public website status change is implied.
