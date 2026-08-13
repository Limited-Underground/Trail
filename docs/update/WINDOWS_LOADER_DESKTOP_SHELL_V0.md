# Windows loader desktop shell v0

Date: 2026-08-12

## Current evidence

OpenTrail now has a real, buildable .NET 8 WPF desktop shell for the future
Windows firmware loader. It runs the existing privacy-safe USB/runtime
inspection pipeline, validates its reduced JSON document again inside the
application, and renders one card for each connected candidate.

The shell and its independent console tests build warning-free on the current
Windows host. Eight document and refresh scenario groups pass. The live inspection pipeline
underneath it previously produced `3 found · 3 inspected · 0 ready to flash`
for the two assembled Heltec V4 GPS bench clients and the packaged SenseCAP
Solar P1-Pro GPS repeater.

This is source/build and document-validation evidence. Visible launch, rendered
layout, accessibility, resizing, and refresh interaction have not yet been
accepted. Those checks require a separate interactive Windows review.

## Safety boundary

The desktop shell is deliberately inspection-only:

- it has no firmware file picker, firmware parser, signature adapter, or trust
  store;
- it has no approved physical-board profile resolver or low-level probe;
- it has no esptool, DFU, erase, write, reset, reboot, or recovery adapter;
- every firmware-writing control is disabled; and
- raw process errors, local ports, serial numbers, hardware-instance paths,
  device locations, pairing data, identities, and raw runtime replies are not
  exposed in its view model.

One revision authority allows only the newest active refresh to publish. A
later refresh invalidates an older result, completion is one-use, and closing
the window invalidates all pending output.

The shell cannot turn USB or installed MeshCore runtime identity into Flash
permission. The final firmware-write admission remains a separate pure gate,
and no code currently consumes an admitted result to mutate a device.

## Development workflow

From the repository root:

```powershell
.\tools\Test-WindowsLoader.ps1
```

For an interactive development launch after the build gate passes:

```powershell
dotnet run --project .\tools\windows-loader\OpenTrail.Loader\OpenTrail.Loader.csproj --framework net8.0-windows
```

The current shell finds the repository source tree and invokes
`tools/Get-OpenTrailLoaderInspection.py`. That is suitable for development and
bench review only; it is not a self-contained end-user package.

The loader has no external NuGet dependency. Its repository-scoped
`tools/windows-loader/NuGet.Config` clears package sources so validation does
not depend on or inspect a developer's private per-user NuGet configuration.

## Remaining gates

1. Visually inspect the real application with the three connected bench units,
   including common window sizes and repeated refresh behavior.
2. Review keyboard navigation, focus order, screen-reader labels, contrast, and
   high-DPI behavior.
3. Replace the development source-tree/Python dependency with a packaged,
   bounded inspection adapter.
4. Add an approved exact-board profile and low-level probe without weakening
   the privacy or fail-closed boundary.
5. Add signed-bundle selection and verification, then connect the one-use final
   admission result to a separately reviewed writer.
6. Prove interrupted write, readback, boot confirmation, rollback, ROM recovery,
   installer lifecycle, and clean-machine operation before any support claim.
