# OT-0261b Console microSD and power failure policy proposal

Prepared 2026-09-29 for owner review under approved revision 1. Planning only:
no firmware, filesystem, board, battery, charger or supported target is selected.

## Owner summary

Keep the card optional and separate from the Console's authority and delivery
state. A missing or damaged card should explain which content is unavailable;
it must never reset ownership, erase replay protection or turn an uncertain send
into a success. Power loss may lose an unfinished content write, but recovery
must expose that uncertainty rather than retry a radio action automatically.

Recommend starting with manually selected, approved non-secret reference content
on microSD, with no automatic conversation, location or diagnostic recording.
This is a recommendation, **not an already approved storage purpose**. The owner
still needs to select the first content category and its retention/capacity rules.
The failure policy below remains applicable without pretending that choice exists.

## Existing commitments and evidence boundary

The [accepted Console direction](OT-0260b-CONSOLE-PROPOSAL-2026-09-29.md) is one
standalone touchscreen/LoRa/microSD/battery assembly. It recommends user-visible
non-secret card content and leaves first contents, retention, power design and
exact hardware unresolved. The [accepted semantic flow](OT-0261a-CONSOLE-FLOW-2026-09-29.md)
separates storage, radio, authority and power states; absent/full SD cannot grant
or revoke radio authority. It also separates local queue admission, submission,
protected peer acceptance and uncertain results.

[Decision 0007](../decisions/0007-shared-client-presentation-tracks.md) supplies
shared semantics with separate standalone adapters. Its old standalone-first
release scope is superseded by [Decision 0033](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md).
Console remains optional to phone/BLE/Heltec/direct LoRa/Heltec/BLE/phone V1.
Neither the historical GNSS binding nor a stronger physical rollback claim is
silently added here. Current pairing/reset supersession remains as stated in
Decision 0033's current notice; card errors are never a factory-reset trigger.

The [portable composition](../platform/PORTABLE_CLIENT_COMPOSITION_V0.md) is
host structural evidence only. Configuration/secret/protocol/outbound-counter
storage uses distinct two-slot 64-byte domains; replay checkpoints separately
need two full 704-byte slots. Those contracts are not a microSD driver, filesystem
or complete Console persistence backend. Their capacity and failure semantics
must remain distinct even if a later target shares a physical backend.

## Proposed content, retention and capacity boundary

| Data class | Proposed location/purpose | Retention, capacity and authority rule |
| --- | --- | --- |
| Owner-selected non-secret reference content | Recommended first microSD purpose, pending category/format selection. No executable scripts, provisioning or auto-import. | Keep until deliberate owner replacement/deletion; no automatic age deletion or overwrite. Validate bounded schema/version, length and integrity before exposing content. A checksum detects damage, not trusted authorship. |
| Optional user export | Not enabled by this proposal; requires selection of exact fields, recipients/privacy and format. | Explicit per-export action; bounded operation and truthful saved/failed/uncertain result. No background history logging. Completed exports remain until deliberate deletion under the selected policy. |
| Disposable caches, maps or offline packages | Not selected. Maps require separate provider/license/attribution and accepted pipeline. | No cache eviction or map download behavior is approved by card presence. A future cache policy must identify recreatable data and must not evict user files or security state. |
| Messages, locations, private identifiers and diagnostics | No default card retention recommended. Non-secret does not mean non-private. | New recording/export needs purpose, consent, minimization, lifetime and deletion decisions. Do not copy payloads or secrets into ordinary error logs. |
| Credentials, authorization, keys, outbound counters and replay state | Separately reviewed persistent authority backend; not trusted from removable content. | Required atomicity/validation and failure containment belong to owning security contracts. Card files cannot replace, restore or clear these domains. No card-based credential backup is proposed. |
| Pending operations and delivery outcomes | Owning operation/state contract; a view or optional export is not the authority. | Card insertion/removal, a saved file or successful flush cannot manufacture peer acceptance. Restart must preserve verified evidence or show outcome uncertain; no automatic send from a restored file. |

Before writable card support, select a filesystem/card profile and freeze finite
limits for file count, individual size, total selected content/export bytes,
working buffer, open handles, transaction reserve and service-time budget.
Reject unsupported values and checked-arithmetic overflow before allocation or
write. Advertised card capacity is not a promise of usable application capacity.
No default unbounded quota, numeric card requirement or runtime promise is set.

Reserve worst-case temporary-write and metadata space before accepting an export;
if that reserve cannot be established, refuse the new write without deleting
existing content. Separate free space, quota exhaustion and I/O failure visibly.
No global filesystem scan, retry loop or card operation may indefinitely delay
radio servicing, touch recovery or a power transition. Adapter cancellation and
completion must be bounded; exact deadlines await the selected target's evidence.

Deleting a file does not prove secure erasure on flash media. Report deletion only
within the implemented filesystem contract; do not promise removal from backups
or physical media. This is another reason not to record private data by default.

## Complete media lifecycle and failures

The storage manager alone owns mounting, file handles, writes and removal state.
The UI renders its current operation revision; a late completion from an old card
or retired operation cannot complete a new request. Reinsertion creates a new
media session even when labels or filenames match; these are not identity proof.

| Before state / trigger | Required transition and visible evidence | Forbidden effect / bounded recovery |
| --- | --- | --- |
| Boot with no card | Report Card absent; omit or explain selected card-dependent content. Continue independently authorized field functions when their own resources are ready. | Do not create default trust, claim empty verified content, or require internet/phone/server to restore local authority. |
| Card inserted | Mount conservatively, validate supported media/content and acquire a fresh session before showing available content. Unknown/unsupported content remains unavailable. | No autorun, automatic import, format, credential restoration or radio request. A read-only card can expose validated readable content while writes remain unavailable. |
| Ready; user requests selected write/export | Revalidate current offer, media session, quota/reserve and power/driver readiness. Stage bounded data separately from the last committed version. Show Saving, not Saved. | No success on queue admission or partial write; no overwrite of the only verified copy before replacement is committed. |
| Write reaches commit | Require the selected backend's documented durable completion and integrity/metadata validation before Saved. Release ownership/handles and update only this operation. | Rename/flush alone is not assumed power-loss atomic on an unselected card/filesystem. If durability cannot be established, show failed or uncertain rather than Saved. |
| Full card or exhausted quota | Refuse new writes; retain validated readable content and last committed versions. Explain deliberate space-management options. | No automatic user-file deletion, authority loss, success fabrication or repeated busy-loop retries. Retry is a new deliberate operation after rechecking resources. |
| Corrupt/unsupported media or read error | Mark affected content unavailable; isolate the failing media session. Preserve useful independent status and wired recovery. | No automatic repair/format or trusting partial files. Any destructive repair/format needs a separate explicit user action and agreed recovery procedure. |
| User selects Safe remove | Stop accepting new card work, retire input offers, cancel or finish the bounded current transaction according to its actual state, flush/close/unmount. Show Safe to remove only after every required completion succeeds. | No removal-ready claim while writes/handles remain owned or commit is ambiguous. On error show Removal not confirmed; offer orderly power-off/recovery guidance, never a guarantee that prior data survived. |
| Card pulled unexpectedly | Invalidate session and handles; stop further access and report Card removed / write outcome uncertain where applicable. | No late acknowledgment to a replacement card, silent continuation, forced remount write or automatic radio retry. |
| Reinsert after fault or interrupted write | Validate afresh; admit only a verified committed generation. Ignore/quarantine incomplete temporary content and report interrupted operation/loss. | Do not infer success from file existence, a higher generation or checksum alone. Preserve recoverable files; no automatic deletion or authority reconstruction. |

The proposed write contract is last verified committed content or an explicit
unavailable/uncertain result after any cut point. It does **not** promise that an
arbitrary removable card preserves the previous version through sudden power loss.
If target testing cannot establish the chosen contract, keep writes unsupported
or revise the accepted profile rather than weakening visible success semantics.

## Power states, sleep and recovery

Use one atomic normalized observation with freshness, source and failure status,
as required by the composition contract. Do not infer percentage/runtime from an
unknown cell or equate attached charging power with healthy battery/persistence.
Critical/low thresholds, hysteresis, freshness interval, shutdown reserve, charging
limits, temperatures, current peaks, brightness/radio duty and endurance all await
an exact measured assembly. Historical host policy constants are not Console limits.

| Power/lifecycle state | Proposed behavior | Recovery and truthful outcome |
| --- | --- | --- |
| Valid normal observation | Permit only actions whose own authority, persistence and resource checks pass. | Normal power is not evidence of storage durability, radio readiness or peer availability. |
| Low power under an accepted measured profile | Show the warning and selected capability limits; stop new optional writes when their completion reserve is unavailable. Keep independent status/recovery responsive. | Do not fabricate battery time remaining or bypass security commits to keep Send available. Specific display/radio restrictions require the later profile decision. |
| Critical power / orderly shutdown requested | Stop new work, retire stale confirmations, reconcile or mark in-flight work uncertain, perform only bounded shutdown commits with demonstrated reserve, then release adapters. | Do not assume enough time to flush everything, wait forever for radio/card, or mark pending traffic delivered. No emergency-delivery guarantee. |
| Unknown, stale or failed power observation | Show Power status unavailable. Do not reuse a stale percentage as current. Require proven operation-specific readiness before new writes or other power-dependent actions. | Sensor failure alone is not a measured critical-voltage event; neither blanket success nor unmeasured numerical shutdown is justified. Maintain safe recovery and contain work whose prerequisite cannot be established. |
| Sleep requested | Retire input revisions; settle or explicitly abandon optional writes; preserve required security state before sleep is admitted. Record pending outcome accurately. | No sleep that silently bypasses required commits. Continuous receiving during sleep is not promised; state that communication may be unavailable. |
| Wake | Resample the checked clock/power source, validate media session/state and rearm transport as required; present a fresh offer before action. | Old touch/hold, handles, deadlines or receive readiness do not survive by assumption. |
| Abrupt power loss/brownout/reset | On next boot, validate all domains before authority-dependent operation. Recover only proven commits; expose interrupted content work and uncertain transmissions. | No counter rollback, replay-window clearing, default ownership, blind resend or interpreting an unfinished SD export as successful delivery. Ambiguous security persistence contains traffic pending its reviewed recovery. |
| Charger attached/detached | Update power state using the target observation and driver contract; reconsider operation readiness. | No reset, credential change or guaranteed uninterrupted operation based solely on a cable event. |

Card failure and optional service loss remain distinct from security-storage
failure. The former disables only affected features; the latter may legitimately
contain traffic. Lack of physical power can stop the whole assembly: independence
is a failure-isolation requirement, not a battery-backup guarantee.

## Specific owner choices and later evidence

1. Select the first microSD purpose: recommended manually selected non-secret
   reference content only, or name the exact export/content category needed.
   Conversation/location history, diagnostics and maps remain excluded unless
   explicitly selected with their privacy/licensing requirements.
2. Accept or revise the proposed retention default: keep selected user content
   until deliberate deletion/replacement, no background capture and no automatic
   user-data eviction. If expiry/rotation is wanted, name its data class and limit.
3. Confirm expected card workflow: recommended user-supplied preloaded content
   first, with no on-device format/repair; decide separately whether writable
   exports or managed import are needed. Engineering must then freeze compatible
   media/filesystem/formats and bounded quotas before implementation.
4. Describe desired sleep/receive behavior and expected operating/charging use.
   Do not select numerical voltage, percentage or endurance limits from this
   proposal; the exact battery/charger/sensor and measured assembly set them.

OT-0262a can map only selected features to real owners/adapters and register bounded
implementation gates; unknown selections must remain prerequisites, not placeholder
success providers. OT-0262b should exercise absent/read-only/full/corrupt cards,
removal at staging/commit/completion, stale completion after reinsertion, interrupted
write/restart, power loss before/after possible radio submission, low/unknown power,
failed required security commits and sleep/wake. Each case must assert visible
state, retained/lost data, resource release and forbidden authority/delivery effects.
OT-0263a separately measures actual card/power and user behavior on the exact
assembly. These are future acceptance cases, not tests executed by this report.

## Validation and limits

Preparation reviewed the linked accepted direction, semantic flow, composition,
current V1 boundary and [registered planning task](../../tasks/OPTIONAL_PRODUCTS_PLAN.md).
Only this report and private preparation/validation evidence are authored here;
shared status/backlog/tracker updates belong to the coordinating task. Repository
checks and local-link results accompany the private evidence manifest.
No firmware/build/device/purchase, implementation approval, supported-target claim,
V1 completion credit, website status change or publication results from this plan.