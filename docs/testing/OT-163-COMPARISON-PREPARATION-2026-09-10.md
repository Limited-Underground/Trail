# OT-163 corrected comparison and security proof preparation

## Scope and evidence boundary

This batch prepares the corrected mbedTLS/PSA comparison while reconciling
resource accounting, transcript custody and reusable security tests. It preserves
the accepted [two-node radio result](OT-163-RECEIPT-CONSOLE-RUN-2026-09-10.md).
It does not execute a device, select production cryptography or close Phase 2/3.
The [existing admission gates](OT-163-CRYPTO-INTEGRATION-GATES-2026-09-10.md)
remain controlling; no new V1 security requirements are added.

## Retained firmware reuse

Both retained corrected builds match the six artifact descriptors accepted in
[OT-151](../../tests/hardware/OT-151-2026-08-27.md): application, ELF, linker map,
generated configuration, bootloader and partition binary. The corrected
application is 245,584 bytes, SHA-256
`458e8bf93be1f2d318a2c6010b6d09a93815edabead4c9149fb584a9f6db5f66`.
Twelve applicable target/common/control/configuration source inputs are recorded
in the private reuse evidence. This is current verification of retained artifacts,
not a new active-worktree build or a hardware result.

The inherited target, corrected failure transcript and successful protocol
suites pass 23 focused tests. The valid successful transcript still requires
exactly 1,015 frames. Decision
[0087](../decisions/0087-record-ot150-abort-and-correct-mbedtls-psa-successor.md)
owns the earlier PSA failure and its narrow correction.

## Additive runner and restoration scope

The [comparison session](../../tools/mbedtls_comparison_execution.py) composes
the pinned historical coordinator with the corrected protocol runner. Its
separate namespace preserves consumed historical attempts. The original source
files and the accepted receipt-console execution package remain unchanged.
The private package binds 19 source files and three application images. Pinned
dependencies load from verified source bytes in an isolated namespace without
replacing the process import loader.

Each role has its own exact original application: A is 586,736 bytes and B is
587,968 bytes, as recorded by the accepted radio restoration. The old shared
500,944-byte original is not applicable. Role verification precedes backend
operations. Comparison failure and recovery preserve independent restoration;
an unavailable candidate must not prevent restore-only recovery.

The successor retains only validated canonical benchmark frames in private
exclusive files, with byte counts and SHA-256. It excludes arbitrary startup
output from retained transcripts. Failed persistence or validation fails the
comparison and leaves restoration mandatory. Host fixture captures are synthetic
test evidence, never physical measurements.

Fifteen [composed host tests](../../tests/host/mbedtls_comparison_execution_tests.py)
pass, including complete 1,015-frame captures for each role, partial writes on
either device, restoration after journal/capture persistence failure, consumed
namespace rejection, source/image/scope/grant rejection, concurrent construction
and restore-only recovery with the candidate missing. The independent review's
global import-hook finding and coordinator read/compile hash gap were corrected
before this result. A regression rejects changed bytes between initial package
verification and the compilation read.

The complete `tools/Test-Host.ps1` matrix passed, including the historical loader
compatibility suites and current simulator suites. Those historical loader checks
are not acceptance of the separately owned current Firmware-Loader product.
The final focused suite passed again after the same-buffer guard was added.
Repository documentation checks and 13 tests, 16 V1/V1.5 scope groups, and the
historical matched-resource/17-binding corpus validators also pass.

This module has no device backend or authority issuer. Physical use still needs
reviewed concrete backend composition, fresh identity/layout/protected-region
readbacks, exact executable/artifact bindings and fresh one-use authority. No
historical grant may be replayed. Devices must be handled sequentially.

## Reusable security evidence

Four existing suites passed 27 host groups: Noise composition (4), outbound
counter leases (10), KV composition (5), and secure-random policy (8).
The Noise composition suite supplies fake cryptographic primitives; it proves
ordering, rejection and wipe behavior, not independent cryptographic agreement.
The scoped source search did not identify an independent XK transcript artifact.

The next interoperability proof must compile the actual admitted adapter against
real primitives and compare complete messages, handshake hash and directional
keys with an independently produced, provenance-bound vector. A handwritten
mirror of the adapter or the current fake primitives cannot be its oracle.

Counter tests cover memory/KV failure injection, not target interrupted writes.
Random-policy tests use a fake source; target readiness still needs exact startup
and entropy-source evidence. These findings identify unproven layers, not observed
hardware failures. Owner-deferred cold-power disassembly stays deferred. Physical
attacker image rollback resistance remains outside the accepted V1 boundary.

## Accounting and continuation

Pinned `esp_idf_size` 2.3.1 reproduces both corrected linker-map totals without a
build: 245,466 linked-flash bytes and 50,187 static-RAM bytes. Retained old controls
measure 145,834 and 49,035 bytes. The arithmetic differences are 99,632 and 1,152
bytes, but these are not newly admitted matched deltas: corrected candidate and
old controls have different project versions. Exact matching control builds and
their provenance/linkage admission remain necessary for a corrected matched result.

The old matched-resource result remains applicable only to its exact candidate
and control. Equal application sizes do not transfer its admission to the
corrected binary. Retained receipt custody and raw-transcript custody are separate
facts; neither a summary nor a hash without the matching retained bytes replaces
the required trace.

The retained Monocypher receipt and journal exactly match the OT-146 SHA-256
bindings, including both parsed-result/diagnostic projections. The inspected
writer retained summaries, not raw transcripts. A scoped independent hash search
of 63 files in retained libsodium test directories found no match to the two
OT-122 raw-capture hashes. This does not establish global absence. Historical
summary evidence remains valid at its accepted layer; raw replay custody remains
unresolved and cannot be reconstructed from aggregate statistics.

Complete the successor's concrete backend and authority composition before a
physical comparison. Reconcile corrected measurements and remaining matched
resources/private traces into an additive corpus, then close the named security
gates before explicit selection and product integration. Preparation earns no
completion credit. Public website capability status is unchanged; no publication
or deployment occurred.
