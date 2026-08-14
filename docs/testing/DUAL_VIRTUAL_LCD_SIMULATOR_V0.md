# Dual Virtual-LCD Simulator v0

Status: **host-tested laptop-only synthetic simulator shell; no live USB,
MeshCore, LoRa, physical-display, target-firmware, installer, or supported-
hardware claim**, 2026-08-14.

## Purpose

The first simulator increment opens two separate Windows virtual-LCD windows so
Client A and Client B behavior can be exercised before two complete physical
touchscreen clients exist. It runs locally on one laptop and requires no cloud,
server, phone, or account.

The simulator is a shared interaction reference for the planned Android
companion and self-contained touchscreen tracks described in
[Decision 0007](../decisions/0007-shared-client-presentation-tracks.md). It is
not a production dependency or a substitute for target acceptance.

## Current composition

One .NET 8 WPF process creates two independently owned windows. Each presenter
owns one client ID while a shared bridge owns the injected transport boundary:

```text
Virtual LCD A -> Client A state -> simulated companion A
                                      |
                              bounded local loopback
                                      |
Virtual LCD B -> Client B state -> simulated companion B
```

The current executable injects two in-memory simulated companions. It does not
enumerate, open, configure, reset, flash, or otherwise access a USB device. It
does not implement the MeshCore companion protocol. Closing one window stops
and disposes only that presenter; the other window remains independent.

## Fixed local boundaries

- Client A and Client B have separate connection state, outgoing queues,
  message histories, alert histories, sequence spaces, and public errors.
- One private opaque endpoint can be assigned to at most one client. Endpoint
  values cannot be rendered as transport paths or identifiers.
- Public device copy is allowlisted to fixed family labels. Transient ports,
  serial numbers, MAC addresses, channel names, keys, coordinates, and raw
  protocol replies are absent from snapshots and UI copy.
- A client outgoing queue holds at most 32 commands. Message history holds the
  newest 128 entries, alert history the newest 32, and each synthetic peer
  inbox at most 64 pending observations.
- Chat text is 1-160 printable characters and alert text is 1-120 printable
  characters. Full queues and invalid text reject without discarding another
  client's state.
- The injected receive operation is a nonblocking poll; it must return `null`
  promptly when no complete observation is ready and honor cancellation before
  I/O. Identity-bearing background callbacks are not part of the interface.
- A connected session becomes stale after the configured freshness interval.
  A backward clock observation produces a public-safe fault instead of fresh
  data.
- Window commands are serialized. A failed message queue request retains the
  draft and Compose screen. Closing suppresses later snapshot rendering and
  prevents duplicate disconnect/dispose work.

## Truthful outcome language

The virtual LCD distinguishes these local evidence levels:

1. `Queued locally` means only that the client queue accepted the item.
2. `Accepted by local bridge` means only that the injected transport accepted
   it.
3. An inbound item means the peer simulator observed the synthetic loopback
   item when its own session was serviced.
4. `Acknowledged by peer simulator` means the synthetic peer returned a
   correlated acknowledgement.

None of these states proves an encoded OpenTrail packet, MeshCore command,
LoRa transmission, relay, physical peer receipt, authenticated sender, field
delivery, or operator action.

The Alerts screen can inject an explicitly synthetic test alert. It does not
exercise the canonical held critical-confirmation workflow and says so in
visible and accessible copy. Message, alert, acknowledgement, and connection
flows are present; the broader home/quick-status/position/archive/recovery shell
and real protocol composition remain later work.

## Desktop and accessibility evidence

The WPF shell declares `PerMonitorV2`, exposes public Client A/Client B window
and control names, provides keyboard access keys and shortcuts, uses visible
focus styling, and communicates connection state in text rather than color
alone. Its accepted 560 x 640 minimum keeps the tested primary simulated-touch
target at least 44 x 44 device-independent pixels. Classic disabled control
copy measures at least 4.5:1 against its actual configured button background;
the theme resource owner also passes deterministic high-contrast and return-to-
classic checks.

Five initial presentation groups were expanded to ten integrated WPF groups
covering theme/contrast, two independent windows, minimum target size and
evidence labels, screen navigation, failed-draft retention, real core-presenter
traffic mapping, deterministic 100%/125%/150%/200% bitmap checks, and one-window
close isolation. The command-serialization group runs 100 overlapping
Connect/Reconnect races and requires explicit post-await UI-thread dispatch. A
dedicated lifecycle group proves that close stops the timer and drains an
in-flight periodic service tick before presenter disposal. Seventeen separate
core groups plus 100/100 repeats cover client isolation, one-to-one assignment,
two-way message/alert/ACK behavior, queue/inbox/history limits,
disconnect/reconnect isolation, stale and
rollback state, text rejection, exact public-label rejection, close-time stale-
inbox draining, discovery cancellation/coherence, duplicate-endpoint rejection,
throwing-subscriber containment, independent peer cleanup after first-close
failure, and deterministic publication. The 100% Client A home render received
visual review with no layout blocker.

This is deterministic in-process host evidence. Actual 125%/150%/200% monitor
transitions, every Windows high-contrast theme,
physical keyboard/mouse/touch, Narrator/Braille/other assistive technology,
clean-machine operation, installer lifecycle, code signing, and distribution
remain open.

## Live-device gates

Before either virtual LCD can truthfully connect to a physical companion, add
and accept all of the following without weakening the synthetic mode:

1. privacy-safe discovery and explicit operator selection with no hard-coded
   transient port;
2. exclusive one-client/one-device lease and private per-user assignment
   storage with a clear forget/reassign path;
3. a bounded, cancellable MeshCore companion adapter with exact firmware/role
   compatibility and no reset, flash, or configuration authority;
4. generation-bound reconnect handling so old reads cannot enter a new session;
5. two-device send/receive/alert/ACK evidence distinguishing host acceptance,
   device acceptance, radio transmission, peer receipt, and acknowledgement;
6. disconnect, cable-loss, stale-data, queue-pressure, process-close, and port-
   contention recovery; and
7. privacy-safe public evidence that omits identities, ports, channels,
   coordinates, keys, and message content.

Real LoRa behavior then requires the existing physical-radio evidence process.
Physical LCD/touch behavior, sunlight readability, brightness, power, heat,
boot time, embedded rendering performance, and recovery must be repeated on the
selected standalone hardware.

## Validation commands

```powershell
tools\Start-WindowsSimulator.ps1
tools\Test-WindowsSimulator.ps1
dotnet run --project tests\windows-simulator\OpenTrail.Simulator.Core.Tests\OpenTrail.Simulator.Core.Tests.csproj -c Release
dotnet run --project tests\windows-simulator\OpenTrail.Simulator.Tests\OpenTrail.Simulator.Tests.csproj -c Release
```

`Start-WindowsSimulator.ps1` builds the source into the stable local system-temp
`OpenTrail.Simulator.Runtime` tree and launches the two-window synthetic app. It
is a source launcher, not a package, installer, shortcut, or release.

The complete local `tools/Test-Host.ps1` project gate builds and runs both suites
and exits 0 for the accepted tree. Remote CI confirmation remains a per-push
publication gate.
