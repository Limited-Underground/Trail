# Decision 0007: Shared Client Presentation Tracks

Status: accepted product direction; initial host-only shared-shell evidence
exists, while target and release evidence remain pending, 2026-08-14

## Decision

OpenTrail will develop two client presentation tracks over the same versioned
protocol and reusable, hardware-independent behavior in this repository:

1. an affordable Android companion paired one-to-one over BLE with a separately
   approved screenless LoRa device running the product firmware; and
2. the original self-contained touchscreen client with its own display, local
   input, power, GNSS, and radio hardware.

The laptop-only dual virtual-LCD simulator is a shared behavior and interaction
reference for both tracks. It is not a production runtime dependency, a third
client product, or normative wire-protocol authority.

The current first-release definition remains four self-contained field units.
The Android track is an additional future client path; it does not make a phone
or laptop a dependency of the standalone client, satisfy the existing
four-person pilot, or change current V1 progress.

An iPhone client and any app-store distribution approach remain undecided. This
decision grants no public product name, store-submission authority, account
activity, distribution approval, or hardware-support claim.

## Shared authority and separate adapters

Shared contracts should cover semantic UI state, message and alert meanings,
packet formats, delivery outcomes, privacy rules, and failure behavior. Each
track still owns its platform-specific adapters and evidence:

- Android owns its app lifecycle, accessibility, permissions, background
  behavior, approved-device bridge, packaging, signing, and distribution.
- The standalone client owns its ESP32 target composition, renderer and input
  adapters, power behavior, boot/recovery path, and physical display evidence.
- The simulator owns only local engineering presentation and injected bridge
  behavior. It must label synthetic versus live evidence and cannot manufacture
  a real-radio or physical-device claim.

Shared source does not permit one track's platform or hardware evidence to be
credited to another. A behavior accepted in the simulator must still be
retested on the applicable Android or standalone target.

## Why

One semantic and protocol authority reduces divergence between the lower-cost
companion path and the self-contained client while keeping hardware and
platform failures independent. The simulator provides fast two-client
iteration before complete physical displays are available, without making a
laptop part of field operation.

## Remaining gates

- bind the host-tested shared application shell and message/alert outcomes to
  separately accepted Android and standalone ESP32 target adapters;
- define renderer-neutral pairing and group-membership state before adding
  those workflows to any platform;
- select and approve the Android LoRa companion target and BLE transport;
- define Android privacy, permission, lifecycle, accessibility, signing, and
  distribution acceptance;
- bind and validate the standalone ESP32 renderer, input, radio, GNSS, storage,
  power, and recovery adapters;
- decide whether iPhone support or store distribution belongs in a later
  release; and
- repeat simulator-accepted flows on each actual target before any support or
  release claim.
