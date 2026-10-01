# OT-0261a Console semantic touch-flow proposal

Prepared 2026-09-29 under approved revision 1 for owner review. This specifies
behavior and evidence, not screen pixels, firmware or a working Console.

## What the user would see

Start at a clear local status screen, establish the approved local authority,
then perform a deliberate Trail action and see what actually happened. Pending,
failed and uncertain outcomes remain distinct. A missing card, GPS or internet
does not masquerade as failed radio authorization, nor disable otherwise valid
local field operation. Recovery instructions remain visible when normal actions
are unavailable.

The accepted [Console proposal](OT-0260b-CONSOLE-PROPOSAL-2026-09-29.md) provides
the baseline: one standalone touchscreen assembly with LoRa, microSD and battery.
Onboard GNSS is outside the recommended first assembly unless its use is later
accepted. Exact hardware and storage purpose remain conditions. This flow does
not choose a panel, add a map pipeline, enable all historical menus or invent
new radio commands.

## Minimum surfaces and actions

| Surface | Required information | Permitted interaction |
| --- | --- | --- |
| Starting | Restoring/validating local state; no premature Ready claim. | Only safe cancellation or the reviewed recovery route; no action requiring authority not yet established. |
| Setup/authorization required | Which prerequisite is missing, without exposing secrets or inventing a remotely trusted identity. | Enter only the accepted local provisioning/confirmation workflow. Its exact target mechanism remains separately gated. |
| Home/status | Independent authority, radio, storage, location-if-present and power states. Empty content is explicit. | Navigate to the accepted Trail action or message list; settings and recovery remain discoverable. |
| Action review | Exact intended destination/context and action, with a current semantic revision. | Deliberate confirm or cancel; critical actions retain the existing held-confirmation requirement if included in accepted scope. |
| Pending result | Local acceptance and current known transport stage; user navigation does not imply cancellation. | Back/status and only a cancellation action actually supported by the owning operation. Disable duplicate submission. |
| Result/detail | Known received/sent information, failure or uncertainty and applicable next action. | Dismiss, inspect status or deliberately start a new permitted attempt; never automatic resend from reopening a page. |
| Recovery | What is unavailable, what remains known and the bounded reviewed next step. | Only currently offered recovery actions; no hidden factory reset, erase or authority recreation. |

The minimum Trail action is an already accepted semantic request exposed by the
current application owner. The historical shell includes templates, quick status
and critical-alert candidates; this plan does not approve every candidate feature.
An unavailable action is explained or omitted, never rendered as functional with
a fake success result. The next integration mapping must name the selected
requests and their actual completion producers before implementation.

For a concrete reference flow, propose the shell's existing message-template
request: Home -> Messages -> Compose -> choose an offered template -> review the
current recipient/context and text -> Confirm -> Pending -> result/detail.
Cancel before Confirm returns without a request; Back after Confirm only changes
the view. An empty offered-template or recipient list explains the missing
prerequisite and offers no Send action. Outbox retains the known result while
opening another page; reopening a detail never repeats the request. Exact allowed
template contents remain part of the accepted application policy, not arbitrary
new text invented by the renderer. This is a proposed use of the existing semantic
request, not evidence that the target currently sends it.

## Outcome words must match evidence

| Evidence supplied by the owning operation | Honest presentation | Must not imply |
| --- | --- | --- |
| Required local prerequisites verified | Ready for this named action. | Other devices are online, a recipient is reachable or RF transmission is permitted merely from saved region. |
| Local queue accepted | Pending / queued locally. | RF emission, recipient receipt or phone display. |
| Bridge/radio submission observed | Submitted / awaiting supported confirmation. | Durable endpoint receipt; old shell bridge-ack state alone is insufficient. |
| Exact protected endpoint admission acknowledged | Peer device accepted, if the final adapter proves that precise meaning. | Human read, phone display, guaranteed delivery or rescue. |
| Definite refusal before admission | Failed with bounded reason and safe next choice. | No-effect certainty when a command may already have reached the radio. |
| Lost contact or ambiguous in-flight result | Outcome uncertain; retain known prior stages. | Delivered, definitely not sent, or automatically safe to retry. |
| Independent recovery evidence | Recovered to the specifically verified state. | The earlier failed action succeeded. |

The [current secure transport](../security/SECURE_LORA_KEY_TRANSPORT_V0.md)
defines the protected-ACK limit. The existing
[PortableUiShell](../../firmware/components/integration/include/opentrail/portable_ui_shell.hpp)
contains `queued`, bridge-observed and bridge-acknowledgement states; those names
are historical host semantics, not authenticated delivery proof. A future adapter
must preserve the distinction or request an explicit semantic extension. It must
not relabel bridge acknowledgement as end-device acceptance for convenience.

## Complete touch and lifecycle sequence

| Transition / owner | Required effect | Negative/recovery case |
| --- | --- | --- |
| Power-on / boot owner | Validate state, clock and adapters while authority-dependent actions are unavailable. | Partial restore, entropy not ready or storage ambiguity leaves a specific unavailable state, not default trust. |
| State to screen / presenter | Render one current semantic offer successfully before accepting its action slots. | Failed render retires that offer; old visible content cannot authorize a new action. |
| Touch / checked input | Resolve the displayed revision and current ownership at action time. | Late touch, repeated confirm, changed destination or expired owner is rejected without sending. |
| Confirm / operation owner | Issue exactly the permitted bounded request and show pending. | Closing a dialog or navigating away neither repeats the action nor fabricates cancellation. |
| Completion / event owner | Apply only exact live-operation evidence and preserve truthful partial progress. | Late response for a retired operation cannot update a new request or restore lost authority. |
| Optional capability loss / capability owner | Update only affected features and keep unrelated status/actions truthful. | No GPS blocks local-position production, not unrelated messaging; absent/full SD blocks its selected storage feature, not granting or revoking radio authority. |
| Sleep/wake / lifecycle owner | Invalidate old input revisions and re-establish time, transport readiness and presentation before new actions. | No assumption that radio receive readiness or a confirmation survived sleep. |
| Restart/recovery / persistence owner | Restore only verified state; pending ambiguous work remains uncertain until reconciled. | No blind retry, forced reset, replay-window clearing or resurrection of old traffic keys. |

Low power may deliberately constrain a capability under an accepted measured
policy; independence does not promise that every feature works without power.
Security-storage failure is distinct from optional microSD failure. The former
can require traffic containment; the latter must not be treated as trusted key
storage or silently overwrite authority to appear healthy.

## Review cases and successor mapping

OT-0261b owns absent/full/corrupt/removable storage and interrupted-power policy.
OT-0262a maps selected requests to actual interfaces/build targets; it must reuse
the checked presentation/input contract and supply real outcome evidence rather
than test Booleans. OT-0262b turns the transitions into tests: failed render before
touch, touch after owner replacement, repeat confirmation, late completion after
cancel, disconnect after possible send, sleep during pending work, card loss while
messaging and restart with damaged authority. Each must assert both the visible
meaning and forbidden side effect.

OT-0263a separately proves physical readability, touch/hold behavior, recovery,
radio and measured endurance on the exact assembly. OT-0263b may document only
that accepted supported set. Simulator rendering and existing host tests cannot
establish pixels, calibration, glove use or physical user acceptance. The
[registered plan](../../tasks/OPTIONAL_PRODUCTS_PLAN.md) retains those gates.

No screen dimensions, timing thresholds, battery promise, new critical feature,
hardware selection or protocol change is made. The artifact's acceptance is an
explicit startup/action/result/recovery flow with empty/offline/error behavior.
Document/link validation and independent review are reported at batch closeout;
no behavioral tests, hardware, V1 credit or public capability change occurred.
