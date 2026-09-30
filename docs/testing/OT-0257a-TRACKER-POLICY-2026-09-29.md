# OT-0257a Tracker privacy and reporting policy proposal

Prepared 2026-09-29 for owner review under approved revision 1. Planning only;
the policy below is proposed behavior, not implemented or physically tested.

## What the owner would get

A Tracker reports the owner's equipment location to one deliberately enrolled
receiver. The operator can see whether reporting is enabled and can stop it
locally without a working radio or GPS. The receiver distinguishes a newly
observed position from an older position, a missing fix and a stopped session.
Restart does not start sharing. No route history or server upload is proposed
for this first scope.

This develops the accepted [tracking-only proposal](OT-0256b-TRACKER-PROPOSAL-2026-09-29.md).
It preserves its owner-equipment, single-receiver, direct-radio and boot-stopped
baseline. The recommendations below are the specific choices for this task's
owner review; accepting the earlier proposal did not choose numeric reporting
or retention values.

## Explicit policy choices

| Policy | Recommendation for review | Decision or evidence still needed |
| --- | --- | --- |
| Consent | Deliberate start after showing the selected recipient and what will be shared; no consent inferred from power-on, pairing, enrollment or a prior session. | Owner accepts the equipment-only use case and this start behavior. A new tracked-person use case requires a separate consent review. |
| Recipient | Exactly one enrolled receiver; bind permission to authenticated identity and current membership, never a display name, discovered address or phone Boolean. | Exact identity/provisioning contract remains an implementation prerequisite, not selected by this policy. |
| Recipient change | Stop and retire queued work before replacing or revoking a recipient. New recipient requires fresh enrollment and deliberate start. | No automatic migration of sharing permission to a replacement receiver or phone. |
| Data | Compact current position plus the bounded validity/freshness evidence required by the reviewed receiver contract. | No raw sensor stream, unrestricted text, device identifiers in diagnostics or map payload. Exact encoding remains compatibility-reviewed. |
| Cadence | A finite reporting interval with latest-fix coalescing, bounded backpressure and no catch-up burst after an outage. A retry is not a new unconstrained schedule. | OT-0257b supplies power/radio evidence. Owner chooses desired responsiveness; interval, retry/expiry and age bounds must then be reviewed together. No numeric defaults are invented here. |
| Retention | Recommend no Tracker route-history log and no automatic export. Keep only bounded working state and the receiver's latest observation while needed for its visible status. | Owner accepts no-history scope. A maximum age/display-retention rule and clear behavior must be frozen before implementation; longer history is a separate explicit choice. |
| Stop | Immediate local stop prevents future submissions, retires still-owned queued reports and leaves an unmistakable stopped indication. | Choose the exact physical control during target selection; do not rely solely on a remote stop. Already transmitted data or recipient copies cannot be recalled. |
| Restart/reset | Boot stopped after normal restart. Reset removes the selected authority/configuration according to the reviewed reset contract; setup is required again. | Logical clearing is not a physical secure-erasure claim. No historical permission silently resumes sharing. |

The concrete owner review is therefore consent/start behavior, recipient-change
rules, no-history retention, responsiveness goals and stop/reset policy. Exact
timings are deliberately unresolved until bounded power/airtime evidence exists.
The plan is complete with those named decisions; firmware must not substitute
arbitrary values or treat an unanswered question as approved policy.

## State and truthfulness model

Sharing intent and location quality are separate axes. The receiver cannot know
that a silent remote Tracker is stopped; it must not infer remote consent state
from missing packets.

| State | Tracker behavior | Receiver meaning |
| --- | --- | --- |
| Disabled | No reporting session and no new position submissions; deliberate start required. | Show disabled only from an authenticated current stop/state observation. Otherwise report last-known state and loss of contact. |
| Enabled, fresh fix | Scheduler may offer the latest eligible fix within accepted cadence. | Current only while validated freshness conditions hold; receipt is not a guarantee of present physical position. |
| Enabled, unavailable fix | Keep explicit waiting-for-fix indication and local stop; do not offer an old fix as new. | No current fix. A retained earlier observation remains separately marked old, or absent if cleared. |
| Stale observation | Do not relabel an aged fix by changing its timestamp. | Show old/last observed with bounded age evidence, never a current marker. |
| Enabled, link/backpressure loss | Coalesce bounded work; show reporting deferred/unconfirmed and retain stop. | A missing report ages normally; radio silence does not prove stopped tracking, distance or device failure. |
| Authority, clock or storage fault | Contain new reporting; indicate fault and require the applicable recovery plus deliberate start. | No fresh-data or sharing-authority claim from an old packet or reconnection. |

Recoverable fix/radio loss may resume eligible reporting only within the same
still-enabled, visibly waiting session whose identity and authority remain valid.
It cannot change disabled to enabled. Stop, restart, recipient change or terminal
fault invalidates that session; later fix arrival, reconnect or a stale callback
cannot revive it. This distinction avoids silently re-enabling sharing while
also avoiding a false claim that every missed fix means consent was withdrawn.

## Start-to-finish ownership and recovery

| Transition / owner | Required effect | Refusal and negative case |
| --- | --- | --- |
| Setup / enrollment owner | Verify exact recipient and durable authority before offering start. | Unknown/changed identity, interrupted save or ambiguous state gives no reporting permission. |
| Start / local control | Bind deliberate current input to the displayed recipient and fresh session; obtain checked time internally. | Delayed input, old display revision or copied UI status cannot start a replacement session. |
| Fix to offer / location and scheduler | Validate fix, age and cadence; coalesce delayed work. | Missing/invalid/stale fix, clock rollback or full sink does not manufacture a current report or unlimited queue. |
| Offer to radio / protected transport | Preserve separate accepted, transmitted and receiver-admitted outcomes. | A sink success or radio send is not proof the receiver or phone displayed the location. |
| Receive / authorized endpoint | Authenticate sender/context, reject replay, classify freshness using valid evidence. | New receipt time must not make an old/replayed source fix fresh. Independent clocks cannot be subtracted without a reviewed correlation. |
| Stop/revoke / control and queue owners | Cancel future work and retire the old session while retaining safe replay/identity rules. | A late radio completion cannot re-enable sharing; no promise to retract already emitted bytes. |
| Restart / persistence owner | Verify retained configuration with sharing stopped. | Old permission, uncertain checkpoint or a lower restored epoch cannot reopen traffic. |

## Existing evidence and successor tests

[Sharing control](../platform/POSITION_SHARING_CONTROL_V0.md) supplies host-tested
active/stopped/waiting/deferred semantics. Its runtime integration must use
[outbound safety](../platform/OUTBOUND_POSITION_SAFETY_V0.md) and
[command authority](../platform/OUTBOUND_POSITION_COMMAND_V0.md), including
clock-independent stop. These are reuse boundaries, not complete Tracker evidence.

OT-0257b should derive target/power selection criteria from the explicit policy
parameters. OT-0258a must identify real producer, receiver and persistence owners;
OT-0258b must turn each table's refusal into an executable scenario, including
stop concurrent with fix arrival, receiver replacement, restart while deferred,
late receipt and corrupted retained state. OT-0259a measures cadence, freshness,
loss and power separately; OT-0259b documents only supported behavior. These
successors already exist in the [plan](../../tasks/OPTIONAL_PRODUCTS_PLAN.md).

No hardware, behavior test, implementation, new protocol, publication or V1
completion credit results from this proposal. Repository document/link checks
and independent review validate the planning artifact only; batch closeout owns
their final results. Public website capability status is unchanged.
