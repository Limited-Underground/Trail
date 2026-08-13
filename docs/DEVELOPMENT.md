# OpenTrail Development Setup

## Current host toolchain

- Python 3.14 with `meshcore` 2.3.8, `meshcore-cli` 1.5.7, and `pyserial`
- MSYS2 UCRT64 GCC 16.1.0 under `C:\msys64`
- PowerShell 7

MSYS2 and its UCRT64 GCC package are intentionally installed on the development
laptop for repeatable native C++ testing. PlatformIO is not required yet because
OpenTrail has no approved board binding or production firmware target.

The Windows loader development shell additionally uses the .NET 8 SDK. The
repository `global.json` selects the latest installed compatible .NET 8 feature
band so newer SDKs do not silently become the build authority.

## Build and test the Windows loader shell

From the repository root:

```powershell
.\tools\Test-WindowsLoader.ps1
```

This restores the dependency-free test project, builds the WPF application in
Release mode with warnings treated as errors, and runs the strict loader
document scenarios. The complete `Test-Host.ps1` gate calls this script after
the C++ and Python/publication-safety checks. Restore uses the loader's
repository-scoped, source-free NuGet configuration and does not read a
developer's private per-user NuGet settings.

The current application is a development shell: it depends on the repository
source tree and local Python inspection script, and every firmware-writing
action is disabled. Read the
[desktop-shell evidence and remaining gates](update/WINDOWS_LOADER_DESKTOP_SHELL_V0.md)
before launching it interactively.

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
- two-slot persistent-configuration version, CRC, migration, safe-default, secret-separation, wear, and power-loss tests; and
- non-secret MeshCore temporary-channel lease, uncertain-response recovery, mismatch protection, and journal validation tests.

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
identifiers and non-sensitive markers, and sends three frames each direction.
It now uses the same non-secret lease journal and exact-name cleanup as the soak
harness, so an interrupted run can be recovered with
`Test-MeshCorePrivateSample.py --recover-only` and its
`meshcore-packet-v0-channel.json` journal. Port assignments may change. The
refactored script must complete one fresh physical packet-v0 run after the
active soak releases the ports before unattended packet-v0 use is claimed.

## Run the host-mediated critical-alert ACK proof

Build both host matrices, select their newest bridge/verifier CLIs, then supply
the two current Heltec companion ports and the SenseCAP repeater console port:

```powershell
.\tools\Test-Host.ps1
$ogRoot = 'D:\ESP32\OpenGauge'
Push-Location $ogRoot
.\tools\Test-Host.ps1
Pop-Location
$latest = Get-ChildItem .\build\host-tests -Directory |
  Sort-Object LastWriteTime -Descending | Select-Object -First 1
$ogLatest = Get-ChildItem (Join-Path $ogRoot 'build\host-tests') -Directory |
  Sort-Object LastWriteTime -Descending | Select-Object -First 1
python .\tools\Test-MeshCoreCriticalAlertAck.py `
  --port-a COM6 --port-b COM11 --repeater-port COM17 `
  --codec (Join-Path $latest.FullName 'critical_alert_bridge_cli.exe') `
  --opengauge-cli (Join-Path $ogLatest.FullName 'critical_alert_round_trip_cli.exe')
```

The script sends the public normative `OGA0` alert, supplies an explicit bench
trust context to the real C++ ingress/responder, returns the correlated `OGK0`,
checks every field, optionally admits the returned ACK through OpenGauge's real
authorization/session/replay/correlation ingress and reconstructed outbox,
snapshots the repeater read-only, and performs exact-name channel cleanup.
Reverse `--port-a` and `--port-b` for the second endpoint-role cycle. This is
host-mediated transport evidence, not authenticated persistent on-device
OpenTrail/OpenGauge firmware.

Add `--ack-outcome stale` to exercise a correlated terminal stale rejection.
That mode succeeds only when OpenGauge processes the rejection without a
delivery acknowledgement, reports `outbox_completed=false`, and records a
terminal failure. The default remains `accepted`.

Use `--ack-outcome rate-limited` for the retryable branch. It succeeds only
when the returned rejection releases exactly one queued OpenGauge retry, leaves
zero in flight and zero acknowledgements, and does not record terminal failure.

Use `--ack-outcome retry-then-accepted` for the four-leg sequence. It sends the
same alert again, returns a next-sequence accepted ACK, and invokes OpenGauge's
exact-backoff/same-frame/final-completion verifier.

If interrupted, recover only the retained lease before another run:

```powershell
python .\tools\Test-MeshCorePrivateSample.py `
  --port-a COM6 --port-b COM11 --recover-only `
  --journal .\build\hardware-test-state\meshcore-critical-alert-ack-channel.json
```

## Run a recoverable three-node bench soak

`Test-MeshCoreThreeNodeSoak.py` uses a low-duty temporary private channel
between two USB Companions and reads the repeater console at the beginning,
every ten probes, and the end. It checkpoints only redacted counters and
summaries. Its lease journal records the two port labels, ephemeral channel
name, and slot—but never the channel secret, device identity, key, coordinates,
or message marker.

First run a short smoke test and inspect its cleanup result:

```powershell
python .\tools\Test-MeshCoreThreeNodeSoak.py `
  --port-a COM6 --port-b COM11 --repeater-port COM17 `
  --duration-minutes 1 --interval-seconds 15
```

Only after that succeeds, choose a longer duration and conservative interval.
If the process or laptop stops before verified cleanup, do not start another
test. Reconnect the same two logical companions and run recovery against the
retained soak journal:

```powershell
python .\tools\Test-MeshCorePrivateSample.py `
  --port-a COM6 --port-b COM11 --recover-only `
  --journal .\build\hardware-test-state\meshcore-soak-channel.json
```

Recovery reads the journal, verifies the exact ephemeral channel name in the
recorded slot, clears only a matching slot, verifies zeroed channel state on
both nodes, and removes the journal. A different channel name is never erased.
