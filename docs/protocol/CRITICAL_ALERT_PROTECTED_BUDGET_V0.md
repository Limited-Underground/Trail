# Critical Alert Protected-Radio Budget v0

Status: deterministic feasibility result, fragmentation not approved, 2026-08-10

## Result

The mirrored OpenGauge/OpenTrail `OGA0` critical-alert frame and `OGK0`
acknowledgement frame are each exactly 64 bytes. Under the corrected signed-
group candidate at the 163-byte example transport MTU:

- authenticated header: 44 bytes per fragment;
- AEAD tag: 16 bytes per fragment;
- candidate Ed25519 source signature: 64 bytes per fragment;
- protected overhead: 124 bytes per fragment;
- maximum plaintext: 39 bytes per fragment; and
- a 64-byte alert or ACK therefore requires two fragments.

The first fragment is 163 bytes and the second is 149 bytes, for 312 transmitted
bytes before retries or repeater copies. At the existing 62.5 kHz/SF7/CR5
comparison PHY, theoretical source airtime is 1,025,024 us. One exact-byte
repeater copy would double LoRa transmission airtime to 2,050,048 us before
contention, scheduling, acknowledgement traffic, or retries.

Alert and ACK have the same size, so a complete signed alert plus signed ACK is
four source fragments and 2,050,048 us theoretical source airtime. With one
repeater forwarding every frame once, aggregate LoRa transmission airtime is
4,100,096 us before retry behavior. These are accounting results, not measured
latency, reliability, capacity, or regulatory approval.

## Why fragmentation remains blocked

The budget deliberately charges a complete header, tag, unique counter, and
candidate source signature to every fragment. A production design must still
decide and prove:

- whether source authentication covers each fragment or one canonical complete
  message, and how partial messages are prevented from creating sender claims;
- exact fragment index/count and message-ID semantics;
- per-fragment nonce/counter assignment and replay-window interaction;
- fixed reassembly capacity, timeout, eviction, duplicate, overlap, reorder,
  mixed-sender/epoch, and conflicting-fragment behavior;
- authentication-before-allocation limits against memory exhaustion;
- whether an ACK is allowed only after complete authenticated reassembly and
  semantic acceptance;
- retry behavior for whole messages versus individual fragments; and
- measured client/repeater airtime, power, congestion, and regional constraints.

Until those gates close, OpenTrail must not split `OGA0` or `OGK0` for a
production radio path. Existing physical tests transported each 64-byte frame
inside MeshCore text and remain host-mediated composition evidence, not this
candidate protected framing.

## Host evidence

The protected-budget suite now contains ten scenario groups. The dedicated
critical-alert case fixes the two-fragment, 312-byte, 1,025,024-us result and
will fail if later overhead changes silently.
