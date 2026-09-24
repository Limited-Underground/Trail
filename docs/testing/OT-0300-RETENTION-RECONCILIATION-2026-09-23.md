# OT-0300 Android retention and sharing reconciliation

Date: 2026-09-23

Status: documentation reconciliation; owner acceptance pending. No implementation,
release-policy successor, hardware, installation, signing or publication authority.

## Scope and authority

ACTIVE_PROJECT_ROOT: `C:/lu/OpenTrail`.
ACTIVE_WORKTREE_ROOT: `C:/lu/OpenTrail/.private/ot177-publication`.
Reviewed starting source: `c45a0615968bf9f1340f901ab545355a3f26f9c3`.
Live checklist OT-0300 revision 1 was owner-approved and started by the
coordinating agent; saved check version 180 was inspected for exact scope.

The contradiction was treating the historical OT-088 transient-only/no-sharing
promise as a current blanket prohibition after later accepted product decisions.
Only the two affected platform documents change. The detailed normative
reconciliation is in the [policy authority table](../platform/ANDROID_PRIVATE_PILOT_OPERATIONAL_POLICY_V0.md#current-v1-retention-and-sharing-authority);
the [release contract](../platform/ANDROID_OPERATIONAL_RELEASE_ACCEPTANCE_V0.md#current-v1-policy-reconciliation)
now explicitly blocks reuse of the historical plan for the expanded product.

| Source | Authority used | Boundary preserved |
|---|---|---|
| [Decision 0032](../decisions/0032-android-private-pilot-operational-policy-freeze.md) | Original transient privacy, uninstall/no-downgrade and best-effort pilot policy | Historical acceptance and exact evidence are not rewritten. |
| [Decision 0009](../decisions/0009-one-phone-companion-authority.md) | Device owns security, groups, queues, history and position-sharing policy | Phone disconnect does not erase or change device policy; app projections are not authority. |
| [Decision 0103](../decisions/0103-adopt-ot168-v1-factory-reset-and-boot-pairing.md) and [reset contract](../platform/DEVICE_FACTORY_RESET_V1.md) | Saved owner; returning-owner authorization; bounded reset receipt; complete device user-domain wipe | System bonds, device state, app-private data and external copies have distinct cleanup. No phone replacement or forensic-erasure claim. |
| [Decision 0104](../decisions/0104-freeze-ot169-v1-user-experience-profile.md) and [V1 UX](../product/V1_USER_EXPERIENCE.md) | Persistent block, private groups, informed location sharing, local templates, deliberate support export and separate V1-Test | Accepted design is not evidence of durable adapters, radio enforcement, physical privacy or release acceptance. |
| [Frozen plan](../../tests/release-plans/OT-086-ANDROID-OPERATIONAL-RELEASE-PLAN-V0.json) and [validator](../../tools/android_release_admission.py) | Exact historical plan values and three prerequisite blockers | No JSON/schema/validator/approval-flag mutation; successor acceptance remains required before current V1 release evaluation. |

## Source observations

OBSERVED 2026-09-23, read-only source review; no new runtime or physical claims:

- `android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1SetupDraftScreen.kt`
  uses app-private `v1-pending-setup`, bounded codec writes and explicit clear;
  its text says pending choices have not been applied to a device.
- `V1HomeScreen.kt` uses app-private `v1-local-templates`. The existing policy
  addendum retains its twelve-template/160-character and 512-byte setup bounds.
- `AndroidBluetoothGattFacade.kt` has the dedicated reset-receipt store.
  Decision 0103 bounds it to receipt plus issued/expiry, at most 120 seconds;
  it is not durable phone/device identity.
- `V1DirectContact.kt` implements typed consent/block transitions and disables
  sharing on block. `V1PeopleScreens.kt` supplies intent actions and displays
  caller-provided state. `V1GroupMembershipRegistry.kt` supplies a model of
  membership. These inspected models do not establish restart-persistent block
  or production transport enforcement; their absence from this proof must not
  weaken the accepted persistent-block requirement.
- `V1SupportScreen.kt` previews before CreateDocument or ACTION_SEND, uses the
  application-scoped FileProvider and explicit read URI grant, and prepares
  `support-reports/` cache copies. The existing eight-file/24-hour cleanup rule
  is applied when preparing another share, not a guaranteed wall-clock deletion
  timer. A cancelled share can leave a cache copy. External saved/shared copies
  are outside app uninstall. Note and pending save text use `rememberSaveable`;
  physical lifecycle/privacy inspection must include restored UI state.
- `tests/release-plans/OT-086-ANDROID-OPERATIONAL-RELEASE-PLAN-V0.json` still
  encodes prohibited persistent product storage, ephemeral local-only companion
  location and no third-party sharing. It cannot truthfully describe all later
  accepted product behavior. A structurally valid plan is not a release pass.

## Lifecycle review

| Before state and owner | Trigger and required result | Forbidden shortcut / failure boundary |
|---|---|---|
| Device-owned saved phone; Android bond separate | Reconnect resolves the current bonded candidate and passes current device authorization | Bond alone, local draft or old app state cannot establish Ready. |
| Persisted block / accepted group consent | Restart or reconnect preserves authoritative controls and refreshes projections | Model recreation cannot silently unblock, admit user content or restore opted-out coordinates. Durable integration is still unverified here. |
| No membership / Optional sharing disabled | Informed join discloses ON and Required/Optional; tightening requires re-enable or leave | No silent reactivation; GPS loss remains unavailable/stale, not refusal. |
| Reviewed local support report | User chooses Save/Share and recipient; cache bounds enforced at share preparation | No automatic send, no claim external copies expire with local cache; failed/cancelled action is not confirmed delivery. |
| App-private data / device user state / external copies | App uninstall, device reset and external deletion follow their separate owners | Uninstall is not device revoke/reset; reset does not erase remote copies. Interrupted device cleanup blocks access until verified. |

## Remaining gates

No new owner choice is needed to restate saved ownership, persistent blocking,
location defaults or deliberate exports: those decisions are already accepted.
No unbounded retention or new phone-local message/location database is approved.
Exact future data schemas, storage limits, consent persistence and deletion
coverage must be specified and validated in their implementation tasks before
admission. The historical policy cannot fill those gaps by promising everything
is transient. A support recipient still needs owner selection before one is
configured; no selection is necessary for this reconciliation.

Before release evaluation, the existing release work must produce an explicitly
accepted successor plan/policy and matching validator bound to the actual data
inventory and exact candidate. Physical matrix, release identity, signer/custody,
privacy, URI grant/cleanup, lifecycle and coherent release evidence remain open.
Historical approved flags do not approve that successor. Public/store distribution,
phone-location sourcing, optional services and hardware work remain excluded.
No V1 completion credit or public website status changes from this correction.

## Validation

VERIFIED 2026-09-23 from the active worktree:

- `python tools/check_repository_docs.py`: passed.
- `python tests/host/repository_docs_tests.py`: 18 tests passed.
- `python tools/android_release_admission.py validate-plan --input tests/release-plans/OT-086-ANDROID-OPERATIONAL-RELEASE-PLAN-V0.json`:
  `PLAN-ACCEPTED-EXECUTION-BLOCKED`, `NOT-EVALUATED`, execution authority false;
  only the three historical prerequisite blockers reported.
- `python tests/host/android_release_admission_tests.py`: 23 scenario groups passed.
- `git diff --check`: passed.

The unchanged canonical plan SHA-256 is
`2c42442936382d9f6e3401d7d91b16566361519ea9e8ef78bb319b34121871b0`.
These checks preserve the historical contract; they do not validate a successor
or establish candidate privacy. No APK build or device test is required by this
bounded documentation correction. The coordinating task owns shared tracker,
backlog, progress and checklist reporting; these three documentation files are
local and uncommitted at handoff.
