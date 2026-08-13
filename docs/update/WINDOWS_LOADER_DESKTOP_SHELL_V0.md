# Windows loader desktop shell v0

Date: 2026-08-12; updated 2026-08-13

## Current evidence

OpenTrail now has a real, buildable .NET 8 WPF desktop shell for the future
Windows firmware loader. It runs the existing privacy-safe USB/runtime
inspection pipeline, validates its reduced JSON document again inside the
application, renders one card for each connected candidate, and can inspect a
bounded local firmware-bundle candidate without granting device authority.

The shell and its independent console tests build warning-free on the current
Windows host. Fifty-five document, identity-safeguard, accessibility,
production-window refresh/selection/keyboard/automation-peer/high-DPI/resize/contrast-theme, snapshot-binding/device-match,
process-boundary, USB-runtime/hardware-profile, firmware-bundle-candidate, and
packaged-inspection scenario groups pass. The source-free built-in C# path
reports `3 USB candidates found · 3
runtime-identified · 0 ready to flash`: two assembled Heltec V4 OLED GPS bench
clients and the packaged SenseCAP Solar P1-Pro GPS repeater.

This is source/build and document-validation evidence. A local self-contained
package has also passed source-free extraction, manifest/hash verification, and
launch smoke testing. The real production XAML now also has deterministic
rendered-layout evidence at desktop and minimum window sizes plus deterministic
125%, 150%, and 200% scaled rendering plus one deterministic contrast profile.
Three consecutive production-window reads through the packaged Windows
USB/runtime adapter now pass on the current host with all three bench candidates
and zero ready to flash. Physical keyboard input, Narrator, live Windows contrast-theme
switching, visible mouse-driven repeated refresh, and clean-machine acceptance
remain separate gates.

The source now provides F5 refresh through the same bounded command as the
button, an explicit keyboard focus visual, automation live regions for changing
summary/error text, and per-device accessible summaries and blocker help. Card
connection and inspection status are bound to the validated document instead
of hard-coded presentation copy. Empty, oversized, or control-bearing blocker
text is rejected. These are compiled/tested source properties, not evidence of
acceptance with Narrator or another assistive technology.

The production-window runner now opens a second real `MainWindow` and follows
WPF focus traversal through Refresh, the current connected-device list
selection, the enabled bounded bundle action, and back to Refresh. The first run
found noninteractive blocker content in the Tab sequence. That `ItemsControl`
is now nonfocusable and the device list is one Tab group, so disabled Flash
actions are skipped. This is focus-manager evidence, not physical key injection,
Narrator, or assistive-technology acceptance.

Routed-key acceptance now covers the real shown device list in both production
layouts. Native Right/Left traverses the wide row; native Down/Up traverses the
900×620 two-plus-one wrap. That run found native WPF End could remain on the
first item at minimum width because the horizontal wrap panel resolved a row
edge instead of the final candidate. The list now intercepts only Home/End,
selects the exact first/last current item, brings it into view, and moves focus
with it. Status text and bounded bundle availability follow every selection.
Routed F5 also reaches the production refresh command, republishes all three
cards, clears selection, disables bundle selection, and restores the Refresh
control. This does not prove physical key injection, keyboard hardware/layout
variants, or assistive-technology behavior.

The same production window now has direct UI Automation peer acceptance.
Device metadata is attached to each selectable `ListBoxItem`, not its inner
visual border. The actual three item peers expose validated public summaries,
blocker help, list-item control type, and selection-item pattern without putting
their internal candidate ordinals in the item name or help. Refresh exposes a
named Button/Invoke contract; the device surface exposes single optional
selection; disabled Flash exposes its name/help while remaining unavailable.
Summary, safety, selection, bundle, and error peers expose their current visible
messages instead of fixed labels when live-region events are raised. This is
automation-peer evidence; Narrator announcement timing, wording, verbosity,
Braille output, and other assistive technologies remain untested.

When the repository inspection script is unavailable, the same application now
uses Windows SetupAPI directly instead of the laptop's access-denied CIM/PnP
inventory path. It privately selects only exact allowlisted USB VID/PID pairs
and holds each validated COM name only inside a bounded fixed-command runtime
probe. The companion probe sends MeshCore app-start and device-info only; it
skips rather than decodes the private BLE-PIN bytes. The repeater probe sends
only `board`, `ver`, and `get role`. No arbitrary command or line-toggle/reset
surface exists. The resulting document contains generic ordinals plus
allowlisted runtime model, role, and firmware; it never contains a COM name,
transport label, serial number, hardware-instance ID, raw reply, BLE PIN,
device identity, pairing data, key, or coordinate. The live C# path identifies
two Heltec V4 OLED MeshCore USB companions and one SenseCAP Solar MeshCore
repeater. These are installed-runtime identities, not exact received-hardware
or support claims.

Each recognized card now contains an explicit hardware-profile evidence panel.
It names only a vendor-family candidate, separates the USB/runtime observation
from the published vendor-family baseline, labels the result `Runtime candidate
only`, and explains the deliberate maintenance restart or DFU/bootloader step
still required. Unknown USB devices receive no restart offer. This adapter is
tested to keep `authoritative_for_flash` false for every outcome; it neither
performs nor schedules a reset.

The panel now also carries a strict future-maintenance contract. A recognized
candidate permits at most one supervised attempt in a session, requires an
operator disruption confirmation before that attempt, and blocks every retry
afterward. If maintenance entry or automatic reset fails, the operator must
stop; normal USB enumeration and runtime inspection must be restored before a
later session may even be considered. Unknown devices have an attempt limit of
zero. The pure policy has deterministic C# coverage, but no UI action or device
adapter consumes it yet.

The `Select firmware bundle` action now opens one local `.fwbundle` read-only.
The utility requires an exact three-entry ZIP, a bounded canonical manifest,
the complete declared image length, and a matching streamed image SHA-256. It
does not expose the selected path or retain the raw manifest, image, or
signature bytes. It verifies RSA-PSS-3072/SHA-256 over the exact canonical
manifest when that signer is pinned in its immutable public-key catalog. The
packaged catalog is deliberately empty, so a local candidate still displays a
blocking message. Production signer custody/trust, protected revocation and
release-generation policy and admission composition do not exist. See the
[candidate bundle format](FIRMWARE_BUNDLE_CANDIDATE_FORMAT_V0.md).

Bundle inspection is revision-bound to the connected-device view. Selection is
disabled until one valid device snapshot is published and the operator selects
exactly one current card. Beginning any refresh
immediately clears the prior bundle presentation and invalidates an in-flight
inspection token; an old async result cannot publish after the snapshot
changes. A failed or timed-out refresh leaves selection disabled, while window
close invalidates all remaining authority. This prevents stale UI state and
creates no Flash authority.

The connected-device cards now use a keyboard-accessible single-selection
list. Selection retains only the reduced generic candidate ordinal already in
the privacy-safe document. The selected card gets a visible navy border plus a
live status message stating that selection is not Flash permission. Refresh or
window close clears the selected ordinal, and changing it invalidates an
in-flight or previously displayed bundle result. No local port, serial number,
hardware-instance ID, device identity, or pairing data enters this state.

After inspection, a separate pure matcher compares the bundle manifest with
the selected card. It accepts only an authoritative received-unit profile and
checks exact hardware-profile ID, processor, product role, board-revision
range, minimum bootloader schema, and image-size capacity. Runtime model names,
USB family, installed MeshCore role, and vendor-family candidate text are never
substituted. The current three cards therefore report that exact-device match
is unavailable. Even a complete match is only compatibility evidence; it
cannot approve a signer or release, enable a writer, or grant Flash permission.
See the [match contract](WINDOWS_LOADER_DEVICE_BUNDLE_MATCH_V0.md).

The owner-selected visual direction is a classic Windows 95-style service
utility rather than a modern dark dashboard. The compiled XAML now uses square
gray surfaces, navy section treatment, compact Microsoft Sans Serif text,
pixel-aligned borders, and an application-owned beveled button template. The
custom template fixes the observed platform-theme failure that rendered
disabled action labels nearly white on white; disabled labels are explicitly
dark gray on the gray button face. A focused follow-up measured the prior
classic pairing at 3.79:1, darkened the semantic disabled brush to `#404040`,
and added a production-control assertion requiring at least 4.5:1; the accepted
pairing is now above 5.5:1.

## Rendered layout evidence

The test executable has an opt-in WPF renderer that creates the real
`MainWindow`, publishes a fully validated privacy-safe three-card inspection
document through the production display path, and captures production XAML.
It does not connect to, reset, or mutate a radio. On 2026-08-13, the generated
1600×900 desktop view, 900×620 minimum view, and 900×620 scrolled view were
inspected pixel-for-pixel.

That review found two defects: the content root could render transparent, which
hid black summary copy against a black backing surface, and the nested list
measured device cards as one unbounded horizontal row, clipping the third card
at minimum width. The production XAML now owns an explicit gray background and
constrains the device list to the visible content width. At 1600×900 all three
cards fit in one row. At 900×620 two cards fit in the first row and the third
wraps below; the outer vertical scroll reaches it while the bundle and safe-mode
footers remain fixed. Selected-state borders, status copy, Refresh, firmware-
bundle, and both disabled Flash labels are readable in the inspected renders.

The executable now embeds an explicit `PerMonitorV2` DPI-awareness manifest
with the legacy `true/pm` fallback. The same production window also renders the
900×620 logical minimum at 125%, 150%, and 200% pixel density. Each bitmap has
the exact expected pixel dimensions and DPI, remains nonblank, and preserves
measurable Refresh, firmware-selection, and scroll-viewport surfaces. Pixel
review accepted the selected state, bundle blocker, safe-mode boundary, and
disabled Flash label at all three scales.

This is deterministic rendered-layout, resize, and high-DPI evidence on the
current host. It is not evidence of mouse/keyboard operation, focus traversal,
Narrator live announcements, real monitor-to-monitor DPI movement, alternate
system/high-contrast themes, repeated live refresh behavior, or clean-machine
operation.

## System-aware contrast-theme evidence

The custom classic presentation no longer hard-codes colors on production
controls. Semantic dynamic brushes cover window/panel/surface text, header,
information, warning, critical, success, selection, focus, disabled copy, and
the complete beveled-button template. Classic mode supplies the same
gray/navy/white palette reviewed above.

At application startup, the theme owner reads `SystemParameters.HighContrast`.
When active, it maps all paired colors from WPF `SystemColors`: window with
window text, control with control text, highlight with highlight text, and gray
text only for disabled content. Accessibility, color, general, visual-style,
and window preference notifications cause the palette to be reread on the UI
dispatcher, including customized contrast colors, without adding any device or
file authority.

The production-window harness injects one deterministic black/white/yellow
palette through that same resource-application path. It requires at least 7:1
contrast for body and disabled text, confirms the actual Refresh and disabled
Flash controls receive the dynamic brushes, retains all three cards, current
selection, and scroll access, and rejects blank output. Pixel review at 900×620
accepts the selected yellow border, status/safety copy, bundle blocker,
safe-mode boundary, and both footer labels. This does not replace live testing
with every built-in or user-customized Windows contrast theme.

A second shown-window scenario establishes the selected last wrapped card as
the keyboard-focused item, scrolls it into view, and then applies classic →
deterministic high contrast → classic without recreating the window. Each
transition must preserve the exact selected model and focused container,
vertical scroll offset, accessible item name, enabled bounded bundle action,
disabled Flash action, and zero-ready summary. Each selectable container now
uses the explicit accessible focus visual. Review of the first high-contrast
transition render found focus and selection both using yellow; the
deterministic focus brush is now white while the selected outline remains
yellow. The fixed minimum-size classic and high-contrast renders were reviewed.
This proves in-window dynamic-resource and state preservation only; the run did
not change the Windows theme setting or observe real OS notification timing.

A third shown-window scenario starts at 1120×760, resizes to the 900×620
production minimum, and returns to 1120×760. The three current 330-pixel cards
must form one row, then the expected two-plus-one wrap, then one row again. The
same last-card model, selected index, and generated keyboard-focused
`ListBoxItem` must survive. After one initial `BringIntoView`, the stricter run
found minimum reflow could leave the selected card below the viewport and drop
its keyboard focus. The production window now queues one post-layout update
only when resize began with focus inside a current selection, verifies that
exact selection is unchanged, brings its container into view, and restores its
focus. Two race checks move focus to Refresh and to a different nonselected
card before the deferred update settles; neither may be reclaimed, and the
callback rejects an inactive, hidden, or minimized utility. Without any
post-resize test-side scrolling, the selected rectangle must
intersect a nonempty viewport and the vertical offset must remain finite and
within the current scrollable range. The visible selection status, public UI
Automation item name, three list-item peers, enabled bounded bundle action, and
disabled Flash action must also remain intact. This is not physical resize
input, a real monitor/DPI transition, or Narrator acceptance.

## Production-window refresh state evidence

The public `MainWindow` constructor and packaged inspection source remain
unchanged. An internal constructor can supply a controlled validated read-only
inspection function to the same production window during host acceptance. The
acceptance runner performs three refresh/selection cycles in one STA session.
Every refresh must republish the three cards, clear the previous selection,
disable bundle selection until a current card is selected, restore the Refresh
button, preserve the zero-ready summary, and discard the selection-bound bundle
footer. Every subsequent selection must enable only bounded local bundle
inspection and publish current-device wording.

That production-window run found one stale presentation state: after a
successful inspection but before device selection, the bundle footer retained
the earlier `waiting for device inspection` message. Successful publication now
sets `No firmware bundle selected` with an explicit current-device-selection
blocker. Selecting a card then changes to the selection-bound wording. Three
complete cycles pass. An opt-in follow-up also runs this production window
against the packaged Windows USB/runtime adapter. Three consecutive live reads
each republished the three connected candidates, kept zero ready to flash,
cleared selection, kept bundle selection disabled, and restored Refresh. This
does not substitute for visible mouse/keyboard input, Narrator, or clean-machine
acceptance.

The visible application identity is now composed from one C# boundary rather
than embedded throughout XAML and inspection copy. Its preliminary working
display is `Limited Underground Trail Device Utility`, accompanied by a visible
`ATTORNEY REVIEW PENDING` status. The presentation document itself uses the
brand-neutral role `Device Utility`, and public blocker copy says `Product
target role` rather than the repository name. Tests reject standalone `LU`,
`LU Link`, `LU Studio`, `LU` plus a model number, retired `TLU` / `LUT` /
`LUTrail` compact names, and every use of `®`. Internal `OpenTrail.Loader`
namespaces, script paths, repository links, `OT-*` records, and schemas remain
stable engineering identifiers. This is a replaceability and policy safeguard,
not attorney clearance or adoption for public distribution, sales, or hardware
marking.

## Safety boundary

The desktop shell is deliberately inspection-only:

- its local file picker feeds only the bounded candidate structure/image-
  digest/signature inspector; its immutable public-key catalog has no
  production signer and it has no protected revocation state, rollback policy
  owner, or release-admission adapter;
- its physical-board resolver is candidate-only; it has no approved
  authoritative received-board profile or low-level probe;
- its future maintenance policy permits only one confirmed attempt per session
  and requires normal runtime recovery after failure, but it exposes no action
  capable of starting that attempt;
- it has no esptool, DFU, erase, write, reset, reboot, or recovery adapter;
- every firmware-writing control is disabled; and
- raw process errors, local ports, serial numbers, hardware-instance paths,
  device locations, pairing data, identities, and raw runtime replies are not
  exposed in its view model.

One revision authority allows only the newest active refresh to publish. A
later refresh invalidates an older result, completion is one-use, and closing
the window invalidates all pending output.

A separate authority gives the current device snapshot the same one-use
protection for local bundle inspection. Refresh or close invalidates that
snapshot and every unfinished bundle result.

Helper stdout is limited to 256 KiB and discarded stderr to 16 KiB. Timeout,
cancellation, invalid output, or excessive output triggers best-effort
termination of the exact helper process tree owned by the inspection. The raw
failure content is never shown in the UI.

The shell cannot turn USB or installed MeshCore runtime identity into Flash
permission. The final firmware-write admission remains a separate pure gate,
and no code currently consumes an admitted result to mutate a device.

## Development workflow

From the repository root:

```powershell
.\tools\Test-WindowsLoader.ps1
```

To regenerate the three deterministic production-XAML renders during that
same warning-free run:

```powershell
$env:OT_LOADER_VISUAL_OUTPUT = Join-Path $env:TEMP 'OpenTrail.Loader.VisualEvidence'
.\tools\Test-WindowsLoader.ps1
```

The renderer writes only the fixed desktop, minimum, and scrolled-minimum PNG
names beneath that explicitly selected directory. Leaving the environment
variable unset keeps the ordinary test run headless.

The validation command resolves a unique directory below the system temporary
directory, gives each .NET project its own intermediate and output subtree,
and removes only that exact validation tree in a `finally` path. This keeps an
old or protected WPF cache/executable from blocking a later source validation
and avoids accumulating validation artifacts.

For an interactive development launch after the build gate passes:

```powershell
dotnet run --project .\tools\windows-loader\OpenTrail.Loader\OpenTrail.Loader.csproj --framework net8.0-windows
```

When launched from the repository, the shell invokes
`tools/Get-OpenTrailLoaderInspection.py` for the existing detailed development
view. When no source tree is present, it automatically uses the built-in
privacy-safe Windows USB/runtime adapter instead. That path requires no Python
interpreter, MeshCLI, CIM/WMI, shell, or network service and can show the
allowlisted installed runtime model, role, and firmware while retaining every
exact-board and Flash blocker. The fallback is present in the self-contained
local engineering package. That does not establish an authoritative hardware
profile, supported end-user package, installer, or clean-machine acceptance
result.

Candidate-bundle selection is local and read-only. A structurally accepted file
does not enable Flash; the writer control remains disabled regardless of the
selection result.

The loader has no external NuGet dependency. Its repository-scoped
`tools/windows-loader/NuGet.Config` clears package sources so validation does
not depend on or inspect a developer's private per-user NuGet configuration.

## Local packaging workflow

Build the inspection-only `win-x64` engineering ZIP from the repository root:

```powershell
.\tools\New-WindowsDeviceUtilityPackage.ps1
```

Independently verify a retained ZIP by extracting it into a new temporary
directory, checking its complete manifest, and briefly launching the exact
packaged executable:

```powershell
.\tools\Test-WindowsDeviceUtilityPackage.ps1 -ArchivePath <path-to-zip>
```

On an interactive Windows bench with the expected three connected candidates,
the same verifier can run native cross-process UI Automation selection and
three read-only Refresh cycles without opening the firmware picker:

```powershell
.\tools\Test-WindowsDeviceUtilityPackage.ps1 -ArchivePath <path-to-zip> `
    -RunUiAutomationAcceptance -ExpectedDeviceCount 3 -RefreshCycles 3
```

The current retained package has 464 payload files and is 72,103,016 bytes. Its
SHA-256 is
`6D6A487B23B44E67E8CCBC37F1FD61B001C2514C31600400613FE2609E5AB5F7`.
The manifest fixes the capability boundary to inspection only, explicitly
permits Windows USB-family discovery and fixed MeshCore runtime-identity
queries plus bounded local bundle structure/image-SHA-256 inspection and the
RSA-PSS-3072/SHA-256 verifier, and declares the exact selected-device matcher.
It also declares that no production signer is configured, rejects signature
admission and authoritative hardware-profile status, and marks the
display name as a working identity pending attorney review. The local
`artifacts/windows-device-utility` output is intentionally ignored by Git; no
binary has been published.

The opt-in external acceptance uses stable non-private Automation IDs to find
the exact packaged Window, Refresh, dynamic status regions, device list,
bounded bundle action, and disabled Flash action. Each of the three device
items must expose ListItem, SelectionItem, and ScrollItem semantics, a public
summary and blocker explanation, and its 1-of-3 position without a candidate,
COM-port, or MAC-like identifier. Three cycles select one item, verify only the
bounded bundle action becomes available, invoke Refresh, and require selection
to clear while all three items, zero-ready status, and disabled Flash return.
Each refreshed public-name multiset must match the initial privacy-safe roster.
This proves native automation-client access to the source-free executable on
this host. It does not prove physical key/mouse input, Narrator wording or
timing, installer behavior, or clean-machine compatibility.

The same external run subscribes one compiled native handler to the packaged
window's LiveRegionChanged subtree. In each selection/Refresh cycle it requires
current bundle and selection events followed by selection cleared, bundle
waiting, inspection busy, bundle reset, the settled zero-ready summary, and the
selection prompt. Adjacent exact duplicates may be collapsed, but missing,
misordered, unexpected, stale, empty, or privacy-bearing events fail. The exact
handler is removed before the owned process closes. This demonstrates native
event delivery to an assistive client, not Narrator speech or announcement UX.

Routed keyboard acceptance now starts F5 from the focused last wrapped card.
Since refresh replaces that item's container, the production window explicitly
returns focus to Refresh after success and failure (but not during initial
load). A successful next Tab enters the fresh list; a failed refresh retains
focused enabled Refresh with the unavailable error and all actions blocked.

## Remaining gates

1. Exercise selection and refresh through physical mouse and keyboard input
   with the three connected bench units; native external UI Automation already
   passes three source-free selection/Refresh cycles.
2. Accept keyboard navigation, focus order, live announcements, labels,
   contrast, live system/high-contrast theme behavior, real monitor DPI
   changes, and the remaining interactive
   resize cases with real Windows UI and assistive-technology review.
3. Build and lifecycle-test an installer, then repeat package and installer
   acceptance on a clean Windows machine; add code signing before distribution.
4. Add an approved authoritative received-board profile and low-level probe
   without weakening the privacy or fail-closed boundary; installed MeshCore
   runtime identity and published vendor-family specifications alone cannot
   close this gate. A maintenance restart remains a separate operator-approved
   future action. Its pure gate is already limited to one confirmed attempt per
   session and requires verified normal-runtime recovery after failure.
5. Independently review the cross-tool RSA-PSS vector, approve offline signer
   custody, pin production public keys, implement protected revocation plus release-
   generation policy, then connect a fully verified bundle to the one-use final
   admission result.
6. Prove interrupted write, readback, boot confirmation, rollback, ROM recovery,
   installer lifecycle, and clean-machine operation before any support claim.
