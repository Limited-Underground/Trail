# OT-201 synchronization operator package — 2026-09-12

## Result and boundary

The executable observation/restoration package is software-validated. The complete
affected matrix passes **280 tests across 16 suites**, including 17 composed
workflows using the exact candidate bytes with simulated devices and seven actual
isolated-launcher tests. This closes the package gate left by
[OT-200](OT-200-INPUT-SYNC-2026-09-12.md); it does not establish the cause of the
earlier physical invalid-length result or constitute a security evaluation pass.

## Exact executable binding

- Target: `heltec_v4_security_sync_diag`, version `ot200-sync-diag-v1`.
- Candidate: 440,432 bytes; SHA-256 `25e18eb5f852ea53a005272af13cbbe6fa828833920ecd5c8c79e826984d5a62`.
- Record contract: `OT200-SYNC-INPUT-RECORD-1`, keys `ot198diag/stage` and `input`.
- Package: `OT201-SYNC-BUNDLE-1`; 22 pinned executable files. The existing 27
  policy modules remain unchanged. New OT-201 grant/intent/custody/claim schemas
  reject predecessor authority and require the detail-write action explicitly.
- Runtime manifest SHA-256: `cb9171ed24be9023e904a8710847e14e73a791daf637e3c3be3a289ffbc7bc97`.

The runtime contains 3,560 files (96,339,904 bytes), built
from existing local dependencies. Real PowerShell parent/child probes pass in
ordinary and hostile Python/esptool environments, with import origins and the
complete inventory verified. No poison fixture executed. A separately retained
package admits this actual runtime, exact candidate and unchanged build/source
pins using synthetic originals only; it issues no authority or physical custody.

## Observation and restoration guarantees tested

Confirmed serial closure and durable restore intent precede observation. The ROM
child consumes one image-bound durable claim before reading. A failed read cannot
be retried through copied child arguments, including while restore intent remains
current. Projection requires that claim and the exact saved-capture hash.

Stage 3 plus a committed input record remains an incomplete durable prefix;
terminal reason 19 retains its exact detail while the legacy stage category stays
compatible. Diagnostics do not substitute for strict solicited receipts. Missing,
invalid or interrupted observations never suppress independent restoration.

Original application/full-NVS restoration, protected-region readback, serial-close
barriers, restore-only recovery without candidate bytes, and guarded release of an
untouched role are exercised through composed synthetic transports. Recovery does
not recapture. The original-reset freshness guard remains enforced.

## Validation and reuse

The [sanitized machine-readable evidence](../../tests/benchmarks/crypto/OT-201-SYNC-OPERATOR-PACKAGE-2026-09-12.json)
records all suite counts, source pins and runtime versions. Reproduce the affected
matrix with `python -B tools/Test-SecuritySyncOperator.py --candidate` followed by
the exact local candidate path; `tools/Test-Host.ps1` also includes the default
synthetic matrix. The final accepted matrix used the exact candidate argument.

The 43 OT-200 build-source pins, 731 local library files and all 14 files in the two
seven-artifact build tuples rehash unchanged. No firmware was rebuilt because no
firmware input changed. Full private commands, logs and synthetic artifacts remain
under `.private/ot201-operator` in the active worktree.

## Remaining acceptance

Fresh physical admission needs current per-role identity, route, original storage
and protected-region custody, exact runtime/image/source binding and explicit
one-use scope. Earlier snapshots and consumed grants cannot authorize it. The
existing firmware porting and physical recovery gates still apply.

No hardware enumeration, serial connection, flash, reset, radio, network operation
or publication occurred. Implementation is complete locally; commit/publication
remain pending separate scope. No V1 credit, crypto selection or public website
status changed. See the [backlog](../../tasks/BACKLOG.md) for sequencing.
