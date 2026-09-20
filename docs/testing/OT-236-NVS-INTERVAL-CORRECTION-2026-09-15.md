# OT-236 NVS preservation interval correction

The subsequent [caller preparation](OT-236-INTERVAL-CALLER-2026-09-15.md)
has completed the integration described in this report's remaining gate. No
corrected physical trial has run.

## Problem and behavior

The [physical preflight](OT-236-PREFLIGHT-RESULT-2026-09-15.md) compared saved
settings against a historical snapshot. Its original-app resets also allowed
normal runtime between preservation checks. Revision 2 acquires a fresh baseline
and keeps original execution outside the interval it measures.

`IntervalBackend` accepts only pinned bootloader, partition and OTA descriptors.
After exact identity, geometry, static-region and original-application checks,
it reads all 12288 NVS bytes twice while holding ROM. Matching hashes are saved
exclusively, flushed, fsynced and read back. The immutable record binds the role,
private identity digest, package and original image. No raw settings are saved.
Coordinator preflight checks that interval without booting either original.
The first candidate write requires another original/protected check and a durable
single-use candidate-start marker. It cannot acquire another baseline or repeat
the candidate write inside that interval.

After capture, original application restoration and protected readback, an
independent full original/protected check closes the interval. A durable release
intent precedes the original reset. A completed release marker follows it.
Only then may the coordinator advance to B. Untouched admitted nodes are checked
and released without application writes if execution stops early.

## Recovery and evidence boundary

The revision-2 journal binds both baseline hashes. Recovery requires those exact
hashes in the backend and journal; it cannot substitute current NVS observations.
A closed interval still checks its baseline before release. A release intent
means the original might have run, so recovery checks its exact application and
static regions and completes release without claiming post-release NVS equality.
Already released roles require no further ROM operations. An uncertain serial
lease continues to block ROM operations.

NVS mismatch during the candidate interval refuses further candidate work and
protected-region restoration checks. The implementation has no NVS write or erase
path. Such a failure needs explicit reconciliation; it must not be reported as
successful preservation or automatically repaired.

The old `Backend` remains solely for historical compatibility tests. Revision-2
Session requires the interval methods and uses `OTLSCJ2`/`OTLSCR2` and separate
`libsodium-capture-2-*` artifacts. The failed live-1 caller, manifest and package
remain unchanged and cannot run this revision. Baseline hashes are private
preservation evidence, not additional benchmark results or V1 credit.

## Validation and remaining integration

The [host proof](../../tests/benchmarks/crypto/OT-236-NVS-INTERVAL-HOST-2026-09-15.json)
records the affected matrix, commands, outputs and exact source hashes. Tests
use the actual Session/IntervalBackend with simulated ROM/USB, alongside direct
fault tests for changing NVS, baseline persistence, single-use state and recovery.
No firmware source changed; existing candidate/recovery artifacts are reverified
without rebuilding them. No hardware, phone or RF action is part of this fix.

A fresh physical caller still must bind static descriptors and the new baseline
records, retain a preflight intent before device access, release originals after
pre-admission failures, and perform its independent checks before original release.
Do not reuse live-1's post-release admission check or its historical NVS manifest.
That caller and current physical authorization are required before another trial.
Physical capture custody and all other admission/product gates remain open.
No public website status or accepted V1 milestone changed.
