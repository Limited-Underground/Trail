# Decision 0073: freeze Windows raw-evidence checkout bytes

- Date: 2026-08-24
- Status: Accepted
- Scope: Host validation / OT-134 deterministic checkout correction

## Decision

Treat every repository file whose exact raw SHA-256 is an accepted contract input as byte-preserved evidence. Pin deterministic LF or CRLF policy wherever the authoritative bytes use one newline form, and preserve the exact Git blob for deliberately mixed-EOL inputs. The policy covers all known checkout-sensitive inputs, including the direct crypto JSON set, nested evidence, firmware inputs, test evidence, and host tools.

Add one non-fail-fast audit that reports existence, length, actual and expected SHA-256, EOL form, BOM, final newline, and effective Git attributes for every registered input. Keep every maximum-size and digest check strict. Do not derive accepted hashes from the checkout or generated output.

## Basis

The last green Host validation was OT-092 run 210. OT-093 run 211 was the first red run, but it failed the older OTCBL0 firmware-input manifest check. The current OTCAI failure signature first appeared when OT-098 introduced and executed that validator in run 217.

The OT-098 artifact exists, is nonempty, and is below the limit: the authoritative LF blob is 5,454 bytes with SHA-256 `b7be03e305c6253e10f69f624132a736cce5aea3f559760cde4f948ae79abad6`, exactly matching the pinned digest, while an unpinned Windows `core.autocrlf=true` checkout produces 5,494 bytes and SHA-256 `058d793018496c0a512059fa6b1d838f2c08d54bd2440c802ab8f8ff64e3e909`. `MAX_BYTES` is 131,072. The deterministic failure was checkout newline conversion, not missing data, oversize data, a regenerated artifact, or a stale canonical digest.

## Consequences

- Fresh Windows checkouts reproduce all 69 registered raw-bound inputs, including canonical LF, authoritative CRLF, and exact mixed-EOL evidence.
- Digest enforcement is not removed, relaxed, or derived from generated output.
- No firmware, hardware, phone, product capability, V1 percentage, or public website status changes.

## Evidence

- [OT-134 host-validation correction evidence](../../tests/hardware/OT-134-2026-08-24.md)
- `.gitattributes`
- `tests/host/crypto_candidate_acquisition_inspection_tests.py`
- `tests/host/crypto_libsodium_managed_import_tests.py`
- `tests/host/crypto_libsodium_source_lock_admission_tests.py`
- `tests/host/raw_byte_checkout_policy_tests.py`
