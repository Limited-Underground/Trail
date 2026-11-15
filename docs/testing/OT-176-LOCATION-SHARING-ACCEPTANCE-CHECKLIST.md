# OT-0176a location-sharing acceptance checklist

## Purpose and authority

Planning for [Trail #159](https://github.com/Limited-Underground/Trail/issues/159), a child of [OT-0176a (#102)](https://github.com/Limited-Underground/Trail/issues/102). Every case starts **NOT RUN**. Checklist review cannot accept the parent or authorize any device operation.

The owner’s `C:/Projects/LimitedUnderground/Portfolio/requirements/TRAIL-V1.md`, requirements 6 and 7 and rows F10 and A7, controls (D-040, D-054). The following is the requirement text:

> 6. **In a group**, the group admin (its creator) chooses the group's location setting:
>    - **off**: members do not share location to the group;
>    - **preferred**: members are asked to share, but a member may refuse and still join;
>    - **required**: a member must share; a user who refuses does not complete joining.
> 7. **Outside a group**, sharing is the user's choice and only possible once a conversation has started:
>    - the person sending a conversation invite can choose to share their location with it;
>    - the person accepting can choose whether to share back;
>    - either person can turn their sharing on or off at any time after the conversation starts.

Capture the sender’s choice on the invite and the recipient’s own choice on acceptance. Verify there is no location disclosure while the conversation is pending; check effects when it starts. The checklist does not choose a wire encoding.

## Owning sources and existing evidence

- [Group pairwise design](../security/GROUP_PAIRWISE_V1.md) and [firmware roster](../../firmware/components/identity/include/opentrail/group_roster.hpp) recognize Off, Preferred and Required. Group host work in #248–#250 does not prove actual location flow. D-058/D-059 bound group position traffic to 60 seconds or slower and stop sending to a group member unheard from with the current roster for 10 minutes.
- [Android group profile](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1GroupProfile.kt), [group screen](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1GroupScreens.kt) and [snapshot](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1GroupSnapshot.kt) show the older local group model. [Direct contact](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1DirectContact.kt) has a local post-accept toggle. [Location presentation](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/GroupLocationPresentation.kt) models presentation, not a device producer.
- [#119](https://github.com/Limited-Underground/Trail/issues/119), [#120](https://github.com/Limited-Underground/Trail/issues/120) and [#121](https://github.com/Limited-Underground/Trail/issues/121) own consent, authenticated location/stop, and coordinate details. No retained host result substitutes for the F10/A7 device cases.

## Open findings at main ae30853

| Requirement | Where the design or code differs | Consequence |
| --- | --- | --- |
| 6: Off and real join choice | [V1 user experience](../product/V1_USER_EXPERIENCE.md#groups) (lines 67–72), [Android pilot policy](../platform/ANDROID_PRIVATE_PILOT_OPERATIONAL_POLICY_V0.md) (line 38), [remaining plan](../../tasks/V1_REMAINING_PLAN.md) (lines 282–284), [backlog](../../tasks/BACKLOG.md) (OT-176 and OT-173) describe ON at join and Required/Optional. The same old rule is retained in [product boundaries](../PRODUCT_BOUNDARIES_V0.md) (lines 35–42), [prior V1 progress data](../V1_PROGRESS.json) (line 286), [historical Decision 0104](../decisions/0104-freeze-ot169-v1-user-experience-profile.md) (lines 64–73) and the [retention record](OT-0300-RETENTION-RECONCILIATION-2026-09-23.md) (line 67). | No group Off. A post-join opt-out does not prove Preferred refusal at join or Required refusal before joining. These passages are not the governing requirement. |
| 6: Android implementation | [V1GroupProfile.kt](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1GroupProfile.kt) (lines 6–9, 90–96) has two rules and forces a new member's sharing true. [V1GroupScreens.kt](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1GroupScreens.kt) (167–175, 342–364) says sharing starts ON, offers two choices and toggles only after joining. [V1GroupSnapshot.kt](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1GroupSnapshot.kt) (line 104) decodes that two-rule ordinal. | Off is unrepresentable and consent at join is not enforced. Older stored rules need a safe interpretation before a candidate can pass. |
| 6: firmware join boundary | [Group design](../security/GROUP_PAIRWISE_V1.md) (lines 116, 193–210) carries the three values and shows the rule; the pair/receipt description does not specify preserving a Preferred refusal or withholding the join after a Required refusal. | Showing a rule and creating a pair do not prove membership tracks the member’s choice. |
| 7: choices at invite and accept | [V1 user experience](../product/V1_USER_EXPERIENCE.md) (lines 36–42) keeps content out of pending invites and defaults direct sharing OFF; it does not describe the two choices at the invite and acceptance. [Remaining plan](../../tasks/V1_REMAINING_PLAN.md) (line 290) likewise only names the OFF default. [V1DirectContact.kt](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1DirectContact.kt) (53–77, 91–94) sets sharing false for invite/pending/accept; the local toggle requires ACCEPTED. [V1PeopleScreens.kt](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1PeopleScreens.kt) (140–168) has no invite or share-back choice, and [V1PendingDirectChat.kt](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1PendingDirectChat.kt) (65–112) retains no invite choice. | The pending restriction is compatible. The sender’s invite choice and the accepting person’s share-back choice are missing, and the local toggle does not prove authenticated on/off during a conversation. |

These findings are open against the existing issues. Do not change design, defaults, code or old records in this checklist.

## Preconditions and evidence

1. Select exact firmware/app versions and hashes, a supported board/phone set, current region, approved recovery artifacts, synthetic coordinates and an owner-present device plan. Establish an observation point for intended receivers and one for forbidden recipients. Use a separate approved host fixture for malformed/stale state; never inject it into a device outside its approved session.
2. Before any real case, verify protected location producer, authenticated recipient/roster, consent and stop paths, and the exact candidate’s copy, external-open and retention policy. Unknown permission, freshness, migration or timing limits are **BLOCKED** until defined by the owning task. Do not select a location source or retention policy here.
3. For each case record exact candidate and layer, rule/roster revision or conversation state, synthetic actor/recipient labels, each person’s action and elapsed time, admission/queue states, location events at each receiver and before/after counts, and verdict. Record fixture labels or hashes instead of real coordinates, keys, names or opaque device identifiers.

## Cases

| Case and layer | Action and required result | Extra evidence |
| --- | --- | --- |
| L01 · Host then device · requirement 6, F10/A7 | Creator selects Off and invites a member. Joining can succeed without location; neither side sends a group coordinate even if a fix is available and an old sharing toggle was on. A non-admin cannot change the rule. | Creator/other actor, signed rule revision, join result and zero coordinate events to the group. |
| L02 · Host then device · requirement 6, F10/A7 | Creator selects Preferred. Attempt with accept and with refuse as separate cases. Both may join; an agreeing member may send, a refusing member sends zero group coordinates and remains a member. | Prompt and choice, member admission and sharing state independently, per-recipient coordinate counts. |
| L03 · Host then device · requirement 6, F10/A7 | Creator selects Required. Attempt with agreement and with refusal or cancel. Refusal or cancel cannot complete joining or receive member-only location; agreement can join once other gates pass. | Join stage and final roster/membership, choice, observed zero pre-admission or refused location events. |
| L04 · Host then device · requirement 6, F10/A7 | Change to Off, and separately Preferred to Required while a member has refused. Off stops group sharing. Do not silently reactivate the opted-out member. Exercise stale/unknown rules and restart; never infer agreement or resume stale authorization. | Old/new revisions, consent transitions, stop time, post-change/restart transmission counts. Record candidate policy for existing-member transition before execution. |
| L05 · Host then device · requirement 7, F10/A7 | Send a conversation invite with the sender’s share choice on and off. While pending, declined or cancelled, no location is available to the recipient. After acceptance, the sender’s choice controls sender sharing. | Invite choice, pending/accept timestamps, first permitted event and zero pending/declined disclosures. |
| L06 · Host then device · requirement 7, F10/A7 | Accept an invite with and without share-back. Both actors’ choices remain independent. Run all four sender/recipient on/off combinations. No unrelated group or contact receives either coordinate. | Choice on each endpoint, acceptance event, each direction’s expected and observed counts. |
| L07 · Host then device · requirement 7, F10/A7 | In a started conversation, either participant can turn sharing on or off at any time. Toggle each side independently, repeat after reconnect, and stop while a send is queued. Stop prevents later sends and retries for that direction; the other direction is unchanged. In-flight packets already sent are not retroactively recalled. A change without an accepted conversation has no authority. | Toggle times versus queue/capture, first/last permitted event, refused operation and outcomes after reconnect. |
| L08 · Host then device · requirements 6–7, F10/A7 | Reject stale or unknown authority, mismatched peer/group, replay and invalid or absent fix. Enforce group cadence and unreachable-current-roster cutoff from D-058/D-059. Show current, stale, unavailable or disabled accurately with age and timestamp. Copy/open only by deliberate operator action in the approved policy; no automatic upload. | Rule/context and fault fixture, sender/receiver counts, measured interval and last current-roster contact, UI state and exact side-effect disposition. |
| L09 · Review / owner gate | Review exact evidence against all requirement sentences above. Findings without proved resolution remain open. A host pass and this checklist cannot accept real location behavior. | Per-case disposition, open finding and owning issue, independent review and later owner decision. |

## Stop and acceptance rules

Stop on a forbidden coordinate, unknown consent, wrong recipient or candidate, unexpected transmission, or loss of recovery readiness. Record **FAIL**, **BLOCKED** or **NOT RUN** and the observed boundary; none is PASS. Keep only sanitized evidence and do not widen the session or alter production behavior to get a passing result.
