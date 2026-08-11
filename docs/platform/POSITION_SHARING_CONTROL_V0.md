# Local position-sharing control v0

Status: **host-tested semantic privacy boundary; no renderer, target task, radio
transmission, or physical-device claim**

## Purpose

Position sharing needs a visible local privacy control, not only an internal
scheduler API. This adapter connects the host-tested position-broadcast
scheduler to the existing checked local display/input contract without giving
a renderer access to coordinates or broad application authority.

The scheduler-only mapping remains the reusable lower-level foundation. Target
composition must use the runtime-aware overload documented in
[Outbound Position Safety v0](OUTBOUND_POSITION_SAFETY_V0.md), so a permanent
outbound clock fault cannot be mistaken for ordinary stopped sharing.

It has two independent functions:

1. convert one scheduler-status snapshot into a fixed semantic `UiFrame`; and
2. apply only `start_position_sharing` or `stop_position_sharing` to one
   scheduler.

It does not poll GPS, call scheduler service, encode or submit a position,
compose a packet, touch the radio, change group membership, send an emergency
alert, or alter update/recovery state.

## Presentation states

Every presentable frame uses a nonzero caller-owned boot-local revision and one
of these fixed mappings:

| Scheduler condition | Notice | Attention | Enabled action |
| --- | --- | --- | --- |
| Healthy and stopped | `position_sharing_stopped` | Information | Start |
| Active with no current error | `position_sharing_active` | Information | Stop |
| Active without a current fix | `position_sharing_waiting_for_fix` | Warning | Stop |
| Active after encode/sink pressure or failure | `position_sharing_deferred` | Warning | Stop |
| Invalid policy, clock rollback, time exhaustion, or incoherent active error | `position_sharing_failed` | Critical system fault | None |

Recoverable conditions retain stop because the scheduler is still active and
will retry at its injected retry interval. Terminal conditions expose no false
restart action. Once an ordinary active scheduler is explicitly stopped, the
stopped state takes precedence over a prior recoverable error.

The frame contains no coordinate, peer identity, address, message, free text,
credential, key handle, or renderer-specific control ID. Target renderers
choose localized labels and layout for the known enums.

## Control behavior

Start calls `PositionBroadcastScheduler::start(now_ms)` exactly once. Success
only arms the scheduler and makes a current fix due; this adapter never calls
`service()` and therefore cannot submit a payload. Scheduler policy/time errors
remain typed and produce no state-change claim.

Stop calls `PositionBroadcastScheduler::stop()` and immediately disables future
service submissions. Repeated start while active and repeated stop while
stopped are successful no-ops with `state_changed=false`.

Every unrelated or unknown UI action is rejected before scheduler access. The
checked local-interface revision contract separately rejects input for a frame
that is no longer current, so a delayed press or touch cannot apply an obsolete
start/stop meaning.

## Host evidence

Ten deterministic groups cover:

1. stopped presentation on button and touch capability shapes;
2. checked start resolution that arms without sink access;
3. checked active presentation and immediate stop;
4. missing-fix warning with stop retained;
5. not-ready/full sink pressure without false success;
6. encode and sink failure as stoppable retry states;
7. action-free invalid-policy, clock-regression, and time-exhaustion faults;
8. revision-zero refusal and stale-frame input rejection;
9. unrelated and unknown action rejection without mutation; and
10. idempotent repeated actions plus typed start rejection.

The focused executable passes 100/100 repeats. The complete 52-executable
OpenTrail host matrix plus all Python and publication-safety checks pass.

A separate ten-group runtime-aware suite covers real source-failure/rollback
precedence, coherent-status validation, no-action fault presentation, stale
Start rejection, and safe idempotent Stop. It also passes 100/100 repeats.

## Remaining gates

- define exact localized wording, icons, color-independent cues, and layout;
- prove start, stop, waiting, deferred, and failure behavior on each physical
  button/touch target, including glove/wet/distracted-use review;
- assign target task ownership and boot-local frame revisions without races;
- connect scheduler service to one exact GPS, authenticated packet/priority
  path, direct-radio adapter, and final cadence policy;
- verify reboot/default/privacy expectations and persistent preference policy;
  and
- measure physical input/render latency, radio/GPS concurrency, power, GPS
  loss/recovery, sink pressure, and long-session behavior.
