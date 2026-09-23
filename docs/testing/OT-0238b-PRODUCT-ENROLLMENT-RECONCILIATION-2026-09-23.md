# OT-0238b product enrollment reconciliation

OBSERVED 2026-09-23. Approved revision1 investigation. Canonical project C:/lu/OpenTrail; active worktree .private/ot177-publication, codex/ot236-libsodium-capture at c31f7196f4f947e632630e6fde04570a21edcf14. No production implementation change or hardware access.

## Result

The corrected OT-0247d evaluation trial is owner-accepted. It proves the complete bounded evaluation exchange, not the product enrollment bootstrap. OT-0238b cannot honestly meet its product acceptance by relabeling the evaluation owner or adding an injected Boolean/trust interface. Product bootstrap and identity semantics require explicit reconciliation first.

| Boundary | Accepted product requirement | Current composition | Disposition |
|---|---|---|---|
| First peer trust | OTSL0/v0: phone requests and parsed QR do not supply cryptographic authority; exact identities and local confirmation precede activation | EnrolledBenchSession INIT accepts operator-supplied signer/group/role; BEGIN supplies signed invitation | Bench-only trust; no authenticated product issuer binding |
| Restart/storage | Retained public membership cannot resume old traffic keys | SessionGenerationAllocator commits a fresh generation; stable membership/evidence namespaces; actual NVS-session test completes generation2 and preserves generation1 | Reuse; do not reimplement historical OT239 storage gap |
| Rekey identity | Decision0035: retained exact identities, epoch advances exactly one, fresh key material | EnrolledPeerEndpoint begin requires both identities different from prior peers, and accepts any greater epoch | Incompatible semantics; do not silently change accepted V1 contract |
| Durable activation | Exact membership/evidence commit/readback and peer activation precede traffic; uncertainty fails closed | Existing endpoint owns signed public evidence, membership and activation gates | Reuse components after product composition is defined |
| Phone ownership | BLE ownership and LoRa membership independent; no implicit membership rotation on phone change | Evaluation has local operator pins, no product phone-request adapter | Phone authorization alone must not become LoRa trust |

Sources: [accepted contract](../security/SECURE_LORA_KEY_TRANSPORT_V0.md), [Decision0035](../decisions/0035-host-tested-secure-lora-key-transport-contract.md), [OT237 assessment](../security/OT-237-CURRENT-ADMISSION-2026-09-16.md), [OT238](OT-238-ENROLLMENT-MEMBERSHIP-2026-09-16.md), [OT239](OT-239-PERSISTED-ENROLLMENT-2026-09-16.md), [OT240](OT-240-INTEGRATED-ENROLLMENT-2026-09-16.md).

Code anchors: firmware/components/security_evaluation/include/opentrail/enrolled_bench_session.hpp INIT/BEGIN; enrolled_peer_endpoint.hpp begin/initialize/activate; session_generation_storage.hpp allocator and generation views; tests/host/enrolled_nvs_session_tests.cpp generation2 activation and unchanged generation1 checks. No current tests were rerun merely to restate these source observations; historical passing evidence retains its original boundary.

## Proposed correction to the task plan

Prepare and independently review the concrete product enrollment contract before implementation. Preserve existing V1 scope, local matching-code confirmation, on-Heltec private keys, fresh durable session allocation, retained identity on rekey and exact epoch+1. Define:

1. Device-owned identity/issuer creation and persistence, and how the first peer obtains authenticated trust. Distinguish untrusted invitation presentation from the event that establishes trust. Reconcile existing QR/PIN/join-window plans without choosing final cryptography or wire bytes prematurely.
2. Separate long-lived identity from per-attempt handshake/traffic material; explicit signer/group/role authority and one-use invitation consumption. An authenticated phone can request a workflow but cannot manufacture peer authentication.
3. Exact state/ownership/clock sequence through first join, cancellation, commit/readback failure, restart, rekey, revocation and reset. Retained public records alone never release traffic keys.
4. A source-bound implementation map reusing accepted storage and activation pieces; negative tests for caller-selected authority, wrong identity, epoch skips, stale invitation, authority/context changes, torn writes and restart. Trace actual components through final output, not only a fake authority seam.
5. Independent security/contract review and owner acceptance of the concrete contract before dependent product implementation. Physical execution, final suite/wire selection, signing and publication remain separate.

This is a proposed next engineering step, not an accepted new product design. It does not ask the owner to choose cryptographic algorithms. The present task remains incomplete and blocked on this prerequisite; no product success, V1 credit or public status change is claimed.

## Concrete design proposal

The owner authorized design preparation. The [reviewed draft](../security/PRODUCT_ENROLLMENT_REKEY_DRAFT_V1.md) now specifies the first-peer identity-verification workflow, fresh proof binding, state/ownership flow, retained-identity/exact-epoch rekey, failure containment and implementation/test sequence. Owner design acceptance remains pending. Interrupted-operation recovery stays a named separate design/implementation gate; this is not OT-0238b completion.
