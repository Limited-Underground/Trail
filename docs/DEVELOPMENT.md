# OpenTrail Development Setup

## Current host toolchain

- Python 3.14 with `meshcore` 2.3.8, `meshcore-cli` 1.5.7, and `pyserial`
- MSYS2 UCRT64 GCC 16.1.0 under `C:\msys64`
- PowerShell 7

MSYS2 and its UCRT64 GCC package are intentionally installed on the development
laptop for repeatable native C++ testing. PlatformIO is not required yet because
OpenTrail has no approved board binding or production firmware target.

## Run deterministic host tests

From the repository root:

```powershell
.\tools\Test-Host.ps1
```

The script finds the MSYS2 compiler, adds its runtime directory to the current
process path, compiles with C++17 and strict warnings-as-errors, and runs:

- radio transport contract/fake tests;
- packet-v0 codec tests; and
- packet/transport integration tests; and
- identity, group-membership lifecycle, reset, rename, revoke, and alias-collision tests.

Generated executables live under ignored `build\host-tests`.

## Inspect connected MeshCore USB companions

Disconnect every MeshCore browser tab from Web Serial first, then run:

```powershell
.\tools\Test-MeshCoreUsbNodes.ps1
```

The script discovers compatible Espressif USB ports and prints a redacted health
snapshot. It does not print identities, coordinates, keys, PINs, or channel
contents.

## Repeat the bounded packet-v0 hardware proof

Confirm both antennas are attached and both nodes use the intended regional
radio settings. Build host tools, then run with the ports currently assigned by
Windows:

```powershell
.\tools\Test-Host.ps1
python .\tools\Test-OpenTrailTwoNodeV0.py --port-a COM6 --port-b COM11
```

The hardware script creates a temporary private channel, uses ephemeral test
identifiers and non-sensitive markers, sends three frames each direction, and
erases and verifies the channel in a `finally` block. Port assignments may
change. Do not run it through an unattended automation until recovery from a
failed cleanup has been separately tested.
