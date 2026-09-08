# OT-163 solicited execution integration

## Accepted host path

The solicited runner is now connected to an isolated per-role coordinator and
the concrete role-checking backend. The new coordinator has its own one-use
journal and receipt names; existing imported coordinators and frozen files keep
their earlier behavior. The backend guards readiness with the same lock and
fresh passive identity checks as other serial operations. ROM operations remain
blocked while any radio handle may still be open.

The ROM parser accepts exactly one or two strictly valid agreeing identity lines,
matching the observed esptool output. Malformed, conflicting, empty or extra
identity records are rejected. Private predecessor modules execute their exact
hash-checked source bytes rather than cached bytecode.

The [binding snapshot](../../tests/benchmarks/crypto/OT-163-SOLICITED-EXECUTION-BINDING-2026-09-08.json)
contains the complete 24-file source/provenance set and three image descriptors.
Execution requires the exact accepted readiness build record and application
image, not merely agreement between an arbitrary image and configuration.
Recovery still requires both independently bound restoration images, but does
not require the benchmark file. No prior authority or journal is reused.

## Focused validation

Six coordinator, eight backend, three full-composition and seven bundle/admission
tests pass. The actual coordinator, backend, runtime, buffered parser and runner
operate over a simulated serial transport that purges startup bytes on every
open. The unchanged result validator accepts 14 frames and 736 wire-payload bytes;
each role receives its own original restoration image. Readiness failures close
handles and restore both roles. Failure to restore one role does not prevent the
other role's restoration; independent recovery succeeds with the benchmark file
removed. Invalid source/image/configuration admission fails before device access.

## Observed hardware attempt and restoration

The single newly bound application-only attempt ran from 18:43:22 to 18:50:04 UTC
on 2026-09-08, including preflight and postchecks. Both Heltec development boards
passed ROM identity, ESP32-S3/16 MB geometry, exact original application plus
erased-tail readback, and Trail boot/partition admission. Both received and
verified the exact `nxk-ready-v1` benchmark image. The setup was the existing two
USB-connected bench boards using the V4.2 pin map; board revision, separation,
antenna placement and battery state were not newly measured.

Execution stopped at `cycle1_retry_timeout` with no admitted benchmark result.
Reaching this sequential stage establishes that initial and post-restart
solicited readiness and the first baseline handshake validator returned
successfully. That is a control-flow inference from the exact bound runner;
raw serial receipts were not retained. This stage covers retry preparation, M1
exchange, deliberate M2 withholding, timeout validation and responder abort.
It does not establish which substep failed or that the timeout receipt itself
was defective. Total packet counts, loss, duplicates and latency were not
admitted for this aborted attempt; it is not a field-range or product messaging
result.

The coordinator restored, read back and reset both original applications:
586,736-byte `ot178-phone-v1` on A and 587,968-byte `ot171-label-v1` on B.
Independent postchecks then matched each complete 589,824-byte application/erased
span and the preflight bootloader, partition and OTA bytes, followed by successful
guarded resets. No separate recovery invocation was needed. NVS was outside all
write ranges; it was not dumped. Phone Ready and displays were not observed.

The [sanitized outcome](../../tests/hardware/OT-163-SOLICITED-LIVE-OUTCOME-2026-09-08.json)
retains the coordinator result, exact image hashes, anonymous pre/postchecks and
limits. The new grant is consumed and non-reusable, as is the earlier OT-162
grant. No automatic second attempt or continuation is granted by this record.
The next bounded task is finite substep/error-category diagnostics and host
regressions within the failing group, before binding any later attempt.

## Execution safeguards and validation

The private live bridge verified and loaded the complete source set in a fresh
process, bound its own source and one-use grant, and performed the pre/postchecks
above. Ten independent offline tests covered admission before consumption,
source/cache checks, exact grant types, error redaction, independent cleanup and
refusal to boot a partial restoration. The complete affected Windows host matrix
passed locally before device access; documentation and publication scans passed.

The existing `nxk-ready-v1` binary is reused byte-for-byte from the two accepted
clean builds; no firmware source or radio profile changes occur here. Target
porting checks for source/image integrity, serial lifecycle, identity, flash
layout and recovery apply. Pin remapping, new builds, storage schema changes,
BLE, GNSS, battery, cold-power, Android installation and field-range testing were
skipped because this increment changes none of those boundaries. The benchmark
retains US915/915 MHz, BW125 kHz, SF7, coding rate4/5 and 2 dBm command setpoint.
Only the application slot may be written; the benchmark table is never installed.

V1 scores and public website status remain unchanged; website publication remains
owner-deferred. Startup/baseline progress does not complete the failed benchmark,
select a crypto suite or add release-readiness credit.
