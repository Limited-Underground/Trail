# Existing firmware V1 criteria and remaining work

This is a reading map of existing criteria, not a new plan or acceptance decision. The [current handoff](../PROJECT_STATUS.md#current-recovery-handoff) controls pickup. Product development and all physical work remain frozen. [Firmware Project #1](https://github.com/orgs/Limited-Underground/projects/1) and [Android App Project #8](https://github.com/orgs/Limited-Underground/projects/8) own live task status; the [backlog](../../tasks/BACKLOG.md), [remaining-work plan](../../tasks/V1_REMAINING_PLAN.md) and original criteria retain scope. Table entries below identify unfinished boundaries, not newly polled board statuses or permission to execute.

## Definition to retain

[Decision 0033](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md) and [V1/V1.5 acceptance scope](../testing/V1_V1_5_ACCEPTANCE_SCOPE_V0.md) require a coherent, candidate-bound protected path across exactly two phone/Heltec pairs. [Decision 0103](../decisions/0103-adopt-ot168-v1-factory-reset-and-boot-pairing.md) supersedes pairing/replacement clauses; [Decision 0104](../decisions/0104-freeze-ot169-v1-user-experience-profile.md) retains the user-experience contract. Optional products and V1.5 do not become V1 prerequisites.

The diagnostic run demonstrates fixed-status transport with unauthenticated RF. Native tests and the qualified source rebuild supplement it without completing protected delivery, physical acceptance, signed release, field performance or recovery equality.

## Existing task route

The preserved backlog already accepts OT-0247c's host correction and OT-0247d's corrected physical trial, OT-0238b's revision-2 isolated evaluation host candidate, OT-0168b's computer reset composition and OT-0101c's experimental configuration audit. Reuse those results within their exact boundaries. Their presence in the older remaining-work plan does not reopen them or turn them into production acceptance. The October 29 progress record also accepts OT-0237b's later bounded entropy/restart source binding; its earlier backlog wording is a dated snapshot.

| Existing task(s) | Required result retained from existing criteria | Current evidence boundary |
| --- | --- | --- |
| OT-0238c; final binding OT-0005i | Complete the remaining target lifecycle, revoke and reset/recovery cases, followed by the separately selected production composition. | The accepted isolated host candidate and October 12 USB retained-rekey case (restart, fresh activation, 8 + 8 authenticated statuses and old-epoch refusal) are reused. They do not complete broader physical lifecycle or Android production binding. |
| OT-0237b / c / d / e | Exact-target entropy, counter/replay persistence, secret retirement/privacy and final independent security evidence admission/selection. | The later October 29 record accepts b's bounded entropy/restart source binding; cold-power/brownout is deferred. c/d target interruption/cleanup and e production admission remain separate. Library benchmarks or isolated conditional profiles do not close them. |
| OT-0005i / OT-0005j | Bind the selected secure transport and final production enrollment to actual firmware; authorized protected BLE commands/events reach both phone UIs in both directions with truthful queue/TX/ACK distinctions. | Unauthenticated diagnostic DATA/ACK does not satisfy this protected composition. |
| OT-0168c | Accept interrupted-state refusal and both exact reset paths with old-owner rejection and fresh unowned pairing, reusing OT-0168b's accepted durable host composition. | Computer composition is not physical interruption or both-trigger acceptance. |
| OT-0101d / e / f; OT-0101c boundary | Accept power/battery, remaining GNSS/clock and live OLED/degraded-state behavior within the exact claimed target/regional configuration. | OT-0101c's audit is accepted without supported/field claims. OT-0101e's bounded retention/clock evidence is retained; GPS loss was inconclusive and recovery/restart cases were not reached at its recorded checkpoint. |
| OT-0089a; OT-0089e / f / g | Coherent two-pair V1 acceptance, bounded field/endurance observations, release-evidence/operator-document reconciliation, then separately authorized publication and artifact verification. | Diagnostic transport, buildability and documentation review do not complete this chain. |

The remaining-work plan's dependency graph is retained; no table order here changes it. Android retention/sharing, app lifecycle and signing retain their own criteria and [Android reading path](../../android/README.md). Firmware Loader acceptance stays in its [separate repository](https://github.com/Limited-Underground/Firmware-Loader).

## Next action and decisions

Use the [current handoff](../PROJECT_STATUS.md#current-recovery-handoff), owning Project and working contract to select authorized work. Dated publication outcomes belong in the progress history; main adoption retains separate owner authority. Product resumption requires explicit owner direction. If resumed, select the live existing task, its exact source/recipe/criteria and prerequisites before any implementation or physical action. Do not invent a new wire, security selection, supported target or release commitment to fill a documentation gap.

The [V1 progress record](../V1_PROGRESS.json) contains separate historical standalone and current Companion tracks. Its dated scores are not a fresh readiness measurement; this documentation does not change them. Keep task completion, source/build proof, device observations, owner acceptance and release claims separate.
