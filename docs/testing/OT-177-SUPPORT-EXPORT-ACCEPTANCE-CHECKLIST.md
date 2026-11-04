# OT-177 support-export acceptance checklist

## Purpose and authority

Planning deliverable for board child **OT-0177a-1**, under milestone OT-0177a
and existing engineering authority OT-177. The board suffix is a planning slice,
not a newly registered engineering ID. Prepared October 18, 2026 as a dated
requirements checklist.

This document defines future acceptance; it executes no product or device test.
Every case below starts **NOT RUN**. Documentation review can validate this
checklist only, not its parent, the product, a physical attempt, a release, or
V1 completion. Writing it changes no board status. Before executing any case,
read its current GitHub Project scope and obtain the needed bounded authority
for that operation and artifact. Earlier parent planning does not authorize
child execution or device work.
Board references:
- [Parent OT-0177a](https://github.com/orgs/Limited-Underground/projects/1?pane=issue&itemId=270958440).
- [Checklist task OT-0177a-1](https://github.com/orgs/Limited-Underground/projects/1?pane=issue&itemId=271003410).

## Owning sources and precedence

- [V1 user experience](../product/V1_USER_EXPERIENCE.md): support preview,
  Save/Share, user-note disclosure, bounded V1-Test and production separation.
- [Current retention/sharing policy](../platform/ANDROID_PRIVATE_PILOT_OPERATIONAL_POLICY_V0.md):
  current V1 authority and implementation addendum take precedence over the
  historical transient-only/no-sharing pilot description.
- [Release acceptance contract](../platform/ANDROID_OPERATIONAL_RELEASE_ACCEPTANCE_V0.md):
  current-candidate successor admission remains separate from historical approval.
- [Engineering backlog](../../tasks/BACKLOG.md): OT-177, OT-0177a, OT-0172a,
  OT-0320 and OT-0320a scope and remaining gates.
- [Project status](../PROJECT_STATUS.md): current evidence summary.
- [OT-0300 reconciliation](OT-0300-RETENTION-RECONCILIATION-2026-09-23.md):
  disclosure, retention, external-copy and policy boundaries.
- [OT-0320a verified region evidence](OT-0320a-VERIFIED-SUPPORT-REGION-2026-10-01.md):
  owner-accepted host scope and its exact limits.
- [Service-lifetime logging evidence](../../tests/hardware/OT-177-SERVICE-LOGGING-2026-09-06.md):
  historical recorder bounds, ownership, production-package inspection and limits.
- [Support schema source](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1SupportReport.kt):
  OT-SUPPORT/1, typed fields, note normalization and rejection rules.
- [Support screen source](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1SupportScreen.kt):
  preview, pending Save and explicit Share integration.
- [Unsigned-package inspector](../../android/Test-AndroidUnsignedReleaseArtifact.ps1):
  existing package-inspection mechanism; referenced, not run by this task.

Source inspection states intended/current source behavior, not physical proof.
If sources disagree, record the exact conflict and stop that case for owner
resolution; do not silently choose an easier expectation or rewrite history.

## Existing evidence versus open gates

| Evidence or dependency | Retained scope | What remains open here |
|---|---|---|
| OT-0320a report | Owner-accepted host tests and exact unsigned-package inspection for verified region projection | Current artifact identity, real-phone export/usability, URI grants/cleanup and full OT-0320 acceptance |
| OT-177 service report | Historical service-owned recording, generation cleanup, bounded storage and production exclusion | It explicitly did not install its new artifact; no new-recorder physical pass can be inferred from that report |
| Current OT-177 backlog | Records later installed combined APK, name/time and saved-owner restart acceptance | Those statements are not a full support-export pass; exact supporting artifacts must be reconciled before reuse |
| OT-0300 | Project status records owner-accepted reconciliation; the original report retains its earlier pending-review wording | Record that dated difference; do not treat the earlier header as current execution approval or claim current release/privacy admission |
| OT-0172a | Parent acceptance dependency; backlog still lists complete production UI/accessibility acceptance as planned | Obtain exact current dependency disposition; do not mark it satisfied from historical scope approval |
| OT-0177a / OT-0320 | Full support-export acceptance remains open | Checklist completion grants no parent, physical, field or release acceptance |

No historical test count, artifact hash, phone result or owner decision is
automatically transferred to a new build or attempt. Cold-power work remains
owner-deferred and is not part of this checklist's execution authority.

## Preconditions for any later case execution

1. Accept a separate bounded plan naming cases, exact checkout/commit and dirty
   inputs, candidate identity, validation mechanism and allowed operations.
2. Recheck current board scope/approval and OT-0172a dependency disposition.
   An unknown or unmet dependency blocks parent acceptance; any preliminary
   host investigation requires its own expressly bounded authorization.
3. Pin the exact app variant, package ID, version, APK digest and signing state
   in the private run record. Inspect installed identity before any authorized
   phone case. No artifact is selected or installed by this document.
4. Use only synthetic notes, messages, locations and identity/secret sentinels.
   Do not gather real keys, PINs, contacts, payloads or device identifiers as
   checklist evidence. Agree on approved storage and retention for private
   artifacts before capture; public summaries contain only sanitized facts.
5. Choose the exact phone/Android matrix, recipient test harness, artifact
   inspection method and traffic-observation coverage in the later plan.
   These are unresolved here. No real recipient or external send is authorized.
6. Resolve the current recorder-format/migration expectations and exact metrics
   allowlist before testing beyond the pinned source. Unknown metrics are not
   permission to add fields, collect raw telemetry or expand this task.
7. Have separately approved phone/data preservation and restoration rules before
   any phone operation. Missing evidence, identity mismatch or privacy uncertainty
   is BLOCKED, never PASS or silently skipped.

## Evidence record required for every case

A future private record must contain these exact named fields:

| Field | Required content |
|---|---|
| case_id, run_id, observed_at_utc | Case number, non-identifying run label and ISO UTC observation time |
| evidence_layer, method | Source review / host test / package inspection / physical / owner decision; exact command or numbered procedure and observation coverage |
| source_binding | Checkout role, commit, relevant dirty inputs and source links; no inference of a clean build |
| candidate_binding | Variant, package ID, version, APK digest and signing state, or NOT APPLICABLE with reason |
| environment | Tool versions for host/package work; approved model/Android version for physical work; no serial, MAC or pairing identity |
| authorization_ref, dependency_state | Exact accepted plan/decision reference; OT-0172a and other applicable gate disposition |
| synthetic_inputs, before_state, trigger | Non-sensitive sentinel labels, controlled state and exact action |
| expected_result, actual_result | Measurable expected and observed results; never copy a historical result as actual |
| verdict, limitation | NOT RUN / PASS / FAIL / BLOCKED; explicit unobserved behavior and layer limits |
| evidence_refs | Approved private artifact reference plus digest/size where applicable; sanitized public summary, no raw sensitive contents |
| reviewer, owner_disposition | Independent review conclusion and separate owner acceptance/pending state |

For exports record schema, MIME type, byte count, private artifact digest,
allowlisted field names and forbidden-sentinel presence/absence, not secret
values. For screenshots redact unrelated data and use synthetic fixtures.
For transmission observations record monitored process/path, time interval,
recipient boundary and method limitations. An absence-of-traffic observation
without adequate coverage is inconclusive.

## Numbered acceptance cases

These are specifications for later separately approved work. All are NOT RUN.
A host PASS never substitutes for the physical cases or owner decision.

| Case / layer / source | Trigger and measurable expected result | Additional evidence and stop rule |
|---|---|---|
| C01 - Source/host; support schema and policy | Generate the same synthetic snapshot/note twice: identical OT-SUPPORT/1 text, text/plain and trail-support-report.txt. Automatic fields are limited to App, Firmware, Phone, Android, Device, Radio region, Phone connection, Radio, GPS, Power, Reconnect count and Last support code. Inject synthetic excluded-data sentinels through candidate inputs: no PIN/key, pairing ID, hardware address, invitation/group secret, message body or coordinate appears in automatic fields. User-note fixtures are evaluated separately in C02. | Record complete field-name comparison, output digest/size and each sentinel's presence/absence. Any extra automatic field or leaked sentinel is FAIL; stop before external export. |
| C02 - Host plus physical UI; UX and schema | Empty note, exactly 2,000 characters, 2,001 characters, CRLF/CR, tab/newline and forbidden control-character fixtures follow the pinned normalization contract: trim outer whitespace, normalize line endings, retain allowed tab/newline, reject over-limit/disallowed controls. The preview and resulting report clearly distinguish user-supplied text and warn it may contain private details. A synthetic note sentinel may remain in that section; automatic redaction must not be claimed to sanitize it. | Record input/output lengths, normalization outcome, warning wording/visibility and exported note section. Missing warning, silent over-limit export or claiming notes are automatically safe is FAIL. Do not use actual sensitive notes. |
| C03 - Physical; UX and support screen | Open support with no recipient configured: show preview, Save text file and Share/Email; omit the address field. Before an explicit Save/Share action, no report is externally written or sent. Review a synthetic report and cancel each chooser: no success/delivery claim. | Record visible controls, reviewed text identity, chooser/cancel outcomes and side-effect observations. Unexpected recipient, external write or send before consent is FAIL. Local share-cache creation is evaluated under C06, not misreported as network delivery. |
| C04 - Host plus physical; support screen and OT-0320a | Preview synthetic state A and select Save; change live state to B while the picker is pending. Saved bytes must equal the reviewed pending report A, not a silent mixture or new B. Cancel/failed writes must not claim success; a newly requested preview reflects its own current state. Repeat with rotation/restored UI state using only synthetic note text. | Record preview/saved digests, byte lengths, state labels, timestamps and cancellation/error result. Stale pending data must be disclosed as the report selected at Save time. A wrong report, cross-state mixture or false success is FAIL. |
| C05 - Package plus physical; policy and package inspector | Confirm non-exported application-scoped FileProvider, explicit read URI grant and only support-reports/ exposure. In a separately approved local recipient harness, the selected report is readable under the intended grant; unrelated app files and write access are denied. Opening the chooser alone is never recorded as delivery. | Record manifest/provider paths, granted modes, allowed/denied URI classes and local receiver outcome. Freeze grant-lifetime expectations before execution. Any overbroad access or missing consent is FAIL; no real recipient transmission is authorized here. |
| C06 - Host plus physical; retention policy and OT-0320a | At share preparation, enforce the eight-file cache cap and 24-hour age cleanup, including boundary fixtures and cancelled shares. With no new share preparation, do not promise timed deletion. App-local cleanup must not be represented as deleting user-saved or recipient-controlled copies. | Record synthetic cache counts/ages before and after the cleanup trigger, cancelled-share residual state and external-copy ownership. Any exceeded contract bound at its enforcement point or false deletion promise is FAIL. Do not delete owner files to test this. |
| C07 - Host plus physical; UX, backlog and service evidence | Append synthetic events across 512-record and 64-KiB boundaries: retained log stays within both bounds with oldest-first eviction. Preserve format-3 typed stages and separate failure diagnostics; unknown/missing diagnostics remain unknown, not a fabricated cause. Exercise the exact pinned supported migration and malformed-format rules. | Record format, pre/post count and bytes, retained synthetic sequence range, failure-field values and migration verdict. Freeze any unspecified byte-accounting/migration rule first. Over-bound storage, invented diagnostics or silent malformed-record acceptance is FAIL. |
| C08 - Host plus physical; UX and service evidence | While a service-owned recorder is active, detach/rebind UI: no duplicate observer or false terminal event. Clear a populated log, reload and inspect/export: pre-Clear synthetic records do not reappear; new events are distinguishable. Diagnostic callback failure and old-generation cleanup do not gain connection authority or terminate a newer trace. | Record synthetic generation/event sequence, Clear boundary, post-reload/export counts and observed lifecycle coverage. Reappearing old data, duplicate ownership or misleading terminal record is FAIL. Process death is not assumed to produce a final event or lossless telemetry. |
| C09 - Package plus physical UI; UX and package inspector | Inspect the exact production package: V1-Test recorder/Application, diagnostic classes and test-only surfaces are absent; ordinary redacted support remains available. Production successful paths expose no local-test/fake-device/service-start plumbing. The V1-Test package is visibly separate with application ID io.github.nbjelanovic.otclient.v1test, label Trail V1-Test and -v1test suffix. | Record package IDs, manifest/DEX inspection results and synthetic UI evidence for both variants. Debug-only observations or a historical APK audit do not pass the current production package. Any test-surface leakage is FAIL. |
| C10 - Source/package plus physical; policy, UX and support screen | Observe app start, support entry, preview, Save, cancelled Share, background/foreground and Clear. No automatic telemetry, upload, analytics, recipient selection or send occurs. Review manifest/code paths and observe the approved app/recipient boundary; receiver opening is distinct from sending. | Record source/package checks and bounded traffic observation with coverage limits. Unexpected transmission is FAIL; inadequate coverage is BLOCKED. Do not infer no upload solely from one missing permission or one quiet capture. |
| C11 - Host plus physical; OT-0320a | Only a current protected positive READ of supported catalog ID/code 1/US915 yields US915. Unset/unknown/unsupported region, null/zero revision, pending or uncertain operations, WRITE alone, disconnect/reconnect, lifecycle stop, owner closure or reset yield Not available until fresh proof. Firmware/device labels without authenticated producers remain unavailable. | Record synthetic state transitions and preview values; no real device identity is needed in the report. Stale region reuse, fabricated labels or an automatic device operation triggered solely by opening support is FAIL. This does not grant RF authority. |
| C12 - Owner gate; board parent, OT-0172a and release contract | Review the complete evidence bundle against this checklist: each required layer has exact candidate-bound evidence; failures/unknowns remain open; OT-0172a acceptance is proven; owner separately decides parent acceptance. Signing, release, field and full V1 gates remain separate. | Record case dispositions, unresolved dependencies and explicit owner decision/reference. Missing evidence, pending dependency or no owner decision blocks parent acceptance. A completed checklist or passed host suite is insufficient. |

## Stop and acceptance rules

- Any privacy leak, unexpected transmission, wrong candidate, uncertain scope,
  unsupported permission or failed expected result stops the affected run before
  further export/device actions. Preserve only approved sanitized evidence;
  propose a separate diagnosis/correction rather than expanding execution.
- Missing or contradictory prerequisites produce BLOCKED with the exact missing
  fact. NOT RUN and BLOCKED are never PASS. Waiving a case requires a separately
  recorded owner decision and cannot silently broaden product claims.
- Checklist acceptance requires independent review of source traceability,
  numbered-case coverage, measurable results, evidence fields and explicit gates.
  Documentation validation does not execute C01-C12.
- Parent acceptance requires separately authorized execution and owner review.
  This checklist does not change a board status, progress percentage or public
  capability, and does not authorize firmware, devices, installation, signing,
  publication, optional services, maps, metrics implementation or V1.5 work.
