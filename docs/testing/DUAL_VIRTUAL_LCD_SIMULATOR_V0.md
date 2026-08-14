# Dual Virtual-LCD Simulator v0

Status: **OT-030B host-tested shared-shell and bounded companion bridge
accepted; OT-030C bounded host/shared-model increment done after focused and
complete host acceptance, with remote publication pending; no real-LoRa,
physical-display, target-firmware, installer, clean-machine, or supported-
hardware claim**, 2026-08-14.

## Purpose

The simulator opens separate Client A and Client B virtual-LCD windows on one
Windows laptop so the portable client interaction model can be exercised before
complete touchscreen hardware exists. It requires no cloud, server, phone, or
account and is not a field dependency.

The shared C++ screen/action model is also the renderer-neutral reference for
the planned Android companion and self-contained touchscreen tracks described
in [Decision 0007](../decisions/0007-shared-client-presentation-tracks.md).
Evidence from the Windows renderer does not transfer to either physical track.

## Authority and composition

Each LCD window renders a fixed 466 x 466 circular logical surface. Windows
device selection, connection state, local evidence, and recovery controls live
outside that circle as host chrome and are not portable application screens.

```text
Client A WPF renderer ---- native protocol v2 ---- shared C++ PortableUiShell
        |                                              |
        +---- presenter ---- simulator Core bridge ----+
                                      |
                         local loopback companion A/B
                         or explicitly selected USB test companion

Client B owns an independent renderer, native shell session, presenter,
connection generation, queue, and message snapshot over the same shared bridge.
```

The shared C++ `PortableUiShell`, compact `UiFrame`, exact-offer
`UiPresentationSidecar`, and `PortableUiRenderPlan` own portable screen state,
semantic action slots, request correlation, fixed text templates, and renderer-
neutral geometry. The sidecar carries bounded copied message/text presentation
for only the pending offer, preserving the existing 24-byte `UiFrame` embedded
ABI and legacy result-object memory budgets. WPF draws only the offered render
primitives and maps input back to the offered action slot. It does not invent
application screens, labels, navigation, free chat, message ordering, read
state, or acknowledgement rules. Legacy/non-message frames require an empty
sidecar.

Every transition is two phase. The native shell offers one exact generation and
revision, WPF either reports that exact frame presented or rejects it, and only
a successful present commits the revision and may emit a typed request. A stale
input, wrong generation or revision, wrong request completion, duplicate pending
offer, failed render, or malformed native reply fails closed.

## Portable screens and actions

The shared shell currently covers:

- Home with Status, Messages, Quick status, and Critical alert;
- Status with position-sharing, archive-controls, and Back actions;
- two quick-status pages with four fixed choices;
- held critical-alert confirmation;
- archive controls plus held Start and immediate Stop confirmations;
- recovery notices and system-fault presentation; and
- OT-030C message center, Inbox, Outbox, detail, fixed-template Compose, and
  send confirmation.

The local-interface contract remains limited to four ordered action slots.
Message lists therefore show two rows per page plus page-cycle and Back actions.
Compose exposes two of eight C++-owned templates per page over four pages. It
does not accept arbitrary WPF or operator-authored text.

## Bounded message presentation

OT-030C fixes a renderer-owned presentation boundary rather than a transport or
storage protocol:

- at most 12 messages per snapshot and 96 printable-ASCII bytes per visible
  message;
- nonzero strictly increasing sequence numbers within a bridge session;
- fixed inbound/outbound direction, chat/quick-status/alert/acknowledgement
  kind, normal/important/critical priority, and local delivery evidence;
- at most four copied, pointer-free `UiOwnedText` values in a frame;
- explicit, mutually exclusive truncated and unavailable states with canonical
  fixed C++ presentation text; and
- newest-first two-row pages, epoch-scoped read markers, and read-on-successful-
  detail-presentation only.

Only an exact active inbound critical alert can expose the hold-to-acknowledge
action. A template send request carries only its numeric template ID. An alert
acknowledgement request carries only the exact message sequence. Completion is
bound to generation, revision, request ID, bridge-session epoch, template or
message identity, and applied message-sequence evidence. A newer unrelated
snapshot cannot satisfy an old request.

The C++ shell owns presentation semantics and selection/read state. The Core
bridge remains the authority for session-scoped transport snapshots, queue
admission, and local bridge evidence; this is not shared message persistence or
a production packet store.

## Transport and evidence boundary

The public UI keeps these levels distinct:

1. `Queued locally` means only that the bounded client queue accepted a request.
2. `Accepted by bridge` means the selected local bridge accepted it.
3. `Observed by bridge` means the peer-side local bridge observed it.
4. `Acknowledgement observed by bridge` means a correlated local bridge
   acknowledgement was observed.

None proves a production OpenTrail packet, MeshCore application message, radio
transmission, relay, authenticated peer, physical receipt, operator response,
or field delivery.

Local loopback supports the shared quick/critical flow and, under OT-030C, fixed
template chat plus acknowledgement of an inbound critical alert. The live USB
application admits only fixed quick-status and critical-alert requests. USB
acknowledgement requests, template/arbitrary chat, archive, and position
requests fail closed. The helper protocol can decode an inbound correlated
`OTS0:A` observation; it may advance only an already-outbound matching critical
alert, and an unmatched observation cannot advance alert state. This is an
unauthenticated simulator test protocol, not the production OpenTrail packet/
application transport.

## USB selection and process boundary

USB candidate discovery is passive VID/PID enumeration only. It never opens,
queries, resets, flashes, configures, or transmits to a candidate. A recognized
candidate is never silently assigned or opened. The operator must explicitly
select an available choice; open then rechecks the exact private binding and
allowlisted runtime model, role, build, and protocol before creating a session.

Private bridge-owner provenance and roster revisions prevent a foreign, stale,
or duplicate choice from being applied. One private endpoint can belong to only
one client, while a selection or Forget operation republishes coherent A and B
snapshots. Transient ports, serial numbers, MAC addresses, channel names, keys,
coordinates, private endpoint tokens, and raw replies do not enter public state
or evidence. The SenseCAP repeater USB family is excluded from companion
selection.

The Python USB helper and native C++ UI host are exact-path child processes with
bounded newline-delimited input and output, fixed public errors, cancellation
and timeouts, terminal handling after ambiguous post-write failures, stderr
draining, and deterministic close/restart behavior. A client close fails and
clears its unsent session-bound work; it cannot replay into a later connection.
Cleanup attempts both bridge and USB-host disposal even when the first fails.

No USB device or serial port was accessed for the accepted OT-030B host gate.

## Native protocol v2

OT-030C uses a line protocol only between the Windows renderer and the bundled
native shared-shell host. Commands are capped at 4096 bytes before allocation;
replies are capped at 8192 bytes. Version 2 requires exact field counts, fixed
numeric ranges, uppercase hexadecimal owned text, canonical exceptional-text
state, newline completion, and exact generation/revision/request correlation.
Version 1, NUL, partial EOF, oversized input, malformed hex, unknown fields,
stale input, and contradictory completion are rejected without applying state.

The native host keeps at most one uncommitted offer. `PRESENTED` can commit only
the exact offered generation and revision; `RENDER_FAILED` discards that offer
without advancing portable state. Quit and EOF clear pending input and request
state.

## Desktop and accessibility boundary

The WPF shell declares `PerMonitorV2`, provides keyboard access and visible
focus, exposes public Client A/Client B accessible names, and communicates state
in text rather than color alone. The renderer uses the canonical logical plan
at 100%, 125%, 150%, and 200% scale, keeps enabled action targets at least 64
logical pixels, and places all portable LCD primitives inside the circular
surface. Host chrome remains visually and semantically separate.

Accepted OT-030B evidence includes classic/high-contrast round trips, realized
disabled contrast of at least 4.5:1, independent window close, serialized
overlapping commands, and close-time draining of an in-flight service lease.
The OT-030C integrated WPF gate passes, and representative Home, Compose,
post-send Client A, and inbound-detail Client B renders were visually reviewed
with connection and loopback state consistent with their exercised fixtures.

Real monitor transitions, every Windows high-contrast theme, physical keyboard/
mouse/touch, Narrator/Braille/other assistive technology, clean-machine launch,
installer lifecycle, signing, and distribution remain open.

## Accepted and pending evidence

OT-030A is retained as historical evidence for the first isolated two-window
WPF shell: 17 core and 10 WPF groups passed. Its generic Home/Messages/Compose/
Alerts UI was not firmware-faithful and has been retired.

OT-030B accepted a warning-free focused gate of 32 Core groups, 23 Windows
companion/process groups, 15 Python helper groups, and 11 WPF groups. Strict C++
shell/render tests, native-host parity, deterministic scale renders, and a
manual render review also passed. That increment proves shared renderer-neutral
screen authority and bounded host integration only.

The accepted OT-030C focused gate passes strict C++ shell/render tests, 33
Core groups, 23 Windows companion/process groups, 15 private-helper groups, ten
native-protocol groups, and 11 integrated WPF groups with warning-free Release
builds. It includes exact-once typed request handling and two native LCDs
crossing one exact fixed-template message over local loopback. The four-image
representative visual set also passed review. The complete expanded 112-
executable host matrix and both publication-safety layers pass. Remote
publication remains pending. OT-030C is `done` for the bounded host/shared-model
increment; parent OT-030 remains `partial` throughout.

## Remaining gates

- Verify the exact published remote commit.
- Bind the shared shell and renderer plan in an exact ESP target composition.
- Implement and validate authenticated OpenTrail packet/application transport;
  the current USB test framing does not satisfy this gate.
- Add renderer-neutral pairing/group-membership screens and synthetic fixtures
  only after their public state/actions are specified. Real peer identity,
  authorization, keys, membership persistence, and join/leave execution must
  remain device/protocol authority and are not present now.
- Exercise two explicitly selected physical companions without publishing
  device-specific identifiers, then keep host/device/radio/peer evidence
  separate.
- Repeat every display, input, readability, brightness, power, thermal, boot,
  latency, and recovery test on the selected touchscreen hardware.
- Validate clean-machine packaging, installer lifecycle, signing, and support
  scope separately.

## Validation commands

```powershell
tools\Start-WindowsSimulator.ps1
tools\Test-WindowsSimulator.ps1
tools\Test-Host.ps1
```

`Start-WindowsSimulator.ps1` builds the source into the stable system-temporary
`OpenTrail.Simulator.Runtime` tree and launches the local two-window app. It is
a source launcher, not a package, installer, shortcut, or release. It performs
no implicit USB open; a physical candidate requires explicit selection.
