# Windows loader desktop shell v0

Date: 2026-08-12; updated 2026-08-13

## Current evidence

OpenTrail now has a real, buildable .NET 8 WPF desktop shell for the future
Windows firmware loader. It runs the existing privacy-safe USB/runtime
inspection pipeline, validates its reduced JSON document again inside the
application, renders one card for each connected candidate, and can inspect a
bounded local firmware-bundle candidate without granting device authority.

The shell and its independent console tests build warning-free on the current
Windows host. Forty-eight document, identity-safeguard, accessibility,
refresh/selection/snapshot-binding/device-match, process-boundary, USB-runtime/hardware-profile, firmware-bundle-candidate, and
packaged-inspection scenario groups pass. The source-free built-in C# path
reports `3 USB candidates found · 3
runtime-identified · 0 ready to flash`: two assembled Heltec V4 OLED GPS bench
clients and the packaged SenseCAP Solar P1-Pro GPS repeater.

This is source/build and document-validation evidence. A local self-contained
package has also passed source-free extraction, manifest/hash verification, and
launch smoke testing. The real production XAML now also has deterministic
rendered-layout evidence at desktop and minimum window sizes. Real keyboard,
Narrator, high-DPI/system-theme, repeated-refresh, and clean-machine acceptance
remain separate gates.

The source now provides F5 refresh through the same bounded command as the
button, an explicit keyboard focus visual, automation live regions for changing
summary/error text, and per-device accessible summaries and blocker help. Card
connection and inspection status are bound to the validated document instead
of hard-coded presentation copy. Empty, oversized, or control-bearing blocker
text is rejected. These are compiled/tested source properties, not evidence of
acceptance with Narrator or another assistive technology.

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
dark gray on the gray button face.

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

This is deterministic rendered-layout and resize evidence on the current host.
It is not evidence of mouse/keyboard operation, focus traversal, Narrator live
announcements, Windows scaling above 100%, alternate system themes, repeated
live refresh behavior, or clean-machine operation.

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

The current retained package has 464 payload files and is 72,098,432 bytes. Its
SHA-256 is
`96720DD25C63151F303522A07D93D65315CC2CC0ED380BEF62D159A47B92CDC6`.
The manifest fixes the capability boundary to inspection only, explicitly
permits Windows USB-family discovery and fixed MeshCore runtime-identity
queries plus bounded local bundle structure/image-SHA-256 inspection and the
RSA-PSS-3072/SHA-256 verifier, and declares the exact selected-device matcher.
It also declares that no production signer is configured, rejects signature
admission and authoritative hardware-profile status, and marks the
display name as a working identity pending attorney review. The local
`artifacts/windows-device-utility` output is intentionally ignored by Git; no
binary has been published.

## Remaining gates

1. Exercise repeated live refresh and explicit selection in the visible app
   with the three connected bench units without starting maintenance mode.
2. Accept keyboard navigation, focus order, live announcements, labels,
   contrast, high-DPI/system-theme behavior, and the remaining interactive
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
