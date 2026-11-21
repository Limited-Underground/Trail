# MeshTrail V1 location sharing

Design for F10/A7 and requirements 6 and 7. D-155 accepts the design decisions below and selects phone GPS for V1. [Milestone #102](https://github.com/Limited-Underground/Trail/issues/102) · [Consent #119](https://github.com/Limited-Underground/Trail/issues/119) · [Protected location #120](https://github.com/Limited-Underground/Trail/issues/120) · [Acceptance checklist](../testing/OT-176-LOCATION-SHARING-ACCEPTANCE-CHECKLIST.md)

## Group consent (requirement 6)

The admin chooses the policy. The board applies the current signed roster and the app shows the verified rule before joining. [Group pair design](GROUP_PAIRWISE_V1.md)

| Rule | Join choice and effect |
| --- | --- |
| Off | Join or cancel. Group location stays disabled for every member. The board cancels queued positions and clears pins when it installs an Off roster. |
| Preferred | Share and join, or join without sharing. Either choice can complete admission. A member who declines may receive permitted positions and choose to share later. |
| Required | Agree to share or cancel. Refusal leaves the person outside the roster. Wait for a fresh fix from the phone GPS before finishing admission. The app shows what it needs and offers Cancel. |

Bind consent to the exact join and signed rule. Keep the fix on the phone until membership and recipient authority are active. The protected join exchange can report source readiness without coordinates. A changed rule or audience clears queued copies and requires new consent when sharing would continue under the change. The matching roster update that completes the original join carries that join's consent into the new member state. Stop local sending immediately when consent is withdrawn. Leaving or removal invalidates the affected member's state and preserves other pairs. During a partition, show the change as pending for disconnected members. Their boards apply the signed change on receipt. D-059 limits eligible recipients to members heard from with the current roster within 10 minutes. Revisit a shorter authorization lease only if a tighter remote cutoff is needed.

When a member stops sharing under Required or declines a switch to Required, stop sending immediately. Keep their roster entry and pause group participation until they consent or leave. After consent, a temporary loss of phone GPS or app visibility still allows ordinary text.

Older app data uses two rules and forced sharing at join. Read the old values during migration and collect fresh consent before enabling location.

## Direct consent (requirement 7)

Offer Share when accepted to the inviter and Share back to the recipient. Each starts off. Hold pending choices locally and bind them to the exact authenticated invitation. Once both boards confirm the conversation is ready, each direction may start independently. Cancel, decline, expiry or loss of the app, board or conversation context invalidates the pending choice. A retained acceptance receipt cannot reactivate sharing. At any point in an active [direct conversation](DIRECT_CONVERSATIONS_V1.md), either person may start or stop their own sharing. Closing or blocking cancels pending positions and clears pins for that conversation.

## Phone GPS, lease and privacy

The MeshTrail App uses the phone's GPS. The Heltec V4 needs an add-on for onboard GNSS, which can be added later. Ask for foreground location permission after the person chooses to share. Keep GPS sampling scoped to the visible app and the selected conversation or group. Direct invitations collect intent only. A Required group join may check for a fresh fix after consent while keeping coordinates on the phone.

Once the audience is authorized, the app rounds coordinates to four decimal places and sends the latest fresh position over the protected Bluetooth link. Each update is bound to the current app attachment, lease and audience. The board owns a 15-second lease, renewed every five seconds by the authorized, visible app while permission and a fresh phone fix remain available. The board checks fix age and elapsed time using its own monotonic clock after admission. Renewing a lease cannot make an old fix fresh.

Stop sampling and clear outgoing phone state when the user stops sharing, revokes permission, turns off phone location, backgrounds the app or loses the board link. Send a protected stop when possible. The board cancels pending positions when it observes a stop or authority loss. The lease bounds any delay if it cannot receive the stop. Require explicit Resume after a background or link pause. At launch the board must still have a valid lease, a fresh fix and the exact active audience.

Show **Allow phone location** when permission is missing and **Turn on phone location** when location services are disabled. Show **Waiting for phone GPS** while a permitted phone has no fresh fix, **Sharing** while the board confirms an active lease and a fresh position, **Paused** when sharing needs Resume, and **Off** when consent is absent. A GPS or permission change cannot turn sharing on by itself.

Keep the latest fix and each received position only in bounded memory. Discard raw coordinates after rounding for transmission. Exclude coordinates and location history from logs, F11, exports and durable inbox. Show live coordinate details only while current scope authority permits it. Review external copy and open actions under [coordinate details #121](https://github.com/Limited-Underground/Trail/issues/121).

Send protected best-effort positions and stop events per eligible pair. Check consent, scope, epoch, fix age and radio budget before launch. Coalesce updates and prioritize text, rekey and roster traffic. Use durable replay admission and receiver freshness proof before showing a pin. Show a received pin as stale at 150 seconds and remove it by 240 seconds. Stop clears a pin on receipt. If the stop is lost, expiry clears it. Require fresh consent after rekey or authority loss.

## Radio fit and acceptance

D-133 and D-136 select the US 64-channel, 125 kHz hybrid route on 902.3–914.9 MHz, pending exact qualification. D-135 sets the separate 925.1 MHz/500 kHz interim bench. D-134 keeps Repeater transmit off in the first US release, and D-138 requires fresh pairing on the hopping profile.

Estimated full payloads are 138 B for a group position, 118 B for a group freshness grant, 92 B for a direct position and 72 B for a direct grant. Each fits D-137's 236 B size cap. At modeled SF7/125 kHz the group position takes about 226 ms and the grant about 200 ms. Six active members use about 12.8 aggregate RF seconds per 120 s for 30 positions and 30 grants before other traffic, hopping and contention. Use a nominal 120 s interval and keep D-058's 60 s minimum between group updates. The qualified scheduler may defer or drop location and must preserve the existing pairing and F5 deadlines. If 112 ms is confirmed as a burst ceiling, these intact frames fail the airtime check. Bring the owner costed framing and cadence options if the qualified burst limit or measured load cannot carry the design. Enable location only once it fits the qualified profile.

Verify the consent matrix, including Required refusal, all four direct choices, phone permission denied or revoked, stale or unavailable phone fixes, background or Bluetooth loss, immediate local stop and a delayed or missing stop receipt. Observe two real boards and phones, then measure the six-member load on the qualified profile. Record eligible deliveries, forbidden deliveries, age, congestion drops and launches after local revocation. Keep interim bench evidence separate from hopping measurements.
