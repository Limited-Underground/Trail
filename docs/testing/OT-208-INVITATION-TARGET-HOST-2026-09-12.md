# OT-208 Actual invitation target host evidence

2026-09-12. The actual target host suite passed 130 groups. This report covers
host execution of the target source; linked firmware builds and physical
acceptance have separate evidence.

The [machine-readable result](../../tests/benchmarks/crypto/OT-208-INVITATION-TARGET-HOST-2026-09-12.json)
binds the test output, commands, native executable and tested source hashes.
The harness compiles the actual `app_main.cpp`, `invitation_evaluation.hpp` and
`nvs_invitation_backend.hpp` with the existing invitation authority, session,
persistence, input parser, stage store and receipt formatter. Real libsodium
signatures, Noise handshake, transcript confirmation, AEAD and replay admission
execute. The successful helper performed 24 commits and six RNG fills.

## Observed behavior

The three invitation namespaces (`ot208_boot`, `ot208_ia`, `ot208_ib`) remain
separate from the four existing `ot187_*` TX/RX namespaces. All seven backends
must be ready before boot allocation. Entropy and time are checked again after
opening them. Boot generation is committed/read back before identity generation;
role consumption precedes session-state writes. Active cancellation of A and
ordinary retirement of B both complete in the successful evaluation.

The suite covers SDK open/read/set failures, short or corrupted reads, every
successful-path commit returning error before application or after applying its
pending state, entropy-fill failures, clock regression and startup refusal.
It forwards `sodium_memzero` to the real library implementation and checks
RNG-written ranges during covering wipe calls. It never reads dead stack memory.

Direct helper repetition advances the boot context, then refuses retained
fresh-only TX/RX state before session-store mutation. Actual application repetition
has a different boundary: retained `ot198diag` refuses before console installation
or boot-ledger allocation. Neither scenario simulates a physical reboot.

The actual app path checks NVS initialization, persisted control admission,
entropy start, sodium initialization, evaluation, entropy stop and one framed
receipt in order. Startup/control failures suppress evaluation; entropy startup,
initialization and evaluation failures still follow the tested shutdown path.

## Toolchain and seams

GCC/G++ 16.1.0 compiled the host proof. Sixteen scalar objects were reused only
after checking the exact accepted OT-206 proof, compiler, generated header and
object hashes; all 731 admitted upstream files were rechecked. Signing and common
objects were compiled afresh. Target and test C++ passed `-Werror` without
diagnostics. Three upstream Ed25519 C files emitted 16 unused-static-function
warnings from their admitted private header; the logs are retained without
suppression or upstream source edits.

NVS SDK calls, time/task delay, console I/O, entropy startup/shutdown and
`sodium_init` results are simulated boundaries. The deterministic test random
source is not an entropy-quality claim. The real guarded entropy runtime also
passed its separate 15 scenario executions across three configurations. The
unchanged receipt boundary passed 24 C++ groups and 12 Python tests. Those
supplementary checks are bound in the JSON and excluded from the 130 target groups.

From the active worktree, run:

```text
C:\Python314\python.exe -X utf8 -B tests/host/security_invitation_target_tests.py --output-root <new-absolute-worktree-build-directory>
```

The optional `--reuse-crypto-root` requires the exact accepted OT-206 private
proof; omission builds the scalar control afresh. The suite is registered in
[Test-Host.ps1](../../tools/Test-Host.ps1). Registration preserves all prior runner
bytes. This evidence batch did not rerun the entire repository host matrix.

The [target porting checklist](../firmware-porting-lessons.md) governs the remaining
build and hardware gates. SDK errors and applied-before-error commits are host
fault models, not measured NVS durability or power-interruption behavior. Actual
USB/entropy startup, whole-memory erasure, human confirmation, cross-node
join/rekey and factory-reset ledger coordination remain outside this proof.
No hardware action, crypto selection, V1 credit or public website status change
follows from these host results.
