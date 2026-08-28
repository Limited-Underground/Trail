# Decision 0093: freeze the OT-157 reset-aware Noise XK executable bundle

- **Status:** Accepted
- **Date:** 2026-08-28
- **Scope:** OT-005 / OT-157 host-only successor bundle after OT-156

## Context

OT-155 consumed the only OT-154 authority on a restored fail-closed abort. OT-156
then accepted a separate reset-aware runner and transport, but intentionally did
not create an executable coordinator/adapter binding or replacement authority.
The immutable OT-153 coordinator also used the consumed OT-153 private namespace
and could not preserve the OT-156 allowlisted failure stage in its terminal
receipt.

The firmware, radio protocol, benchmark result contract, build tuple, and Trail
restoration application did not change. Duplicating or rebuilding those accepted
bytes would add risk without changing the correction boundary.

## Decision

1. Bind the exact OT-156 runner and reconnectable runtime into a new OT-157
   coordinator and concrete adapter. Hash drift in every reused or successor
   module fails closed.
2. Reuse, without modification, the exact OT-153 firmware/source/build lineage:
   application 296,640 bytes, bootloader 22,480 bytes, partition table 3,072
   bytes, and the recorded ELF. The two accepted build tuples remain identical.
3. Reuse the exact 500,944-byte Trail restoration application at offset
   `0x10000`, with independent restore write, readback, and reset for every
   benchmark-touched node on success, failure, or `BaseException`.
4. Give OT-157 separate journal, execution-receipt, and recovery-receipt paths
   and schemas. The exact private root is created with a bounded non-recursive
   operation when absent, then the live inherited paths are validated for
   identity and reparse/symlink ancestry before use. Consumed OT-153/OT-154
   state cannot authorize or resume OT-157. Resolution, creation, reparse, and
   identity errors all fail closed before authority validation or device I/O.
5. Preserve OT-156 ordering: both `RESTART` acknowledgements precede either
   reopen; old handles and queues are discarded; each reopen is bounded; and
   both complete post-reboot contracts precede every radio verb.
6. Persist only one of the sixteen allowlisted OT-156 failure stages with the
   generic `radio_run_failed` code. Unknown or private exception text, ports,
   paths, device identifiers, and raw serial data remain absent.
7. Preserve the immutable OT-153 successful command sequence, result bytes,
   14 transmissions, 736 radio-payload bytes, and 1,447,424 microseconds of
   theoretical airtime.
8. OT-157 grants no device, serial, reset, flash, radio, benchmark, key/entropy,
   or execution authority. OT-158 is the separate fresh explicit non-reusable
   one-attempt authority gate.

## Consequences

The corrected path is an immutable executable/restoration preparation that
can be reviewed independently before any authority. It neither executes nor
proves a physical OT-155 root cause. No result, candidate/library/suite,
handshake/KDF, Packet-v1, Phase 2/3 completion, readiness, support, production,
regulatory, or score claim changes.

V1 remains exact 43.75% / displayed 44%; the historical baseline remains exact
31.75% / displayed 32%. This internal bundle correction does not require a
public website status update.

## Validation state

- OT-157 focused bundle/coordinator/adapter suites: 23/23 passed.
- OT-156 reset-aware runtime/runner suites: 17/17 passed.
- Frozen OT-153 target/runner/bundle/coordinator/adapter chain: 75/75 passed.
- Authoritative raw-byte audit passed with all registered OT-157 files exact.
- Complete Windows Host matrix passed locally with exit code 0.

## Evidence

- [OT-157 host-only evidence](../../tests/hardware/OT-157-2026-08-28.md)
- [Canonical OT-157 preparation](../../tests/benchmarks/crypto/OT-157-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0.json)
- Bundle validator: `tools/ot157_noise_xk_radio_bundle.py`
- Coordinator: `tools/ot157_noise_xk_radio_coordinator.py`
- Hardware adapter: `tools/ot157_noise_xk_radio_hardware_adapter.py`
