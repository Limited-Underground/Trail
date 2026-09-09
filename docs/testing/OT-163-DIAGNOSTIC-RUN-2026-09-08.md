# OT-163 Instrumented two-device benchmark

## Execution boundary

This increment combines diagnostic integration, composed host tests, physical
execution and bounded correction/retest where the evidence supports one.
Physical results are recorded below after execution; source presence and host
simulations are not physical acceptance.

The adapter wraps only the backend's returned radio endpoints, resolving role
from exact configuration object identity. It delegates the existing write,
readback, reset and recovery methods. A session uses an ordinal-specific journal
namespace and unchanged one-use admission. Its 26-source snapshot extends the
unchanged prior 24-file snapshot with the observer and integration module.
The private execution grant also binds the ordinal and diagnostic capacity.

## Host validation and preflight

Eight composed diagnostic test groups exercise the real byte parser/runtime/runner with
synthetic serial I/O: 14 frames/736 bytes, retry rejection with retained safe
diagnostics, distinct A/B restores, failed reopen, failed-close lease retention,
source tampering before I/O, recovery without the benchmark and consumed-journal
denial. The earlier eight diagnostic groups cover thirteen receipt boundaries
and delayed 2,196 ms delivery. The private wrapper reuses the hash-pinned prior
physical bridge through exact-count replacements rather than a second copy of
the flash and restoration implementation.

Five startup-tolerance groups cover bounded recognized startup records, stale
healthy readiness, unhealthy/active/unknown rejection, post-readiness strictness,
the unchanged absolute readiness deadline and source tamper rejection. The
combined predecessor/diagnostic/startup chain passes 45 tests. The complete Host
matrix passes with both suites registered once.

The applicable firmware-porting checklist is applied as follows:

- Target and bytes: reuse the accepted Heltec V4 / ESP32-S3, 16 MB bench target,
  native USB console, fixed US915 benchmark profile and application offset
  `0x10000`. The benchmark image is 297,152 bytes, SHA-256
  `3c3913ee4c2a60bd4168e0bb60eca6a0f2a641ba8f283f99f656c04c07b301c7`.
- Builds: firmware sources, configuration and dependencies are unchanged. Reuse
  the accepted matching A5/B5 builds and their provenance; rebuilding an unchanged
  target is skipped. All three image files are rehashed before execution.
- Lifecycle/concurrency: retain solicited readiness, fresh-handle reopen, strict
  parsing and one serialized physical operator. The observer adds no commands.
- Persistence/recovery: use distinct original A/B images; write no partition
  table, bootloader, OTA metadata or NVS. Verify each own application/erased span
  and preserved regions before execution and independently after restoration.
- Composition: focused host tests and the complete affected host matrix precede
  hardware. No physical action is admitted solely from unit tests.
- Hardware: volatile identity, geometry and installed-image checks run again
  immediately before the one-use execution. No cold-power disassembly, phone
  installation, bond changes or device-registry changes are part of this run.

Build configuration and recovery hashes remain in the
[prior execution evidence](OT-163-SOLICITED-EXECUTION-2026-09-08.md).

## Physical outcomes

Attempt 1 reached the post-restart readiness check and aborted on an invalid
Node B readiness receipt before prepare or send. Its authority was consumed.
Both role-specific Trail applications were independently restored, read back
and reset, with their protected regions passing postcheck. The allowlisted
record is
`tests/hardware/OT-163-DIAGNOSTIC-LIVE-OUTCOME-1-2026-09-08.json`.

The startup-tolerance successor then extended the frozen closure to 27 sources
without changing the target, runner deadlines, images or restoration behavior.
Five deterministic groups admit only malformed recognized startup records and
healthy stale READY records before fresh readiness; unhealthy, active, unknown,
post-readiness and over-budget input still fails closed.

Attempt 2 issued message `m3` and accepted its `TX_START` receipt, then aborted
because no valid `TX_DONE` was accepted within the five-second deadline. The
one-use grant was consumed. Both role-specific Trail applications were fully
restored, read back and reset. This bounded observation establishes the timeout
location only; it does not establish why the receipt was absent. The retained coordinator receipt, safe transport events and independent
postchecks provide the execution timestamps and exact recovery evidence in
`tests/hardware/OT-163-DIAGNOSTIC-LIVE-OUTCOME-2-2026-09-08.json`.

## Capability boundary

A partial attempt cannot admit the planned packet counts, latency or radio-cost
result. Neither attempt changes V1 completion or the website projection.
Diagnostic receipts contain only allowlisted fields, not raw serial
data, identifiers, challenges or key/payload digests. No product phone-to-phone
messaging or crypto-suite selection is implied by this engineering benchmark.


## Follow-up driver fault reproduction (2026-09-09)

The target emits `TX_DONE` only after synchronous transmit and receive rearming
return. Missing or malformed serial output is another unresolved possibility.
The trace therefore cannot attribute the physical timeout to a particular driver
or hardware state.

A host experiment extracted the actual RadioLib 7.7.1 `transmit`, `startTransmit`
and `launchMode` method bodies from the pinned managed dependency. Peripheral
operations were stubbed and HAL pin states controlled. With BUSY held high, a
host watchdog interrupted after 1,000 simulated milliseconds; the transmit
method had made zero timeout-clock reads. With BUSY low and IRQ low, the normal
driver timeout returned after 600 simulated milliseconds; the healthy IRQ-high
control completed. These are constructed faults, not measurements of the boards.

The experiment proves an unbounded BUSY wait before the driver's transmit timer
starts. It does not prove BUSY was high during attempt 2. The next correction
should bound that wait and add finite transmit-return/receive-rearm checkpoints,
plus parser-discard diagnostics, before a fresh hardware attempt. Preserve the
existing host deadline until evidence justifies a change.

Reproduce with `python tools/reproduce_radio_busy_wait.py` from a checkout with
the accepted managed RadioLib dependency and a C++20 GCC compiler. The script
rejects changed driver source hashes and writes only under `build/radiolib-busy-repro`.
It uses a five-second process limit for the generated test. The
[reproducer](../../tools/reproduce_radio_busy_wait.py) and
[result record](../../tests/benchmarks/crypto/OT-163-RADIO-BUSY-FAULT-2026-09-09.json)
retain exact method/source/harness hashes. This dependency-specific experiment
is separate from CI's portable host matrix. No firmware or driver was changed.
