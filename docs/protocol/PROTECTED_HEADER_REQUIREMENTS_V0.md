# Protected Header Requirements Reconciliation v0

Status: reviewed sizing correction, not a packet-v1 wire format, 2026-08-10

## Finding

The original 36-byte candidate header budget reserved four bytes for generic
routing and fragment metadata. The accepted immutable single-repeater policy
requires the protected object to carry an authenticated 64-bit destination
alias—zero for group broadcast—while retaining fragment index/count. Four bytes
cannot represent that requirement.

Freezing a 36-byte encoder would therefore either omit the destination from
AEAD additional authenticated data, truncate the alias, or hide bytes outside
the published budget. All three outcomes are rejected.

## Corrected 44-byte accounting

| Field group | Candidate bytes |
| --- | ---: |
| Envelope magic/version/type/flags/header length | 6 |
| Immutable forwarding permission/reserved plus fragment index/count | 4 |
| Group epoch | 4 |
| Sender alias | 8 |
| Destination alias (`0` means group broadcast) | 8 |
| Message identifier | 4 |
| Rollback-safe per-frame counter | 8 |
| Ciphertext length | 2 |
| **Authenticated header subtotal** | **44** |

There is no TTL or mutable hop count. An authorized first-release repeater
validates and rebroadcasts the exact protected bytes once.

This is still an accounting profile, not final offsets. Packet type values,
flags, reserved-bit policy, fragment/reassembly rules, message-ID relationship
to fragments, destination privacy, and signature coverage remain review gates.

## Budget effect at the existing example PHY

With a 16-byte AEAD tag, base overhead becomes 60 bytes. At the 163-byte example
MTU, base plaintext capacity is 103 bytes. Adding the 64-byte signed-group
candidate raises overhead to 124 bytes and leaves 39 plaintext bytes. A 16-byte
position then occupies 140 bytes with 461,312 us theoretical airtime at
62.5 kHz/SF7/CR5.

The correction costs eight bytes per frame. These are sizing results, not
measured radio performance or approval of fragmentation, signatures, AEAD, or
packet v1.

## Host evidence

The ten protected-budget groups now use the corrected 44-byte
profile. They cover 163/255-byte MTUs, base/signed/future-wrapper costs, empty
and fragmented plaintext, exact one/two-frame boundary, capacity failure, and
invalid PHY behavior.
