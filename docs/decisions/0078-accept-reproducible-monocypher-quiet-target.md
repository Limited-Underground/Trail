# Decision 0078: accept the reproducible Monocypher quiet target

- Date: 2026-08-25
- Status: Accepted
- Scope: OT-005 / OT-139 host-only quiet-target build

## Decision

Accept a separate ESP-IDF target, `monocypher_ot139_quiet`, that compiles the
frozen OT-129 Monocypher application and START/READY control sources by
reference while changing only the target-local configuration boundary.

The resolved configuration selects no primary or secondary ESP-IDF console,
sets bootloader, default application, and maximum application logging to none,
retains the direct USB Serial/JTAG driver, enables reproducible application
builds, and leaves the ESP32-S3 ROM logging policy at its normal always-on
setting. ROM output before the application starts is therefore neither
suppressed nor claimed absent.

Two fresh, initially absent, cache-disabled builds under exact ESP-IDF commit
`7101770dc6db2667b3c477cc31365dd1acd6db4e` produced identical application BIN,
ELF, linker map, bootloader BIN, partition-table BIN, and generated sdkconfig
artifacts. The canonical evidence binds the exact six-file tuple, resolved
configuration, toolchain, reused source inputs, and preserved transport/parser
boundaries.

## Preserved boundaries

- Frozen OT-129, OT-132, and OT-135 runners and protocol sources are unchanged.
- The cumulative 512-byte pre-READY bound, fixed deadlines, exact `READY`,
  frame-before-`READY` rejection, post-`READY` strictness, privacy-safe
  diagnostics, and unchanged real 1,014-frame parser remain authoritative.
- No eFuse or GPIO-based ROM-log suppression is selected or authorized.
- The quiet target is a host-built successor target only. It is not yet an
  immutable executable bundle and is not authorized for device execution.

## Consequences

- OT-139 closes the host-only quiet-target build gate identified by OT-138.
- The next gate is a separate immutable executable binding that includes the
  OT-139 artifact tuple, unchanged runner/parser, restoration-safe coordinator,
  concrete adapter, and exact Trail restoration application, followed by a
  fresh explicit non-reusable one-attempt authority.
- No hardware, phone, flash, benchmark, radio, result, selection, Phase 2
  completion, support, regulatory, production, end-to-end, or score claim is
  added.
- The OT-136 authority remains consumed and non-reusable. No current Monocypher
  hardware attempt is authorized.
- V1 remains exact 43.75%, displayed 44%; the historical baseline remains exact
  31.75%, displayed 32%.
- Public website synchronization is required because the accepted latest
  increment and next gate change, even though no percentage or physical claim
  changes.

## Evidence

- [OT-139 host-only evidence](../../tests/hardware/OT-139-2026-08-25.md)
- [Canonical two-build evidence](../../tests/benchmarks/crypto/OT-139-OT005-MONOCYPHER-QUIET-TARGET-BUILD-EVIDENCE-V0.json)
- Build tool: `tools/Build-Ot139MonocypherQuietTarget.ps1`
- Evidence validator: `tools/ot139_monocypher_quiet_target_evidence.py`
- Focused tests: `tests/host/ot139_monocypher_quiet_target_tests.py`
