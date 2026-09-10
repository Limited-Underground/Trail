# OT-163 crypto admission and Companion integration gates

Current successor: [composed security evaluation and matched resources](OT-163-SECURITY-EVALUATION-2026-09-10.md). Target wiring and candidate-specific controls below now have build/host evidence; remaining physical and complete policy gates stay open. Earlier assessment paragraphs retain their checkpoint scope.

## Current result and controlling scope

The [completed receipt-console trial](OT-163-RECEIPT-CONSOLE-RUN-2026-09-10.md)
accepts the bounded, role-reversed Noise XK radio benchmark and independent
restoration checks. It does not select a library, instantiate production keys,
freeze Packet V1, or connect the phone Messages screens to authenticated LoRa.

[Decision 0003](../decisions/0003-crypto-benchmark-gate.md),
[Decision 0084](../decisions/0084-reconcile-incomplete-phase-two-before-selection.md)
and the [frozen OT-116 plan](../../tests/benchmarks/crypto/OT-116-OT005-EXECUTABLE-BENCHMARK-PLAN-V1.json)
control measurement and independent admission before a separate selection
decision. The eight gates below are the plan's existing gates, not additional
release requirements invented for this increment. Historical results remain
immutable; a successor reconciliation must bind their exact applicable evidence.

[Decision 0033](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md)
limits V1 to two device-phone pairs and explicitly accepts ownership rollback by
an attacker with physical firmware-writing access. It does not waive ordinary
runtime/restart nonce uniqueness or interruption-safe outbound counters.
[Decision 0103](../decisions/0103-adopt-ot168-v1-factory-reset-and-boot-pairing.md)
supersedes its replacement/pairing clauses: current acceptance uses destructive
factory reset and fresh pairing, not the historical lost-phone transfer flow.

## Evidence classification for the eight named gates

“Not reconciled” means no complete gate admission is established here; it does
not erase narrower accepted results. Reuse and bind existing proofs before
scheduling a new measurement.

| OT-116 gate | Existing evidence and limitation | Completion checkpoint |
| --- | --- | --- |
| Primitive vectors and negative cases | Accepted libsodium eight-operation checkpoint [OT-122](../../tests/benchmarks/crypto/OT-122-OT005-LIBSODIUM-NOISE-RESOURCE-EXECUTION-RECEIPT-V0.json); accepted Monocypher [OT-146](../decisions/0083-record-ot146-ot145-monocypher-execution-success.md) is five-operation comparison evidence. The corrected [mbedTLS/PSA comparison](OT-163-MBEDTLS-COMPARISON-2026-09-10.md) now supplies successful five-operation timing/runtime results on both devices; the historical OT-151 abort remains preserved. | Bind exact candidate vectors, negative cases, operation coverage and available target results. Reuse the completed corrected mbedTLS/PSA comparison; do not convert five-of-eight coverage into selection eligibility. |
| Noise XK independent interoperability | The [independent host proof](../security/CRYPTO_ADMISSION_BATCH_2026-09-10.md) now matches two exact XK transcripts/hash/split-key cases with real scalar primitives and negative cases. A separate adapter successor fixes a real-header nonnull contract violation. | Bind the completed deterministic proof and corrected successor to the evaluation target. Normal library initialization, target composition and full product protocol acceptance remain separate. |
| Invitation, replay, reorder and timeout refusal | [OTSL0/v0](../security/SECURE_LORA_KEY_TRANSPORT_V0.md) has accepted host lifecycle/admission evidence. The new radio trial accepts its bounded benchmark retry/stale behavior, not the complete production invitation flow. | Bind host rejection proofs and exercise the selected target composition's invitation identity/epoch/role, human confirmation, cancellation, restart and replay rules. |
| Entropy and cold-start uniqueness | An additive serialized guard now passes actual-adapter host admission, source-loss and in-flight shutdown tests; target wiring and physical entropy remain open. Deterministic fixture keys do not establish production entropy. | Prove the actual target entropy-ready/absent behavior and accepted concurrency/restart/interruption cases; retain cold-start/brownout items explicitly open where physical evidence is absent or owner-deferred. |
| Temporary-secret wipe and log redaction | Benchmark wipe/refusal checkpoints and bounded privacy-safe receipt tests exist. Host/API wipe evidence is narrower than the full target key lifecycle. | Bind exact wipe/error paths, ensure test RNG/private material cannot enter product composition or ordinary logs, and cover target failure/abort/reset boundaries. |
| Rollback-safe counter interruption | Host counter-lease and KV-composition tests exist. This is not accepted target interrupted-persistence proof. | Bind counter reservation, durable commit/readback and restart/interruption behavior to the chosen storage adapter; prove no key/nonce reuse under the accepted V1 threat boundary. Physical-attacker rollback-proof hardware is not added as a V1 prerequisite. |
| Two-device join, revoke, reset and recovery | Protected BLE/setup and exact benchmark restoration are separate accepted evidence layers. They do not establish LoRa membership/rekey or coherent product delivery. | Exercise the selected security target's two-device lifecycle with current factory-reset semantics and key retirement; later integrate and accept the full phone-to-phone path. |
| License, SBOM and reproducible lock | Candidate source/API/import admissions and reproducible benchmark builds exist. [OT-150 matched resources](../../tests/benchmarks/crypto/OT-150-OT005-MATCHED-RESOURCE-RESULT-V1.json) is admitted host-only mbedTLS/PSA comparison evidence, not a missing result or target execution. | Revalidate applicable exact locks, configuration, license/SBOM inventory, matched resource bindings and private trace custody in one successor corpus; changed targets require their own evidence. |

## Batch 1: comparison results and consolidated corpus

Reconcile the existing accepted local-operation, runtime-resource, matched-size
and new radio receipts using
[the corpus validator](../../tools/crypto_phase_two_reconciliation.py) and
[its tests](../../tests/host/crypto_phase_two_reconciliation_tests.py) as the
historical baseline. Preserve that frozen reconciliation; any new admission
belongs in an additive successor with exact raw/canonical bindings.

Reuse [signed matched-resource accounting](../../tools/crypto_matched_resource_accounting.py)
and [its tests](../../tests/host/crypto_matched_resource_accounting_tests.py).
The [corrected mbedTLS pair](../security/CRYPTO_ADMISSION_BATCH_2026-09-10.md) now has its own matched resource admission. OT-150 remains valid for its original exact pair. Libsodium and corrected Monocypher still require candidate-specific harness-preserving controls; raw application sizes are not comparable rankings.

The corrected physical mbedTLS comparison and canonical capture audit are complete.
Reuse those exact results. Historical libsodium/Monocypher raw capture custody is
still unestablished; retain the scoped search and parsed-receipt evidence without
reconstructing raw bytes. Locate exact originals or prepare a separately authorized
successor capture with retention. The current
[batch assessment](../security/CRYPTO_ADMISSION_BATCH_2026-09-10.md) binds these
results and keeps selection withheld while the remaining gates are open.

## Batch 2: security target proofs and explicit selection

Close the remaining named gate proofs against the exact selected-for-evaluation
target. Reuse the [benchmark-only Noise adapter](../../tests/benchmarks/crypto/adapters/libsodium_noise_xk_v0/noise_xk_libsodium.c)
and [composition tests](../../tests/host/libsodium_noise_xk_composition_tests.cpp)
as historical interface/order evidence. Use the independently proved
[nonnull successor](../../tests/benchmarks/crypto/independent_noise_xk/nonnull_adapter/noise_xk_libsodium.c)
for evaluation integration; do not promote deterministic benchmark key handling
into production. Reuse [secure-random tests](../../tests/host/secure_random_tests.cpp),
[counter-lease tests](../../tests/host/outbound_counter_lease_store_tests.cpp),
[KV-composition tests](../../tests/host/outbound_counter_kv_composition_tests.cpp),
and the [secure-LoRa contract admission suite](../../tests/host/secure_lora_contract_admission_tests.py).

The [counter component](../../firmware/components/persistence/include/opentrail/outbound_counter_lease_store.hpp),
[nonce component](../../firmware/components/security/include/opentrail/aead_nonce.hpp)
and [traffic-key context](../../firmware/components/security/include/opentrail/traffic_key_context.hpp)
provide bounded reusable policy; each still needs correct target/storage/crypto
composition. Where a named gate needs evaluation firmware, keep it explicitly
benchmark/test-only until the independent admission and selection decision.

The checkpoint is OT-116 Phase 3 admission of the complete applicable corpus,
then a separate explicit decision naming the library, suite, handshake/KDF and
wire instantiation. Libsodium 1.0.22 remains an evidence-backed recommendation
until that decision; comparison candidates remain nonselectable under their
current operation coverage. Do not require a new owner preference question for
a routine technical choice already authorized; record the selection explicitly
and do not infer it from a successful benchmark.

## Batch 3: product integration and coherent two-pair acceptance

Compose the selected adapter with invitation/epoch activation, durable counters,
replay rejection and protected acknowledgements under
[Decision 0035](../decisions/0035-host-tested-secure-lora-key-transport-contract.md).
Review the existing [packet codec](../../firmware/components/protocol/include/opentrail/packet_codec.hpp)
and [tests](../../tests/host/packet_codec_tests.cpp) for reusable bounds only;
existing packet traffic does not acquire authentication merely by using that codec.
Wire the actual Companion target and Android Messages flow to the accepted
pairwise-unicast delivery contract. Keep Trail Server, repeaters and group
broadcast outside V1's required field path.

After host composition and affected target/Android builds pass, freeze one
candidate-bound two-pair physical acceptance: current fresh pairing/reset rules,
reconnect and cross-pair denial, bidirectional phone message/reply delivery,
recipient isolation, duplicate/corrupt/unauthenticated rejection, BLE interruption,
bounded LoRa retry/failure/recovery and restart coherence. Apply the existing
signed-artifact and release gates separately; a benchmark restoration or OLED
readback does not substitute for the product run.

## Owner-dependent boundaries and accounting

Cold-power work requiring enclosure disassembly remains explicitly deferred by
the owner. Continue independent host/comparison/integration work while keeping
that untested gate visible. Physical trial authority remains bound to its exact
current scope and restoration plan.

Irreversible eFuse, Secure Boot, release flash-encryption or sacrificial-device
work requires its separate owner-approved recovery procedure. It is not an
implicit default V1 hardening task, and physical-attacker ownership rollback
resistance remains outside the accepted V1 claim.

This plan creates no execution grant, hardware result, selection, publication
permission or percentage change. Update V1 completion only when newly accepted
milestone evidence warrants it; successful preparation alone earns no credit.
