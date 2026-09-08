# OT-163 Host-only benchmark successor composition

## Result and boundary

The successor composes startup admission, buffered byte receipts, restart with
fresh serial handles, and distinct role A/B restoration descriptors. It reuses
the immutable OT-153 protocol/result rules and OT-156 restart orchestration.
It is dependency-injected host software, with no CLI, issued authority, port
selection or executable hardware bundle. No device was opened, reset or flashed.
The consumed OT-162 authority and OT-163 abort remain unchanged.

## Corrections demonstrated by composition

Both initial boot contracts must pass before the first restart: stale-replay
self-test, exact firmware/radio identity, configured profile, zero counters,
command availability and a solicited profile readback. Missing boot output fails
closed; a guessed sleep is not readiness. After accepted restart receipts, each
role gets a fresh handle and receipt buffer with DTR/RTS false before opening.

The byte-level path also exposed two defects hidden by prebuilt-receipt tests:
separately imported Receipt classes failed the runner's type check, and the old
lexical parser rejected the actual firmware's `payload_sha256`, `tx_key_sha256`
and `rx_key_sha256` field names. The successor shares the runner's receipt type
and admits exactly these three numeric-suffix names. Existing semantic field
sets, hashes, counters, timing and public-result validation remain authoritative.
Frozen source files are not patched or rewritten.

## Role-bound restoration

The binding includes separate A/B image names, lengths and hashes. Exact image
bytes and installed readbacks must pass before authority consumption and writes.
The journal and receipt retain both descriptors. Recovery reads the role-specific
restoration images without requiring the benchmark file or invoking the runner.
One role's restoration failure does not prevent the other's independent attempt.

An injected backend must explicitly verify the endpoint's anonymous role before
every operation, independently of installed application bytes. Both devices can
contain identical benchmark bytes, so image equality alone cannot establish
which original recovery image belongs to which device. This interface is a
requirement on the future concrete backend, not evidence of USB identity today.

The original and second recovery files were rehashed locally and still match
[the recorded two-image baseline](OT-163-RECEIPT-STREAM-2026-09-08.md#product-and-recovery-boundary).
This verifies local artifacts only, not installed bytes or current connectivity.

## Validation and next gate

The focused suites exercise the real byte-parser/runner path and failure recovery
with fake serial handles and backends. Both are wired into `tools/Test-Host.ps1`.
The 83 focused cases pass, including eight byte-composition cases and fifteen
per-role recovery cases; 291 raw-byte inputs, 16 scope groups, 13 documentation
tests and publication scanning also pass. Synthetic
radio receipts validate host orchestration; they are not measured radio results.
V1 milestone scores, product message support and public website status do not
change.

Next, bind one complete executable successor to the benchmark image, runner,
parser, runtime, coordinator, concrete role-verifying backend and both exact
recovery images. Verify source closure, actual endpoints, installed application
bytes, partitions and recovery before a fresh non-reusable execution grant.
The new injected grant interface cannot reuse the old OT-162 grant. No hardware
attempt is admitted by this document or by host test success.

The firmware-porting checklist applies here to source-byte integrity, receipt
framing, readiness, reset lifecycle and independent cleanup. Board pins, firmware
builds, partition changes, RF settings, BLE, storage layout, power and physical
acceptance are not applicable to this host-only change; no target changed.
