# OT-0256b Tracking-only product proposal

Prepared 2026-09-29 for owner review. This is a proposed product decision,
not implemented Tracker firmware or a supported hardware declaration.

## What is proposed

Start with an owner-operated Tracker attached to the owner's equipment, with
location received by one explicitly authorized Trail receiver. The receiver's
authorized phone presents the location. The Tracker has no user messaging
feature. A phone may assist setup, but continuous phone proximity and internet
access should not be required once an explicitly enabled tracking session starts.
This is a proposed first use case, not a decision already made by the owner.

This keeps the initial flow concrete: the operator deliberately enables tracking,
the device obtains a valid location, a permitted receiver receives it, and the
operator can stop reporting. A radio submission alone must never be shown as
successful delivery. Missing GPS or a missing receiver must be visible as missing
or old information, never a fabricated current location.

The owner-approved boundary is tracking-only, not two-way user communication.
Protocol acknowledgements and authenticated setup can still be necessary;
they do not add a chat feature. Tracker remains optional to base V1.

## Choices to accept or change

| Choice | Recommended first scope | Alternative and consequence |
| --- | --- | --- |
| Operator and tracked subject | The equipment owner operates a Tracker on their own equipment. | Person or animal tracking adds distinct consent, attachment and safe-use requirements; name that intended use before freezing policy. |
| Permitted recipients | One explicitly enrolled receiver, with location shown through its authorized phone. | Multiple recipients require an explicit recipient-management, revocation and protected-distribution design. |
| Setup and start/stop | Authenticated local setup plus a deliberate, locally available stop control and a clear active/stopped indication. Phone setup is a candidate workflow. | A phone-free setup workflow needs an exact trusted local control/confirmation design; a remote-only stop leaves an avoidable radio-loss dependency. |
| First operating path | Direct authenticated LoRa to the selected receiver; no server dependency. | Relay or server collection remains a separate extension with its own compatibility and privacy review. |
| Restart | Boot stopped; require deliberate re-enabling after state verification. | Automatic resume would require a separately accepted explicit opt-in policy and evidence that stale or damaged state cannot restart sharing. |

Specific owner decisions: accept or replace the equipment-tracking use case;
identify whether one receiver is sufficient; choose phone-assisted versus
phone-free setup; and accept or revise the recommended restart policy.
These questions are the review output allowed by this task's acceptance,
not missing facts silently filled with assumed approval.

## Proposed operation and recovery contract

The responsibilities below describe future integration. They are not claims that
the current components already form a working Tracker.

| Before state / owner | Trigger and required effect | Forbidden effect | Failure or recovery |
| --- | --- | --- | --- |
| Unconfigured / provisioning owner | Deliberate authenticated enrollment binds the operator and permitted recipient. | Taking a received alias or unauthenticated radio field as permission. | Ambiguous or interrupted persistence leaves reporting disabled. |
| Configured, stopped / local control | Explicit start creates the current reporting session only after authority, clock and location-safety checks. | Reconnect, stale input or boot silently starting reporting. | Refuse start with an observable reason; retain stop access. |
| Active, valid fix / location and scheduler | Offer a bounded fresh position under accepted cadence and transport permission. | Sending an old fix as current, unbounded queuing, or claiming delivery from enqueue. | No fix/clock fault suppresses fresh reports; receiver ages its last observation independently. |
| Report offered / transport and receiver | Preserve separate admitted, transmitted and receiver-observed outcomes. | Manufacturing a delivery receipt or treating RF silence as a precise distance. | Bounded expiry/backpressure; no unlimited replay after link recovery. |
| Active / local stop authority | Stop cancels future offers and retires queued work where the transport still owns it. | Claiming to retract bytes already transmitted or erase a recipient's earlier copy. | Show stopped state independently of receiver availability; later input needs fresh authorization. |
| Restart or reset / persistence owner | Verify retained authority while reporting stays disabled; reset clears the intended configuration under an explicit policy. | Damaged storage granting recipient access or resetting replay state into unsafe transmission. | Contain, show recovery-required status and require deliberate setup. |

No numeric cadence, age threshold, retention duration or battery runtime is
selected here. OT-0257a must make each policy choice explicit and OT-0257b must
tie power/target criteria to measurements. The receiver needs an observation
time/age model; device timestamps from independent clocks cannot be subtracted
as if synchronized. A loss indication is distinct from tracking being disabled.

## Reuse and interface limits

The [accepted inventory](OT-0256a-TRACKER-EVIDENCE-2026-09-23.md) identifies
location validation, compact encoding and bounded scheduling as reusable
components. The [sharing control](../platform/POSITION_SHARING_CONTROL_V0.md),
[outbound safety](../platform/OUTBOUND_POSITION_SAFETY_V0.md) and
[outbound command](../platform/OUTBOUND_POSITION_COMMAND_V0.md) contracts provide
host-level authority boundaries, not a deployable Tracker composition.

Future work must bind a real location provider, one checked clock, authorized
start/stop, protected recipient identity, durable state and truthful receiver
presentation. Reusing a position codec does not establish that the current
direct security profile transports the proposed reporting object. Any required
wire/receiver change needs an explicit versioned compatibility review before
implementation. No raw vehicle data, unrestricted text or map payload is added.

Non-goals are chat, guaranteed rescue, covert location collection, universal
receiver compatibility, historical-route storage, autonomous enrollment,
unlimited range and unattended recovery through unverified state. Historical
Wio or Heltec evidence does not select or validate a Tracker board.

## Acceptance and next gate

This proposal supplies intended operator, subject, provisioning and endpoint
choices with explicit unanswered owner questions, as required by OT-0256b.
After owner acceptance, OT-0257a can freeze privacy/reporting policy. Target,
implementation, physical evidence and release remain separate successor gates
in the [registered plan](../../tasks/OPTIONAL_PRODUCTS_PLAN.md).

Preparation was a read-only source/contract review followed by documentation.
No tests of device behavior were run; the flow is proposed, not observed.
Repository document and link validation is reported with the batch closeout.
No firmware, hardware, V1 completion credit or public website status changed.
No publication is authorized or required by this planning scope.
