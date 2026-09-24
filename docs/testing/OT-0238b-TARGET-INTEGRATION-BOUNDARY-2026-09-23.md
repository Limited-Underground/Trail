# OT-0238b target integration boundary reconciliation

## Result

The remaining target work must follow the accepted V1 physical-access boundary.
[Decision0033](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md)
and the [threat model](../security/THREAT_MODEL_V0.md) exclude hostile physical
flash replacement/rollback. The later [selection review](../security/CRYPTO_SELECTION_REVIEW_2026-09-10.md)
explicitly adds no secure-element, irreversible eFuse or production secure-boot
requirement. The identity-store comment now reflects that boundary. No security
check, key operation, acceptance decision or executable behavior changed.

Prior host reports correctly disclose an unsealed identity seed and lack of
hostile rollback protection. Those limitations must not become new mandatory V1
hardware features. They do not excuse accidental interruption, stale authority,
remote secret exposure, missing reset cleanup or reuse of old traffic keys.

## Actual remaining integration

| Owner | Existing implementation | Remaining acceptance |
|---|---|---|
| Identity storage | Candidate isolates ot_identity_v1 and verifies exact persistent seed readback | Bind target-owned storage; keep seed inaccessible to ordinary commands/logs; include namespace in verified destructive reset |
| Enrollment stores | Separate generation, session, membership, evidence, binding and journal owners; host interruption coverage | Exact target namespace inventory, serialization, readback and complete reset coverage |
| Display/input | Host trusted-IO review adapter and complete layout | One real GPIO/display arbiter, actual pixels, reset preemption and stable release handoff; normal status cannot overwrite review |
| Request routing | Protected companion configuration; evaluation-only confirmation kind7 | Product-owned bounded request/lifecycle route; disconnect cancels pending work; evaluation route is not product authority |
| Lifecycle/timing | First enrollment and matching-committed fresh rekey host composition | Full target resource/read/signature timing, rejection/recovery and separately authorized physical acceptance |

Source review: heltec_v4_factory_reset_storage.cpp currently checks owner, device
name, region and existing user state; it does not establish reset coverage of the
new candidate enrollment namespaces. StartupDisplayOwner covers PIN/reset overlays,
not enrollment leases. The companion dispatcher restricts kind7 to its evaluation
minor; it cannot be silently promoted into a product enrollment command.

Target integration is therefore unfinished for concrete ownership and lifecycle
reasons, not because V1 requires tamper-resistant hardware. No hardware, eFuse,
firmware binary, final algorithm/wire selection or V1 credit is produced here.
Validation is source/authority reconciliation plus repository documentation checks;
prior56-suite results remain evidence for unchanged executable behavior only.
