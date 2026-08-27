# Decision 0086: freeze the OT-150 mbedTLS/PSA executable/resource bundle

- Date: 2026-08-27
- Status: Accepted
- Scope: OT-005 / OT-150 host-only executable and matched-resource binding

## Decision

Accept OT-150 as the host-only successor that freezes the mbedTLS/PSA
comparison executable and matched-resource evidence before any new device
attempt.

The OT-149 candidate and no-candidate control builds remain immutable
compile-validation snapshots. Their project version was not explicitly pinned,
so their application BIN and ELF identities are historical evidence only and
are not eligible for execution. OT-150 does not relabel, overwrite, or promote
those artifacts.

OT-150 instead requires two initially absent candidate builds and two initially
absent no-candidate control builds with exact
`PROJECT_VER=ot150-mbedtls-psa-v0`, ccache disabled explicitly, and component-
manager network access disabled. Candidate A/B must match byte-for-byte and by
SHA-256, control A/B must match the same way, and candidate/control must retain
the accepted matched configuration, toolchain, harness, instrumentation, and
partition boundary. Only the reviewed candidate-versus-no-candidate linkage may
differ. The canonical preparation and matched-resource result bind the exact
build artifacts, JSON2 reports, and resulting linked-flash/static-RAM values;
this narrative does not duplicate those generated identities.

The signed/zero-capable OTMRAR1 result is admitted as host-only resource
evidence. Signed positive, zero, or negative deltas remain valid outcomes when
they are reproduced from the exact bound ESP-IDF JSON2 reports. This admission
is not a benchmark execution, device result, or candidate selection.

The future execution boundary uses a fresh OT-150 runtime and private-state
namespace. It retains the exact five-operation order, strict 1,015-frame
parser/schema, and START/READY transport boundary accepted by OT-149. Only the
exact candidate application BIN bound by the canonical preparation may become
writable in a later separately authorized attempt, and only at application
offset `0x10000`. The control application, ELF files, linker maps, bootloaders,
partition tables, sdkconfigs, and JSON2 reports are provenance or host evidence
and are never writable payloads.

Any future attempt must restore the exact current OT-147 Trail application,
`opentrail_heltec_v4_bench.bin`, 500,944 bytes, SHA-256
`f2a58414f82eed585ba90bf7671b06c8ebfaa82e8b23f6ac2093de95143b4e0e`.
OT-150 itself grants no attempt. Fresh explicit owner approval and a separately
accepted, non-reusable authority bound to this exact preparation remain
mandatory before endpoint access or any physical action.

## Consequences

- OT-149 remains accepted compile-validation history but supplies no executable
  image for a later physical attempt.
- OT-150 admits only the exact matched-resource result and freezes the inputs
  needed for a later bounded mbedTLS/PSA comparison attempt.
- mbedTLS/PSA remains a five-of-eight, comparison-only, structurally
  nonselectable candidate.
- No device, endpoint, phone, reset, flash, benchmark capture, radio operation,
  candidate or suite selection, Phase 2 completion, field readiness, support,
  compatibility, regulatory, production, end-to-end, or score claim is added.
- V1 progress does not change.
- No immediate public website update is required because public capability,
  readiness, completion percentage, and score remain unchanged; normal batched
  website publication continues.

## Evidence

- [OT-150 host-only evidence](../../tests/hardware/OT-150-2026-08-27.md)
- [Canonical executable/resource preparation](../../tests/benchmarks/crypto/OT-150-OT005-MBEDTLS-PSA-EXECUTABLE-RESOURCE-BUNDLE-PREPARATION-V0.json)
- [Candidate ESP-IDF JSON2 report](../../tests/benchmarks/crypto/OT-150-OT005-MBEDTLS-PSA-CANDIDATE-SIZE-REPORT-V1.json)
- [Control ESP-IDF JSON2 report](../../tests/benchmarks/crypto/OT-150-OT005-MBEDTLS-PSA-CONTROL-SIZE-REPORT-V1.json)
- [Matched-resource result](../../tests/benchmarks/crypto/OT-150-OT005-MATCHED-RESOURCE-RESULT-V1.json)
- Bundle validator: `tools/ot150_mbedtls_psa_bundle.py`

## Next gate

Disclose the exact frozen attempt scope and obtain fresh explicit owner approval
before creating or accepting a one-attempt authority. Until that separate gate
passes, every execution, device, reset, flash, radio, and key/entropy authority
remains false.
