# Decision 0092: accept the OT-156 reset-aware Noise XK radio host correction

- **Status:** Accepted
- **Date:** 2026-08-27
- **Scope:** OT-005 / OT-156 host-only successor to the consumed OT-153/154 execution path

## Context

OT-155 recorded one exactly restored `radio_run_failed` abort. Its frozen public
boundary intentionally collapsed endpoint-open, runner, and result-validation
exceptions, so that evidence could not identify the exact failed substage or
prove a host, USB, firmware, or physical-radio root cause.

The host review found a deterministic lifecycle mismatch worth correcting: the
frozen OT-153 runner accepted each firmware `RESTART` receipt and then expected
the reboot receipts on the same serial handle. A Windows USB Serial/JTAG handle
may become invalid or be re-enumerated across `esp_restart()`. Adversarial host
simulation reproduces that invalidated-handle failure in the frozen runner. It
does not retroactively prove that this was the physical OT-155 root cause.

## Decision

1. Preserve every OT-153, OT-154, and OT-155 source, executable, authority, and
   receipt byte as immutable history.
2. Add a reset-aware successor runner that validates both anonymous nodes'
   `RESTART` acknowledgements before reopening either endpoint.
3. Close and discard each obsolete handle, wait 150 milliseconds for firmware
   restart settling, then retry a fresh open every 250 milliseconds for at most
   15 seconds. Initial endpoint open remains separately bounded to 10 seconds.
4. Set DTR and RTS false and restore the private endpoint name before every
   fresh serial open. Old queued receipts cannot cross into the new handle.
5. Require both nodes to pass the exact stale-self-test, boot, profile, status,
   command-list, and explicit profile-echo contract before any radio verb.
6. Reuse the frozen OT-153 parser, protocol operations, result construction,
   validation, flash, readback, and reset behavior. A successful fixed-token
   public result must remain byte-identical to the frozen happy path, including
   exact command order and count.
7. Collapse successor failures to one of sixteen allowlisted privacy-safe stage
   codes. Do not expose private endpoints, paths, serial or backend text, raw
   receipts, or nested exception context.
8. Treat OT-156 as host-only correction evidence. It grants no device access,
   radio attempt, authority, selection, Phase 2 completion, or continuation.

## Alternatives rejected

- Reuse the same handle and add a longer read timeout: rejected because an
  invalidated handle cannot be made fresh by waiting longer.
- Reconnect transparently inside generic receipt parsing: rejected because it
  could blur the reset boundary or admit stale pre-reset data.
- Modify the OT-153 runner or the consumed OT-154 bundle: rejected because those
  exact bytes are historical authority inputs.
- Retry the physical attempt under OT-154: rejected because that authority was
  consumed by OT-155 and is non-reusable.
- Claim OT-156 proves the OT-155 root cause: rejected because the physical abort
  retained no exact safe substage.

## Consequences

OT-156 resolves the host-side restart lifecycle contract in a separate,
adversarially tested successor. A later task must freeze a fresh executable and
restoration bundle around the new runner/runtime plus successor coordinator and
adapter bindings. A still-later task must separately accept fresh explicit
non-reusable authority before another physical attempt.

No firmware was rebuilt, no device or phone was used, and no flash, reset, radio
transmission, cryptographic measurement, or physical observation occurred. No
candidate/library/suite/handshake/KDF/Packet-v1 selection, Phase 2 completion,
readiness, support, release, regulatory, or score claim changes. V1 remains
exact 43.75% and displayed 44%; the historical baseline remains exact 31.75%
and displayed 32%. This internal host correction does not require a public
website status update.

## Evidence

- [OT-156 host-only evidence](../../tests/hardware/OT-156-2026-08-27.md)
- Successor runner: `tools/ot156_noise_xk_radio_runner.py`
- Reconnectable runtime: `tools/ot156_noise_xk_radio_runtime.py`
- Runner tests: `tests/host/ot156_noise_xk_radio_runner_tests.py`
- Runtime tests: `tests/host/ot156_noise_xk_radio_runtime_tests.py`
