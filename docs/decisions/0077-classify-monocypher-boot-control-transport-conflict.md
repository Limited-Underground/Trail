# Decision 0077: classify the Monocypher boot/control transport conflict

- Date: 2026-08-25
- Status: Accepted
- Scope: OT-005 / OT-138 host-only boot/control investigation

## Decision

Accept the host-only classification of the OT-137 capture boundary. A fabricated
stream reproduces the complete public diagnostic shape: one START write, two
immediately available 512-byte reads, 1,024 observed bytes, 11 complete opaque
pre-`READY` records, no empty read, no frame, and fail-closed
`preamble_invalid`. Alternative chunking of the same fabricated byte sequence
reaches the same failure classification.

The frozen OT-135 runner sends START before its first read and charges every
subsequently returned complete and partial pre-`READY` byte to the single
512-byte protocol-preamble budget. With immediately queued input, no sleep
occurs and the 250 ms START retry cannot become due before the budget is
crossed. The runner can therefore abort before the benchmark application's
control driver has accepted a START.

The exact accepted generated sdkconfig is publicly pinned at 106,913 bytes and
SHA-256 `4260688e6323cfda7a50912b4cc9c77a7b6f5133b6970b543bf0ce822ffd023f`.
Its accepted inputs select the USB Serial/JTAG console and INFO logging, while
the frozen benchmark application suppresses runtime logs only in `app_main`
before installing the direct USB Serial/JTAG driver and waiting for START.
This proves a structural transport conflict: startup console output and the
control protocol can use the same endpoint, and the host runner does not
separate already queued startup bytes from its post-START budget.

The actual OT-137 bytes were deliberately not retained. Their contents remain
unconfirmed and are not reconstructed or inferred by this decision.

## Successor direction

Choose a new, separately reviewed quiet-target configuration as the next
successor direction. It must isolate the ESP-IDF console from the direct USB
Serial/JTAG protocol and disable bootloader/default application logging while
retaining the direct USB Serial/JTAG driver. The generated configuration must
prove the resolved settings, and two fresh absent-directory builds must produce
identical BIN, ELF, map, and sdkconfig artifacts before any executable binding
is proposed.

Do not modify the frozen OT-129, OT-132, or OT-135 runners. Do not increase,
bypass, reset, or silently drain around the 512-byte post-START preamble budget.
Exact `READY`, frame-before-`READY` rejection, duplicate/post-`READY`
strictness, fixed deadlines, privacy-safe diagnostics, and the strict real
1,014-frame parser remain unchanged.

## Consequences

- OT-138 is investigation and classification only. It creates no firmware
  successor, executable bundle, adapter, coordinator, authority, or attempt.
- No hardware, phone, flash, benchmark, radio, result, selection, Phase 2
  completion, support, regulatory, production, end-to-end, or score claim is
  added.
- The OT-136 authority remains consumed and non-reusable. No Monocypher hardware
  attempt is authorized.
- A later task may build and validate the quiet target host-only. Any device
  access still requires a later immutable executable binding and fresh explicit
  non-reusable one-attempt authority.
- V1 remains exact 43.75%, displayed 44%; the historical baseline remains exact
  31.75%, displayed 32%.
- Public website synchronization is required because the accepted latest
  finding and next gate change, even though no percentage or physical claim
  changes.

## Evidence

- [OT-138 host-only evidence](../../tests/hardware/OT-138-2026-08-25.md)
- Investigation module: `tools/ot138_monocypher_boot_control_investigation.py`
- Adversarial tests: `tests/host/ot138_monocypher_boot_control_investigation_tests.py`

