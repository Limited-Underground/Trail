# OT-188 security capture and full-NVS recovery preparation

The security evaluation now has a host-tested package, authority, serial endpoint
and recovery composition. This is **host-ready preparation**, not hardware
acceptance. No device was read, reset, flashed or run in this increment, and no
live private snapshots or execution grant were created. The two frozen OT-187
builds are reused without changing their target sources or images.

## Implemented composition

Four new Python modules compose the retained capture parser:

| Component | Responsibility |
| --- | --- |
| [security_policy_bundle.py](../../tools/security_policy_bundle.py) | Freeze and verify private candidate, original application/NVS snapshots, protected-region descriptors, role bindings and source pins; canonical package digest |
| [security_policy_hardware.py](../../tools/security_policy_hardware.py) | Explicit two-role ROM/serial transport, passive and ROM identity checks, exact read/write spans, handle generations and exclusive serial leases |
| [security_policy_endpoint.py](../../tools/security_policy_endpoint.py) | One consumed command, bounded write/read admission, guarded endpoint identity, strict receipt capture and confirmed closure |
| [security_policy_execution.py](../../tools/security_policy_execution.py) | External file-authority validation, consumed-grant barrier, process exclusion, durable journal, sequential execution and scoped restoration |
| [security_policy_capture.py](../../tools/security_policy_capture.py) | Frozen challenge/receipt framing, exact deadline, fragmentation and trailing-output rejection |

Importing these modules opens no device. There is no CLI, automatic discovery of
roles or grant issuer. A caller must supply exact private A/B bindings. Route
uniqueness is case-insensitive; normalized device identities must also differ.
USB and ROM identity checks prevent accidental board substitution; they do not
prove resistance to cloned identifiers or hostile hardware.

Execution admission verifies the exact
[OT-187 build report](../../tests/benchmarks/crypto/OT-187-SECURITY-POLICY-BUILD-2026-09-10.json)
(digest `c48873be23aac58a3fdb725b25e023bf53b8ddc7566bf02cd926f8dba32c8832`),
its 33 source pins and the 437,488-byte candidate image. The package additionally
binds all five Python modules above, both private snapshot paths and bytes,
protected-region descriptors, identities, routes and fixed nonradio scope. Private
files must be absolute, beneath the active worktree's `.private` directory and
free of symlink/reparse traversal. Errors and summaries omit private identifiers
and raw payloads. Freezing this metadata does not grant execution authority.

## Exact storage and boot boundary

| Region | Offset | Admitted span | Operation |
| --- | ---: | ---: | --- |
| Bootloader | 0x0000 | 32,768 bytes | Read/verify only |
| Partition table | 0x8000 | 4,096 bytes | Read/verify only |
| OTA selection | 0x9000 | 8,192 bytes | Read/verify only |
| Ordinary NVS | 0xd000 | 12,288 bytes | Full original capture and exact restoration |
| Application | 0x10000 | 589,824 bytes | Candidate padded with 0xff to the full span; exact original restoration |

The partition descriptor is pinned to the OT-187 3,072-byte partition artifact
padded with 0xff to 4,096 bytes; its digest is
`b7bbaf702afd377973aa2371f288bcea50548865d10e2cdada4d5e7f98a91601`.
OTA selection is pinned to the admitted factory-selection artifact, digest
`7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f`.
An arbitrary self-described layout or alternate OTA selection cannot authorize
the fixed NVS/application ranges. The bootloader remains freshly bound per role;
this does not attest secure boot or an adversarial boot chain.

The full ordinary NVS snapshot may contain pairing and owner information. It stays
private. Namespace erasure or reconstruction of visible settings cannot replace
whole-span write/readback equality. Creation of live snapshots itself requires
separately authorized device read/reset handling; synthetic host fixtures do not
establish those originals.

## Authority, execution and recovery

`FileAuthority` accepts an externally supplied, exact-hash private grant with one
attempt, matching package digest, fixed action scope and finite UTC validity. It
never issues a grant. A grant is consumed durably before device operations; a
consumed namespace cannot be reused. Normal automatic restoration is already included in the execute grant. A separate
later `recover` call requires fresh restore-only authority bound to the original
attempt and the same package. Its explicit action scope
includes ROM preflight, original application restoration, full-NVS restoration
and original reset; it cannot run a new evaluation.

An OS advisory process lock excludes concurrent cooperating controllers and is
released on process termination. A separate durable active-attempt record survives
that termination and prevents starting a new attempt over unresolved work. Neither
mechanism replaces the backend's serial-handle lease. A failed or uncertain open
or close retains a blocking lease; subsequent ROM work requires confirmed closure.
A durable `serial_open_intent` precedes opening the handle, and `serial_closed`
is recorded only after confirmed closure. Recovery in a newly constructed backend
or process still refuses an unresolved serial-open intent: an empty new in-memory
lease pool is not evidence that the earlier handle closed. That state requires
explicit reconciliation.

The normal sequence fully handles A before beginning B: verify protected regions
and original application/NVS, write and read back the padded candidate, verify
protected/NVS bytes again, reset, issue one fresh challenged request, capture one
terminal result, close serial, restore the full original application and NVS,
read back both plus protected regions, then reset the original application.
A non-pass evaluation stops before B after A's verified restoration. Receipt
success alone is insufficient for a successful two-role result.

Journal records enforce ordered, bounded transitions and exact allowed fields.
Intent is recorded before each mutation. Corrupt or unwritable journals withhold
further journaled hardware mutations; a known serial handle still receives a
best-effort close, and unconfirmed closure remains blocking. Immediately before
resetting restored original firmware, the journal records `original_boot_intent`.
Once that reset may have run, recovery refuses to overwrite NVS from an older
snapshot unless the recorded state permits a safe restoration path. A completed
original boot is skipped; an uncertain original reset requires reconciliation.
This prevents treating a missing acknowledgement as permission to overwrite newer
owner or pairing state.

Restore-only verification deliberately tolerates a missing candidate image or
build report. It still verifies the package's fixed descriptors, current recovery
code pins, both original payloads, role bindings, protected regions, grant and
journal state. It restores only a journal-authorized touched role; it does not
restart the evaluation, reconstruct keys, blindly resume a serial session or
bypass missing/corrupt custody. A preflight-only uncertainty is a reconciliation
case rather than an inferred restoration authorization.

## Python API sequence

These are integration boundaries, not an executable flashing recipe:

1. After separate live preflight and private snapshot custody, call
   `freeze(root, candidate, roles)` and retain its canonical digest. `verify`
   returns checked candidate/original bytes and role metadata without device I/O.
2. Supply externally issued authority through
   `FileAuthority(root, grant_path, expected_sha256)` and create a backend with
   the same exact role bindings. Construction is not permission to operate.
3. Call `execute(root, package, authority, backend)` only after the complete
   physical/runtime gates and that grant's scope are admitted. The executor owns
   sequencing and consumes fresh challenges; there is no automatic retry.
4. If eligible recovery is needed, use fresh restore-only authority and
   `recover(root, package, authority, backend, origin_attempt=...)`. An incomplete
   or refused recovery must be reconciled; do not delete its journal or lock to
   force another run.

The [OT-187 capture contract](OT-187-CAPTURE-PREPARATION.md) remains authoritative
for framing and deadline bounds. SDK calls and injected callbacks must honor their
own bounds. Python cannot cancel an indefinitely blocking callback, and a valid
capture only covers its observed horizon, not arbitrary future output.

## Host validation

| Command (repository root) | Passed cases | Scope |
| --- | ---: | --- |
| `python tests/host/security_policy_bundle_tests.py` | 19 | Exact package/source/payload binding, private paths, reparse refusal, strict JSON and recovery without candidate |
| `python tests/host/security_policy_hardware_tests.py` | 23 | Actual backend with simulated transport/inventory, spans, identities and serial leases |
| `python tests/host/security_policy_endpoint_tests.py` | 13 | Actual one-use endpoint with simulated handle and monotonic clock |
| `python tests/host/security_policy_execution_tests.py` | 19 | Authority/journal/ordering, process exclusion and restoration failure policy |
| `python tests/host/security_policy_package_tests.py` | 9 | Actual complete bundle/authority/execution/backend/endpoint/parser with physical transport and inventory simulated |

All 83 focused cases passed. The
[host preparation evidence](../../tests/benchmarks/crypto/OT-188-CAPTURE-RECOVERY-2026-09-10.json)
records the bounded result. The sandbox run of the complete host matrix stopped
at an existing DPAPI test. The isolated 15-case authority suite and complete host
matrix passed under the normal user, including all 13 simulator UI tests. No
product change was made for that execution-environment difference. Documentation
checks and 17 tests, 16 scope groups, publication safety and 291 raw-byte checkout
inputs also pass. Full-stack cases cover both-role pass,
complete padded application and full-NVS restoration, protected-byte preservation,
wrong/expired/consumed authority, refused receipts, recovery without candidate,
changed source or role binding, and serial-close uncertainty withholding ROM work.
These tests create only synthetic private files. They do not exercise hardware,
physical persistence, real USB timing, radio or the evaluation firmware itself.

## Remaining physical admission gate

The package pins five local Python modules and the applicable build sources. It
does **not** pin the complete external Python runtime or transitive tooling/import
closure. The ROM transport checks Python 3.14.6, esptool 5.3.1 and pyserial 3.5
version strings; matching versions do not prove matching installed bytes or import
origins. A future fresh operator process must independently verify its interpreter,
exact installed tool/module/dependency bytes and import origins before importing or
running the physical caller, and bind that runtime to the admitted attempt.
Inherited module search paths cannot be presumed safe merely because the package
source hashes pass.

A future trial also requires fresh exact inventory and installed-region readbacks,
private complete NVS/application originals, reset/endpoint handling, and explicit
one-use physical authority. No earlier consumed grant is reused. This preparation
does not accept the [OT-187 policy](OT-187-SECURITY-POLICY-EVALUATION-2026-09-10.md)
on hardware or select a production crypto suite. It adds no V1 completion credit
and does not change public website status.
