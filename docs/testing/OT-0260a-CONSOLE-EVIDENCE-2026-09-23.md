# OT-0260a Console scope and historical client evidence

Publication version, 2026-09-29: local workspace paths removed; dated technical
facts and evidence limits are unchanged. Original evidence is retained privately.

Observed 2026-09-23. Planning deliverable for owner review; no target selection,
implementation or physical acceptance.

## In plain English

Trail Console is a LoRa radio running Trail firmware with an ESP32 touchscreen
LCD, microSD storage and a battery pack. It is the owner's existing Console
direction, not an additional handheld product. There is reusable screen and
control software, but a working complete Console has not been demonstrated by
this review. You do not need to connect anything for this report.

## Current scope takes precedence

Approved OT-0260a revision 1 covers reconciliation, with remaining outcomes
[OT-0260 through OT-0263](../../tasks/BACKLOG.md). Ownership remains in OpenTrail,
baseline `46ca4af65d59377a3c068dfe69ae62182c99cbd9`. No repository move is proposed.

[Decision 0007](../decisions/0007-shared-client-presentation-tracks.md) preserves
shared semantic behavior with separate platform adapters. Its statement that
four standalone units form the first release, and Android is future-only, is
superseded by [Decision 0033](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md).
Today's base V1 remains phone/BLE/Heltec/direct LoRa/Heltec/BLE/phone. Console
does not delay or become a runtime dependency of that release.

The historical Gold one-screen and Platinum two-screen working names still
appear in [architecture](../ARCHITECTURE.md). They are historical naming and
configuration assumptions, not authorization to create two Console versions or
require a second screen. Likewise an old four-person pilot statement is not the
current V1 acceptance gate. This task identifies those assumptions; it does not
rewrite accepted historical evidence or silently expand today's product list.

## Evidence map

| Owner-defined element | Existing evidence | What it actually establishes / remaining gap |
| --- | --- | --- |
| LoRa and Trail firmware | [portable client composition](../platform/PORTABLE_CLIENT_COMPOSITION_V0.md), [tests](../../tests/host/portable_client_composition_tests.cpp) | Host structural review of independently supplied radio, GPS, storage, clock, entropy, power, display and input adapters. It neither selects a Console board nor proves a working boot/task composition. Historical GPS binding is not a new owner requirement for Console. |
| Screen/navigation and actions | [PortableUiShell](../../firmware/components/integration/include/opentrail/portable_ui_shell.hpp), [tests](../../tests/host/portable_ui_shell_tests.cpp) | Fixed-capacity host screen/action model with generation/revision ownership. Source reuse candidate; does not prove actual LCD/touch behavior or freeze all historical menu features into the first Console. |
| Visible interaction prototype | [dual virtual LCD report](DUAL_VIRTUAL_LCD_SIMULATOR_V0.md), [WPF renderer](../../tools/windows-simulator/OpenTrail.Simulator/VirtualLcdWindow.xaml.cs) | Windows simulator renders shared offers on 466x466 circular surfaces. Historical host/UI evidence is not ESP32 firmware, physical LCD pixels, touch calibration, enclosure or endurance evidence. A simulator is an engineering tool, not a third product or field dependency. |
| ESP32 target | [existing target tree](../../firmware/targets) | Existing Heltec bench/evaluation targets are exact-target evidence. No accepted Console touchscreen/microSD/battery integration was established. A successful Heltec build is not a Console build. |
| microSD | [composition/storage boundary](../platform/PORTABLE_CLIENT_COMPOSITION_V0.md) | Separate small persistent-state and replay-slot contracts provide reusable requirements, not a microSD filesystem/driver or storage-failure acceptance. Removable storage must not be silently treated as trusted key or authorization storage. |
| Battery and power | [composition power policy](../platform/PORTABLE_CLIENT_COMPOSITION_V0.md), [candidate inventory](../../hardware/INVENTORY.md) | Existing normalized power interface and policy checks are host-level. Exact battery, charger, enclosure, heat, runtime and interruption behavior remain unknown for Console. No borrowed runtime or capacity claim. |
| Recovery/failure display | [update recovery presentation](../../firmware/components/integration/include/opentrail/update_recovery_presentation.hpp), [tests](../../tests/host/update_recovery_presentation_tests.cpp) | Semantic status rendering, not an updater, recovery image or confirmed boot outcome. Actual target ownership and visible recovery remain separate gates. |

All historical results retain their original evidence layer. No new host test
run or hardware observation is claimed here. Maps and optional server/vehicle
integrations are not added to Console's first implementation by source reuse.

## End-to-end integration questions for the existing successors

The proposed flow to review is power-on -> accepted state restore -> truthful
screen -> deliberate input -> authorized Trail operation -> visible outcome ->
sleep/stop/restart and recovery. Rendering success must precede input acceptance
for that revision; stale touch events must not approve a newer operation.
Display/input or removable-card failure must not invent a successful radio
operation. A server being absent must not disable the base field path.

- **OT-0260b:** choose one exact ESP32/radio/touchscreen/card-reader/power
  combination and decide whether GNSS is included. Resolve shared buses, memory,
  pin allocation and recovery access before a target is built.
- **OT-0261a/b:** select the actual first screen flows, stored contents,
  removal/full/corrupt-card behavior, sleep/wake and battery behavior. Historical
  screens are candidates, not automatic requirements.
- **OT-0262a/b:** bind accepted adapters and ownership, then test stale input,
  rendering failure, storage failure, power interruption and optional-service
  loss through the complete target flow.
- **OT-0263a/b:** prove pixels/touch/readability, runtime/heat, recovery and exact
  supported combinations before operator documentation or release claims.

These are already registered planning tasks. No purchase, hardware session,
new product variant or implementation is approved by this report.

## Validation and closeout

Source/decision reconciliation and relative-link checks validate this inventory;
repository document checks and independent review precede submission. Completed
historical host work is not reopened. Only this local report changes; no V1
progress, public website status, firmware, repository layout or publication
changes. Owner acceptance of the report is separate from successor approval.
