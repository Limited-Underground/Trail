# OT-189 operator runtime and physical preflight boundary

This prepares an independently admitted operator runtime and a later nonradio
security evaluation on two intended Heltec V4.2 bench devices. No device I/O,
reset, grant or live snapshot is performed by this increment. Runtime construction,
isolated launch and focused host validation passed. The normal-user host matrix
completed in segments, retaining the unchanged prefix and a final-source tail.
The
[OT-188 host preparation](OT-188-SECURITY-CAPTURE-RECOVERY-2026-09-10.md)
and frozen [OT-187 build evidence](../../tests/benchmarks/crypto/OT-187-SECURITY-POLICY-BUILD-2026-09-10.json)
remain the implementation/build baseline; neither supplies physical authority.

Each physical device would run the same-chip two-session security evaluation
independently. This is not a radio exchange between those devices. No region
change, LoRa TX, product BLE advertising, phone installation, provisioning or
production key selection belongs to this scope.

## Runtime admission before device access

The [runtime builder](../../tools/security_policy_runtime_bundle.py),
[operator](../../tools/security_policy_operator.py) and
[PowerShell launcher](../../tools/Invoke-SecurityPolicyOperator.ps1) create and
verify a private copied runtime before importing the physical controller. The
observed capsule contains 3,545 files / 96,172,657 bytes and 18 third-party packages.
It uses CPython 3.14.6, esptool 5.3.1 and pyserial 3.5. Its manifest SHA-256 is
`96b6ba113ae631b6b122871dea6c7387cc29b011d019be409fbb3d62769ac9bb`.
The [runtime evidence](../../tests/benchmarks/crypto/OT-189-OPERATOR-RUNTIME-2026-09-10.json)
records the exact bounded validation; private paths and package contents are not
published.

The reviewed PowerShell verifier checks the complete manifest/file inventory
before launching Python with `-I -S -B`. Its fixed `python314._pth` admits only
`Lib`, `DLLs`, `packages` and `policy`; bytecode, extra files and site hooks are
excluded. Both `PYTHON*` and `ESPTOOL_*` environment variables are cleared, and a
pinned capsule-root `esptool.cfg` with an empty section is selected before imports so inherited
configuration cannot silently alter reset/retry behavior. Exact top-level
`PathFinder` origins and existing module-cache entries are checked before imports;
the running worker also rechecks the capsule and loaded origins. Ordinary and
poisoned-environment no-device probes passed parent imports and independently
externally verified Version child launches, followed by exact capsule-file checks.
The poisoned probe included Python and esptool configuration/retry/FPGA variables.
Two focused suites passed 13 cases each:
`tests/host/security_policy_runtime_bundle_tests.py` and
`tests/host/security_policy_operator_tests.py`. The earlier runtime manifest
beginning `7a8e8e4b` is superseded and must not be admitted.

The local full matrix passed its unchanged prefix, then stopped at the old
12-case fixture while these edits were in progress (sentinel exit code 6).
The final-source tail rerun beginning at the runtime suites passed, including all
13 simulator UI tests. This completes segmented local validation; the required CI
check runs the complete final-source matrix afresh before merge. Documentation
checks and 17 tests, 16 scope groups, publication scanning and 291 raw-byte checkout
inputs also pass.

Python's executable, startup-loaded DLLs and effective path configuration are
checked before Python starts. These checks depend on the reviewed PowerShell
verifier and its host PowerShell/.NET/OS trust base. The host executable pin does
not attest that entire host or a compromised operating system, and Python does
not self-attest its own prior startup. Copied dependency RECORD verification and
manifest hashes establish byte integrity against the admitted local inputs, not
upstream publisher signatures or protection from a malicious concurrent local
administrator.

The outer private operator request binds the runtime manifest digest, raw package
file digest and external grant digest. Execute/recover still use the frozen
one-use authority consumption and journal policy. Each ROM child independently
rechecks the runtime, source/package and already-consumed authority, then verifies
its current journal phase, route, payload and permitted span before esptool runs.
No grant issuer or unrestricted ROM-command path is provided. The operator exposes
execute/recover only: the separately bounded first read/reset backup controller
is **not implemented** by this increment. The following backup scope is prepared,
but cannot be called an executable live-preflight path until that adapter is
implemented and validated.

## Live gates remain unfilled

| Gate | Current disposition |
| --- | --- |
| Exact two-device mapping | Pending fresh passive inventory, intended-device confirmation and independent ROM identity/ESP32-S3/16 MB checks; current private routes and identities are not populated here |
| Installed original and boot layout | Pending complete per-device reads against the admitted original/recovery baseline; partition and OTA must match the fixed OT-188 factory layout |
| Full NVS custody | Pending separately authorized acquisition of each complete private NVS span and its exact bytes/hash; no pairing/owner data may enter public evidence |
| Recovery readiness | Pending each device's exact original application, NVS and protected descriptors plus a usable independently verified recovery path |
| Operator launch/runtime | Copied runtime and no-device isolated parent/child probe passed; final-source host tail and unchanged prefix passed, and the exact capsule must be reverified before any later live invocation |
| Physical authority | Pending explicit one-use scope bound to the final runtime, package, current devices and originals; earlier consumed grants are unusable |

The [mandatory porting checklist](../firmware-porting-lessons.md) applies. Reuse
OT-187 source/configuration/build evidence while those exact inputs remain
unchanged. Physical board/setup, USB/reset and restoration gates remain unproved.
Display, battery, GNSS, field range and phone behavior are outside this isolated
evaluation. Cold-power enclosure disassembly remains deferred.

## First physical step: authorized backups and guarded reset

The first proposed device operation is **read/reset preflight only**, separately
scoped from candidate installation. Its bounded operator adapter remains to be
implemented; the current execute/recover interface does not acquire these live
backups. A ROM read can reset or halt the running
application even though it writes no flash. Authorization and the exit plan must
therefore cover transport ownership, entry into ROM and guarded return to the
verified original firmware; do not describe this as passive inventory.

Handle one device at a time. Re-enumerate and verify the intended current route,
identity and geometry before reading these complete spans:

| Region | Offset | Length |
| --- | ---: | ---: |
| Bootloader | 0x0000 | 32,768 bytes |
| Partition sector | 0x8000 | 4,096 bytes |
| OTA selection | 0x9000 | 8,192 bytes |
| Ordinary NVS | 0xd000 | 12,288 bytes |
| Original application and sector tail | 0x10000 | 589,824 bytes |

Verify the installed application and protected boot layout before permitting an
original reset. Retain complete NVS and application bytes privately, with distinct
per-device custody. An unexpected image, layout, identity or incomplete read stops
admission; it does not authorize booting unknown bytes or writing a substitute.
Record the exact failure and retained ROM/recovery state.

NVS acquisition must be coherent while original firmware is held in ROM. Booting
the original may change pairing/owner state and invalidate a prior snapshot for
later restoration. A later trial must re-read and exactly match its admitted
original/NVS immediately before candidate mutation; mismatches require fresh
custody and a newly bound package/authority, not tolerance or automatic erasure.
Whether the authorized preflight holds the device in ROM or returns it to original
firmware must be explicit. This document does not choose an unattended hold or
perform either action.

## Later single nonradio trial and handback

After every gate is satisfied, the execute grant covers A's complete evaluation
and normal restoration before B starts. Recheck current identity and recovery
binding before each mutation. Write only the exact OT-187 candidate padded to the
589,824-byte application span, independently read it back and verify protected
regions/NVS before reset. Use one fresh challenged request and the unchanged
[OT-187 capture contract](OT-187-CAPTURE-PREPARATION.md); do not widen deadlines or
retry a consumed attempt to obtain a pass.

Close and confirm serial ownership before ROM restoration. Restore each touched
device's own complete original application and full NVS, read both back exactly,
and reverify bootloader/partition/OTA preservation before guarded original reset.
No bootloader/partition/OTA write, namespace-only cleanup or whole-flash erase is
permitted. A non-pass A ends the evaluation after its verified restoration; B does
not run. Two matching receipts alone do not establish successful handback.

The durable journal governs uncertainty. An unresolved serial-open intent cannot
be bypassed by creating a new backend or process. Once `original_boot_intent`
records that original firmware may have restarted, an older NVS snapshot must not
be replayed blindly. Separate eligible recovery needs fresh restore-only authority
for the original attempt, still verifies recovery code and originals, and cannot
start another evaluation. Preserve reconciliation states and do not remove their
locks/journals to force progress.

Completion requires recorded per-device outcomes and independent restoration
checks. Report physical entropy/persistence observations only if actually measured;
retain missing checks and failures explicitly. This preparation alone grants no
V1 completion credit, hardware acceptance or website status change.
