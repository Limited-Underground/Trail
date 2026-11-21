# MeshTrail V1 direct conversations

Design for F9/A6 and requirement 5. D-155 accepts the direct-conversation decisions below. Requirement 6 governs group location and grants no direct-contact permission. Requirement 7 governs each person's independent location choice. [Milestone #100](https://github.com/Limited-Underground/Trail/issues/100) · [Discovery #113](https://github.com/Limited-Underground/Trail/issues/113) · [Consent #114](https://github.com/Limited-Underground/Trail/issues/114) · [Block #115](https://github.com/Limited-Underground/Trail/issues/115)

## Invite, protect and end a conversation

| Stage | Proposed behavior |
| --- | --- |
| Find and invite | People nearby shows the chosen public name and fresh presence only. Visibility defaults on. Turning it off cancels queued and future presence transmissions. Exchange a short-lived invite code in person. Both owners verify the same attempt and the expected peer boards. Bind the durable identity during the verified invitation. Freeze discovery freshness, work and airtime limits after measurement. Discovery, a shared group, a name match and a code alone grant no permission. |
| Accept or decline | The recipient explicitly accepts or declines. The boards persist matching consent and reconcile the committed results using the existing pairing recovery pattern. Show Finishing connection while only the local result is known. Enable the composer and release any staged draft only after both boards have confirmed and fresh protected F5 traffic is ready. Decline, cancel, expiry or a changed peer keeps content gated. |
| Reconnect | The board owns the exact conversation, peer and current session. The app reads live state over the protected link and binds responses to the current attachment. Rekey accepted peers after restart under D-059. Retire the old traffic and queued work before admitting the successor. A pending invitation cannot inherit prior consent. |
| End or block | Ending retires local conversation authority and cancels queued messages and positions. Blocking persists a denial for the verified peer that covers direct discovery actions, invites, traffic and recovery. Report success after durable readback. Require a fresh invite after End or Unblock. Group membership follows its own authorization. |

Offer default-off sharing choices in the invite and acceptance flows. Hold each choice on the phone until the authenticated direct conversation is active. A change of peer, expired operation or lost context clears the pending intent. Discovery and invitations carry no coordinates or group state. They never start GPS or request location permission. Once Ready, each person who chose to share can use their phone GPS under the foreground permission and board lease in the [location design](V1_LOCATION_SHARING.md). Missing location permission or a fix leaves the direct conversation usable. Ending or blocking clears pending positions and the affected sharing and pin state.

## Radio and storage

Reuse the pairwise [Packet-v1 profile](PACKET-V1-HOST-PROFILE.md) with a distinct direct-conversation scope and [pairing recovery](F5-R2-BOARD-PAIRING-PLAN.md). D-133 and D-136 select the US 64-channel, 125 kHz hybrid profile on 902.3–914.9 MHz, pending qualification for the exact board and firmware. D-135 reserves 925.1 MHz at 500 kHz for the interim bench. Keep profiles separate and require fresh pairing for hopping (D-138). The first US release keeps Repeater transmit off (D-134).

D-137 caps the complete LoRa payload at 236 B. Packet-v1 overhead is 60 B, leaving at most 176 B before application fields. Smaller codec or profile limits still apply. Keep direct text to one frame (D-060), prioritize protected traffic over discovery and positions, and budget each invitation stage, channel wait, ACK and retry inside the existing pairing and F5 deadlines. The 236 B limit is a size check. A possible 112 ms burst ceiling still needs confirmation and measured fit. Bring costed options to the owner before changing deadlines or frame splitting.

D-059 permits eight direct conversations per board. The current 32-allocation F5 store budget can exhaust within repeated rekey sweeps (I-080). Prove bounded reuse of retired slots with fresh logical generations and counter domains while group and direct records coexist. Bring a revised storage budget to the owner if the stores cannot coexist. Refuse exhaustion or uncertain storage with existing records intact.

Host acceptance exercises both owners and the protected app boundary, including refusal, expiry, stale actions, interrupted saves, restart, group isolation, block persistence and capacity. Owner acceptance uses two boards and two phones for a successful conversation in both directions and for denial and recovery cases.
