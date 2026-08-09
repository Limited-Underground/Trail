# Decision 0001: Radio Transport Contract

Status: accepted for the OT-004 experimental foundation, 2026-08-08

## Decision

OpenTrail application and packet logic will depend on a small non-blocking
`RadioTransport` interface rather than a board, LoRa driver, MeshCore API, or
network topology.

The interface:

- accepts opaque binary frames and copies them into transport-owned storage;
- reports an adapter-specific MTU up to a 255-byte storage ceiling;
- exposes explicit radio state, error, queue, and frame-counter information;
- returns optional receive time, frequency, RSSI, and SNR metadata with validity
  flags instead of inventing values when hardware cannot measure them;
- leaves identity, addressing, authentication, encryption, acknowledgements,
  retries, duplicate suppression, TTL, and forwarding above the transport;
- is cooperatively serviced so radio work cannot block the UI indefinitely; and
- preserves an unread frame after a caller supplies an undersized buffer.

Runtime-facing structures use fixed-capacity storage and do not allocate memory
while sending or receiving. The included fake transport connects two in-memory
nodes and deterministically models latency, queue saturation, missing peers,
offline/fault states, injected send errors, and packet loss.

## Why

The existing two-node evidence came through MeshCore USB Companion firmware,
but OpenTrail must not make MeshCore, USB, or the Heltec V4 its permanent
architecture. An opaque contract lets the project use the current companions as
an experimental adapter while later replacing them with a direct SX1262 board
binding without changing packet or delivery logic.

## Consequences

- A successful `send` means the frame was queued locally; it does not prove
  over-the-air delivery or acknowledgement.
- Concrete adapters must document their actual MTU and how they map unavailable
  metrics.
- OT-006 owns the experimental packet byte budget and encoding.
- OT-008 and OT-009 own acknowledgement, duplicate, retry, expiry, and forwarding
  policy.
- A real Heltec/SX1262 adapter remains pending until exact board bindings and a
  recoverable build/flash path are established.
