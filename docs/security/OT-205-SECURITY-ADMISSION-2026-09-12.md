# OT-205 Security admission review

2026-09-12. Review complete; Phase 3 admission and crypto selection remain withheld.
This additive assessment reconciles the eight OT-116 gates against the exact OT-203
candidate executed successfully on both boards in OT-204. Earlier reports retain
their historical scope. No product behavior or hardware changed.

## Candidate and evidence

`heltec_v4_security_receipt_sync`, `ot203-receipt-sync-v1`, 440480 bytes, SHA256
`d47b09dae0aa1ae878769410a5daaabdac07af2d4c95c9f9d3436a1e03321cee`.
Current verification matched 44 firmware source pins, 23 operator source pins,
731 library files and 26 retained admission-batch raw bindings. The OT-204 public
report matches its private independent audit. Existing behavioral test results
were reused; no new behavioral tests or builds were run.

The [machine-readable assessment](../../tests/benchmarks/crypto/OT-205-SECURITY-ADMISSION-REVIEW-2026-09-12.json)
pins the controlling plan, source admission, corpus, resource controls and target evidence.
See [policy evaluation](../testing/OT-187-SECURITY-POLICY-EVALUATION-2026-09-10.md)
and [physical confirmation](../testing/OT-204-RECEIPT-TRIAL-2026-09-12.md).

## Gate dispositions

Accepted means only the explicitly bounded evidence below. Partial means useful
accepted evidence exists but the complete gate remains open.

| Gate | Disposition | Accepted evidence | Remaining boundary |
| --- | --- | --- | --- |
| primitive_vectors_and_negative_cases | partial | Eight-operation libsodium primitive evidence and actual-adapter negative/host tests are retained. Corrected comparison candidates remain five-operation partial candidates. | Complete required primitive/negative-case mapping for the final selected composition; benchmark coverage does not select a product suite. |
| noise_xk_independent_interoperability | accepted | Bounded independent host interoperability: two exact XK vectors, three messages, transcript hash and split keys, plus actual target adapter successor proofs. OT204 executes that adapter on target. | Acceptance is limited to the tested suite/vectors and evaluation adapter; full Noise conformance and product interoperability are not claimed. |
| invitation_replay_reorder_timeout_refusal | partial | Signed identity/epoch/role/context binding, transcript confirmation, expiry, rollback and same-object reentry refusal; authenticated duplicate/lower-counter rejection. | Durable invitation consumption across reconstructed objects/restarts, cancellation and real human confirmation. Consumption currently lives in InvitationGate phase_. |
| entropy_and_cold_start_uniqueness | partial | Actual guard/runtime host lifecycle, concurrency and failure tests; OT204 normal target startup, random fills and shutdown. | Target source-absent/failure and physical restart/cold-start uniqueness; cold-power/brownout deferred. No measured entropy-quality or SDK wall-clock guarantee. |
| temporary_secret_wipe_and_log_redaction | partial | Selected-field cleanup and failure/retirement tests; OT204 requires both local sessions to retire and report secrets cleared; bounded console checks. | Complete target failure/abort/reset secret and logging lifecycle; selected fields do not establish whole-memory erasure. Boot preamble contents were not retained. |
| rollback_safe_counter_interruption | partial | Host allocator/KV/replay restart and interrupted-commit tests, SDK-stub NVS commit/readback failures, and OT204 normal real NVS persistence. | Actual target interruption/restart proof. Fresh-only sessions refuse retained state; no resumed-key path. No new physical-attacker rollback-proof hardware requirement. |
| two_device_join_revoke_reset_recovery | partial | Local bidirectional authenticated records, duplicate refusal, retirement/tombstones; OT204 restores both originals independently. | Actual cross-node join/revoke/rekey and current factory-reset/fresh-pairing lifecycle. Two same-chip role evaluations are not a two-device cryptographic join. |
| license_sbom_and_reproducible_lock | partial | Existing libsodium 1.0.22 source/license/SBOM admission, 731-file lock, matching OT203 builds and exact source/compiled dependency closure. All three matched resource controls now exist. | Final selected composition license/SBOM/result binding and Phase 3 corpus admission remain incomplete. Current target uses vendored sources with component manager disabled; absence of a generated dependency lock is explicit, not hidden. |

## First missing proof: OT-206

Prove role-scoped invitation freshness and consumption across gate reconstruction/restart, preserving legitimate A/B use and refusing expired, cancelled or reused authorization before traffic activation or persistent mutation.

`InvitationGate` keeps consumed state only in its object-local `phase_`. Existing
host tests deliberately admit the same invitation into two fresh objects for the
two roles. A successor must bind local-role authority and boot context; a global
nonce blacklist would incorrectly reject legitimate use. Define restart semantics
and add focused actual-source tests before any target trial. This review does not
claim that fresh-only session storage is bypassed or that product exploitation was
demonstrated. It identifies missing invitation-level admission evidence.

## Corpus and acceptance limits

Historical libsodium/Monocypher raw capture custody remains unestablished within
the previously assessed locations. Do not recreate raw bytes from aggregates.
OT-204 preserves strict parser acceptance, fixed diagnostics and an audited journal;
its original wire bytes were not retained. All three matched resource controls now
exist, superseding older resource-control TODOs, but resource images do not inherit
timing measurements from different images.

Both OT-204 boards independently ran both roles on one chip; no cross-node join,
product radio exchange, phone confirmation UI, factory reset or physical interruption
was exercised. Grants remain consumed and originals restored. No V1 completion
credit or public website status change follows from this review. Work remains local
and uncommitted; publication and remote verification were not performed.
