# Decision 0062: Monocypher comparison benchmark preparation

Status: Accepted

## Decision

Accept the computer-only OT-123 preparation checkpoint for the admitted
Monocypher 4.0.3 comparison surface. The target exposes exactly five operations:
Ed25519 sign/verify, X25519, and IETF ChaCha20-Poly1305 encrypt/decrypt. SHA-256,
HKDF-SHA256, and Noise XK remain unavailable; Monocypher remains structurally
nonselectable. The mandatory gate includes official
[RFC 8032 Test 1](https://www.rfc-editor.org/rfc/rfc8032#section-7.1) Ed25519
and [RFC 8439 Section 2.8.2](https://www.rfc-editor.org/rfc/rfc8439#section-2.8.2)
ChaCha20-Poly1305 known-answer tests plus explicit rejection of an all-zero
low-order X25519 shared result.

Two fresh, initially absent, cache-disabled and component-manager-network-
disabled ESP-IDF 6.0.2 builds reproduced the exact accepted sdkconfig with zero
compiler warnings and identical BIN, ELF, and linker-map tuples. The 186,640-byte
application image has SHA-256
`5e075fb791a658546fca714fc60de095ecbf14f7c443f414d3ac8642965a3b64`.
The strict frame contract expects 1,014 records and the fail-closed runner pins
that image digest, the existing bounded Phase 2 authority, application-only
offset `0x10000`, the exact Trail restore image, the proven 115,200-baud
`--no-stub` ROM path, and a fixed private recovery journal. Before that journal or any write exists,
the runner reads back and hash-verifies the exact installed Trail application
on both nodes. It also binds the exact OT-122 continuation receipt, and its
supported command surface cannot select another journal or benchmark digest.

Freeze `OTMRAC0/v0` before reporting linked-flash or static-RAM deltas. The
candidate absolute ESP-IDF JSON2 report records `total_size=186516` and
`.data + .bss + TLS = 50973` bytes, but those are not candidate deltas. Exact deltas
require two fresh candidate builds and two fresh structurally matched
no-candidate control builds. BIN/ELF/map file lengths, archives, symbol anchors,
the restored Trail application, and the historical OT-093 full product are
explicitly invalid controls. Because that matched control has not been built,
both resource deltas remain null and unadmitted. Static RAM includes DIRAM
.data, .bss, and .noinit plus TLS .tdata/.tbss. The current OTCBR0 validator
rejects zero or negative resource values, so a signed-delta-capable successor
schema and validator are required before either delta can be admitted.

No device was accessed or flashed, no benchmark or cryptographic operation was
executed on hardware, no radio was used, no candidate or suite was selected,
Phase 2 remains incomplete, and no support, compatibility, regulatory,
production, secure-LoRa, end-to-end, or score claim is added.

## Evidence

- [OT-123 preparation record](../../tests/benchmarks/crypto/OT-123-OT005-MONOCYPHER-COMPARISON-PREPARATION-V0.json)
- [Reproducible build recipe](../../tests/benchmarks/crypto/OT-123-OT005-MONOCYPHER-BUILD-RECIPE-V0.json)
- [Matched resource-accounting contract](../../tests/benchmarks/crypto/OT-123-OT005-MATCHED-RESOURCE-ACCOUNTING-CONTRACT-V0.json)
- [Candidate JSON2 size report](../../tests/benchmarks/crypto/OT-123-OT005-MONOCYPHER-CANDIDATE-SIZE-REPORT-V0.json)
- [OT-123 evidence note](../../tests/hardware/OT-123-2026-08-23.md)
