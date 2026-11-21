# MeshTrail V1 location sharing

Proposed design for F10/A7 and requirements 6 and 7. [Milestone #102](https://github.com/Limited-Underground/Trail/issues/102) · [Consent #119](https://github.com/Limited-Underground/Trail/issues/119) · [Protected location #120](https://github.com/Limited-Underground/Trail/issues/120) · [Acceptance checklist](../testing/OT-176-LOCATION-SHARING-ACCEPTANCE-CHECKLIST.md)

## Group consent (requirement 6)

The admin chooses the policy. The board applies the current signed roster and the app shows the verified rule before joining. [Group pair design](GROUP_PAIRWISE_V1.md)

| Rule | Join choice and effect |
| --- | --- |
| Off | Join or cancel. Group location stays disabled for every member. The board cancels queued positions and clears pins when it installs an Off roster. |
| Preferred | Share and join, or join without sharing. Either choice can complete admission. A member who declines may receive permitted positions and choose to share later. |
| Required | Agree to share or cancel. Refusal leaves the person outside the roster. Propose waiting for a fresh local fix before finishing admission. The app explains the wait and offers Cancel. |

Bind consent to the exact join and signed rule. Hold the fix locally until membership and recipient authority are active. A changed rule or audience clears queued copies and requires new consent when sharing would continue under the change. The matching roster update that completes the original join carries that join's consent into the new member state. Stop local sending immediately when consent is withdrawn. Leaving or removal invalidates the affected member's state and preserves other pairs. During a partition, other boards apply the signed change when they receive it. D-059 limits eligible recipients to members heard from with the current roster within 10 minutes.

Older app data uses two rules and forced sharing at join. Read the old values during migration and collect fresh consent before enabling location.

## Direct consent (requirement 7)

Offer Share when accepted to the inviter and Share back to the recipient. Each starts off. Hold pending choices locally and bind them to the exact authenticated invitation. Once both boards confirm the conversation is ready, each direction may start independently. Cancel, decline, expiry or loss of the app, board or conversation context invalidates the pending choice. A retained acceptance receipt cannot reactivate sharing. At any point in an active [direct conversation](DIRECT_CONVERSATIONS_V1.md), either person may start or stop their own sharing. Closing or blocking cancels pending positions and clears pins for that conversation.

## Source, delivery and privacy

Propose qualified board GNSS and a 15-second firmware lease renewed every five seconds while the authorized app is visible. Age out a disconnected or backgrounded sender and require an explicit Resume. Show Waiting for fix, Sharing, Paused or Off from protected status. Keep only the latest quantized fix and received position per scope in bounded memory. Exclude coordinates from logs, F11, exports and durable inbox. With a missing capability, leave sharing off.

Send protected best-effort positions and stop events per eligible pair. Check consent, scope, epoch, fix age and radio budget before launch. Coalesce updates and prioritize text, rekey and roster traffic. Use durable replay admission and receiver freshness proof before showing a pin. Stop clears a pin on receipt. If the stop is lost, expiry clears it. Require fresh consent after rekey or authority loss.

## Radio fit and acceptance

D-133 and D-136 select the US 64-channel, 125 kHz hybrid route on 902.3–914.9 MHz, pending exact qualification. D-135 sets the separate 925.1 MHz/500 kHz interim bench. D-134 keeps Repeater transmit off in the first US release, and D-138 requires fresh pairing on the hopping profile.

Estimated full payloads are 138 B for a group position, 118 B for a group freshness grant, 92 B for a direct position and 72 B for a direct grant. Each fits D-137's 236 B size cap. At modeled SF7/125 kHz the group position takes about 226 ms and the grant about 200 ms. Six active members use about 12.8 aggregate RF seconds per 120 s for 30 positions and 30 grants before other traffic, hopping and contention. Use 120 s as a proposal and keep D-058's 60 s minimum between group updates. The qualified scheduler may defer or drop location and must preserve the existing pairing and F5 deadlines. If 112 ms is confirmed as a burst ceiling, these intact frames fail the airtime check. Return with costed options before changing framing or deadlines.

Verify the consent matrix, including Required refusal, all four direct choices, immediate local stop and a delayed or missing stop receipt. Observe two real boards and phones, then measure the six-member load on the qualified profile. Record eligible deliveries, forbidden deliveries, age, congestion drops and launches after local revocation. Keep interim bench evidence separate from hopping measurements.

## Owner questions

| Question | Recommendation |
| --- | --- |
| What happens to members who stop sharing under Required or decline a switch to Required? | Stop sending immediately. Keep their roster entry and pause group participation until they consent or leave. After consent, keep ordinary text available during a temporary loss of GNSS or app visibility. |
| Should Required wait for a fix on join? | Start with a visible wait and Cancel. Review field delays before allowing admission with consent and no fresh fix. |
| Is board GNSS while the app is visible enough for V1? | Start with board GNSS and the short lease. Evaluate phone GPS or background sharing separately if needed. |
| How precise, frequent and persistent should a position be? | Start with four decimal places, a 120 s nominal interval, stale at 150 s and removal by 240 s. Keep live coordinate details available during consent. Review external copy/open with the coordinate-details scope in #121. |
| How quickly must policy changes reach disconnected members? | Start with signed roster convergence, the current contact cutoff and a visible pending state. Consider a shorter authorization lease only if a tighter remote cutoff is required. |
| What if the qualified burst limit or measured group load cannot carry this proposal? | Bring framing and cadence options back for review. Enable location only after the chosen profile fits. |
