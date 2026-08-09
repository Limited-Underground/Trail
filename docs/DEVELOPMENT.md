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
- packet-v0 codec and packet/transport integration tests;
- identity, local membership, reset, rename, revoke, and alias-collision tests;
- bounded group invitation, mutual-auth obligation, promotion, epoch-rekey, revocation, and recovery-policy tests;
- delivery policy, acknowledgement/retry/expiry, duplicate-window reboot, and lost-ACK integration tests;
- controlled-forwarding role, TTL, loop suppression, group isolation, and congestion simulations;
- priority reservation, preemption, rate, stale/failure, and delivery integration tests; and
- diagnostics compile/runtime filtering, timestamp/tag, redaction, sanitization, and sink-backpressure tests; and
- GPS provider, validation, stale-boundary, recovery, and no-UTC behavior tests;
- position payload validation/round-trip and packet/transport integration tests; and
- deterministic LoRa airtime formula and invalid-input tests;
- two-slot persistent-configuration version, CRC, migration, safe-default, secret-separation, wear, and power-loss tests.

Generated executables live in per-run directories under ignored
`build\host-tests` so a stale Windows process cannot block the next compile.

## Inspect connected MeshCore USB companions

Disconnect every MeshCore browser tab from Web Serial first, then run:

```powershell
.\tools\Test-MeshCoreUsbNodes.ps1
```

The script discovers compatible Espressif USB ports and prints a redacted health
snapshot. It does not print identities, coordinates, keys, PINs, or channel
contents.

## Preflight an incoming Wio Tracker L1 Pro

Before pairing, entering DFU, or flashing the candidate Wio unit, run:

```powershell
.\tools\Get-WioTrackerL1Preflight.ps1 | Format-List
```

This is enumeration only. It records serial-port names, public-safe matching
PnP names when Windows allows the query, redacted USB-registry names as a
fallback, and a mounted `TRACKER L1` DFU volume. It deliberately does not open
a serial port, reset, pair, erase, flash, or read files. Follow
`hardware/WIO_TRACKER_L1_PRO_BRINGUP.md`; normal shipping Bluetooth Companion
operation should be preserved before recovery is tested.

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
