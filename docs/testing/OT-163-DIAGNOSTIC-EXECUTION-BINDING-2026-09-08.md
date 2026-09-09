# OT-163 diagnostic execution binding

Date: 2026-09-08

## Scope

This is deterministic host evidence only. It integrates the bounded retry
observer at the existing concrete backend `open_radio_endpoint` seam without
copying or changing the accepted runner, timeouts, firmware target, physical
coordinator semantics or restoration operations.

No device or phone was opened, reset, flashed or otherwise accessed. No physical
authority was created or consumed. The firmware-porting checklist is not
applicable because no firmware target or board binding changed.

## Accepted host behavior

- The decorator resolves A/B only from exact configured endpoint object identity;
  equal text or another equal object cannot select a role.
- Role verification, application write, exact application readback, hard reset
  and endpoint open are forwarded unchanged to the guarded backend.
- The real endpoint retains ownership of close/reopen and its radio lease. An
  unconfirmed close keeps the lease active and prevents ROM restoration.
- One observer belongs to one diagnostic session. Its bounded snapshot contains
  only allowlisted roles, operations, receipt kinds/scalars and error categories;
  it excludes session tokens, challenges, payload digests and raw exceptions.
- The source closure binds 26 exact files. The image closure binds the accepted
  297,152-byte benchmark application and the two distinct Trail restoration
  applications, 586,736 bytes for A and 587,968 bytes for B.
- Attempt ordinals 1-9 select distinct journal, execution and recovery filenames.
  They do not grant execution. A fresh session cannot reuse an already consumed
  namespace, and recovery never requires the benchmark file or opens radio.

The checked-in host binding is
`tests/benchmarks/crypto/OT-163-DIAGNOSTIC-EXECUTION-BINDING-1-2026-09-08.json`.
It records no private endpoint, board identity, credential or hardware address.

## Validation

Focused execution:

```powershell
python tests/host/noise_xk_diagnostic_execution_tests.py
```

Result: 8 tests passed. After the bounded startup-tolerance successor was added,
the complete affected predecessor/successor chain passed 45 tests: solicited
coordinator 6, backend 8, execution 3, bundle 7, retry diagnostics 8, diagnostic
execution 8 and startup execution 5.

The checked-in binding passed exact validation for 26 sources, three images and
diagnostic attempt ordinal 1. `git diff --check` passed.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Test-Host.ps1
```

The first sandboxed run reached the unrelated current-user DPAPI round-trip and
failed `key_store_unavailable`. The exact Wio GNSS authority suite then passed
15 groups with normal current-user DPAPI access. The final combined Host matrix
was rerun in that environment and exited 0, including both new execution suites,
291 authoritative raw-byte inputs, 16 V1/V1.5 scope groups, publication safety,
Windows loader and simulator gates.

## Remaining gate

Host binding does not authorize hardware. The physical attempts used separate
non-reusable grants and both are consumed. The next gate is host-only analysis
of attempt 2's exact receipt boundary. Any proposed correction must retain the
frozen runner deadlines unless new evidence justifies a change, pass the complete
affected matrix, and receive separate acceptance before a new authority can be
considered.

The subsequent bounded attempts are documented in the
[instrumented run](OT-163-DIAGNOSTIC-RUN-2026-09-08.md). Attempt 2 located the
failure at the expected `TX_DONE` receipt after `m3`, but did not establish why
that receipt timed out. This work does not admit a benchmark result, select
cryptography, implement product messaging or change V1 completion.
