# Decision 0097: freeze the OT-161 corrected Noise XK executable bundle

- **Status:** Accepted
- **Date:** 2026-08-28
- **Scope:** OT-005 / OT-161 host-only corrected bundle after OT-160

## Context

OT-159 recorded a deterministic pre-consumption blockage in the immutable
OT-157 composition. OT-160 then accepted a separate corrected coordinator and
adapter: source locks use `_source_sha256(Path)`, while inherited application
readback reaches the frozen OT-153 `_sha256(bytes)` helper. OT-160 intentionally
created neither an immutable bundle preparation nor execution authority.

The OT-158 authority remains unused and unconsumed, but it is permanently bound
to the defective OT-157 coordinator and adapter bytes. It cannot be transferred,
reinterpreted, or reused for the corrected composition.

## Decision

1. Freeze the already accepted exact OT-160 coordinator and concrete adapter;
   do not clone or rewrite either module under an OT-161 runtime name.
2. Bind those corrected modules to the exact OT-156 reset-aware runner and
   reconnectable runtime. Hash drift in every reused module fails closed.
3. Reuse, without modification or rebuild, the exact OT-153 preparation,
   reproduced firmware tuple, 296,640-byte benchmark application, and exact
   500,944-byte Trail restoration application at offset `0x10000`.
4. Record the binding in one canonical `OT161NXBP0` preparation. The preparation
   grants no authority and keeps every device, serial, reset, flash, radio,
   benchmark, key/entropy, selection, Packet-v1, and score permission false.
5. Retain the accepted OT-160 private filenames and `OT160NXJ0` /
   `OT160NXCR0` schemas. OT-161 introduces no journal, execution-receipt,
   recovery-receipt, coordinator, adapter, or runtime-private namespace.
6. Preserve independent unconditional restoration, all sixteen allowlisted
   OT-156 failure stages, both post-restart contracts before any radio verb,
   and the exact successful contract of 14 transmissions, 736 radio-payload
   bytes, and 1,447,424 microseconds theoretical airtime.
7. Treat this as host-only immutable-bundle evidence. No device, phone,
   endpoint, physical read/reset, flash, radio, journal, receipt, benchmark,
   or cryptographic measurement occurs.
8. Require OT-162 to separately accept fresh explicit non-reusable authority
   before any corrected hardware execution.

## Exact inherited identities

- OT-160 coordinator: 7,264 bytes, SHA-256
  `444528fd341b3d55f3a5b3224b217620e1b37e3c7960d224aefbe01d9953a02d`.
- OT-160 adapter: 3,952 bytes, SHA-256
  `24d75806cdf7ae28c47fe427cac12a7ef3564d76d68a926ad58bd610c9e8f4b9`.
- OT-156 runner: 11,099 bytes, SHA-256
  `81d0a329d34c20e76362b9a3f07221b77b77f02189dd06660119a02ac1700244`.
- OT-156 runtime: 6,588 bytes, SHA-256
  `bcdb1a772971aa665be699c2699a2b69625c4e8bd2352abd21811be0a4295dd8`.
- OT-153 preparation: 7,496 bytes, raw SHA-256
  `84d7ba8c601f59115fe5d7c74275dc8ccba807a10f87a14ee5afbbe40b3a184b`.
- Trail restoration application: 500,944 bytes, SHA-256
  `f2a58414f82eed585ba90bf7671b06c8ebfaa82e8b23f6ac2093de95143b4e0e`.

## Consequences

The corrected executable/restoration composition can be reviewed as one exact
bundle before any new authority. OT-157 through OT-160 remain immutable history;
OT-161 does not relabel their evidence or imply a physical result.

No candidate/library/suite, handshake/KDF, Packet-v1, Phase 2/3 completion,
readiness, support, production, regulatory, or score claim changes. V1 remains
exact 43.75% / displayed 44%; the historical baseline remains exact 31.75% /
displayed 32%. This internal bundle freeze requires no public website update.

## Validation state

- OT-161 focused bundle suite: 11/11 passed.
- Focused OT-153 bundle, OT-157 bundle, OT-160 coordinator/adapter, and OT-161
  bundle chain: 46/46 passed.
- Authoritative raw-byte audit: 269/269 passed.
- Complete Windows Host matrix: passed locally with exit 0.

## Exact OT-161 artifact identities

- Canonical preparation: 6,113 bytes, raw SHA-256
  `942f7bda82273e8d06901827934eac6dc2c30ac3135ba614c2067eecb8cb171c`,
  canonical payload SHA-256
  `0364615ab9d1129f4b3d83e0ea34d66da0b6e4a8a3070646d277984881388e9f`.
- Bundle validator: 16,818 bytes, SHA-256
  `71668a4d9e7050b10a8aa39cde9caa9078d50313a1ced150a43d4979e00a72a5`.
- Bundle tests: 17,900 bytes, SHA-256
  `ba5bfe8396dd25e0c4c2543c4705156aa9b5f4d5f1d18bd327d291dcd44fd77b`.

## Evidence

- [OT-161 host-only evidence](../../tests/hardware/OT-161-2026-08-28.md)
- [Canonical OT-161 preparation](../../tests/benchmarks/crypto/OT-161-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0.json)
- Bundle validator: `tools/ot161_noise_xk_radio_bundle.py`
- Bundle tests: `tests/host/ot161_noise_xk_radio_bundle_tests.py`
