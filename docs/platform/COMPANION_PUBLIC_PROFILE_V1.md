# Protected public-profile settings v1

Owner: OpenTrail. OT-0302 adds protected local settings; public discovery,
radio transmission and completed onboarding are separate operations.

The negotiated configuration minor is **5**, capabilities `0xff`, payload
capacity 128 bytes, record capacity 148 bytes and minimum ATT MTU 151.
Request kind is `8`; response kind is `0x8a`. Profiles 2, 3 and evaluation 127
retain their previous wire rules and reject this operation.

An atomic `OTPC` payload contains both chosen public name and visibility:

| Offset | Bytes | Value |
|---|---:|---|
| 0 | 4 | ASCII `OTPC` |
| 4 | 1 | Version 1 |
| 5 | 1 | Kind |
| 6 | 1 | Reason |
| 7 | 1 | UTF-8 name byte count |
| 8 | 8 | Unsigned revision, little endian |
| 16 | 1 | Visibility: exactly 0 or 1 |
| 17 | 3 | Reserved zero |
| 20 | 0–96 | Exact UTF-8 name bytes |

Names are well-formed UTF-8, at most 40 UTF-16 code units and 96 encoded bytes,
without leading/trailing ASCII space or C0/C1 controls. The Android product
surface additionally applies its maintained public display-alias policy.
Name and visibility are distinct from the device's private saved name.

| Kind | Revision/name/visibility |
|---|---|
| 1 READ | Zero, empty, false |
| 2 WRITE | Expected revision (zero allowed, maximum forbidden), nonempty name, chosen visibility |
| `0x81` SNAPSHOT | Absent: zero, empty, true; present: nonzero, nonempty, stored visibility |
| `0x82` APPLIED | Nonzero next revision, exact committed name and visibility |
| `0x83` REJECTED | Zero, empty, false; nonzero reason |
| `0x84` UNCERTAIN | Zero, empty, false; storage-failure reason 4 |

Reasons are 0 none, 1 unauthorized, 2 stale revision, 3 invalid name,
4 storage failure, 5 unsupported. A write cannot clear the public name.
Factory reset owns deletion of the separate stored profile.

Operations require the current protected Ready session and fresh verified
device-name and valid saved-region readbacks. The nonce, negotiated profile,
exchange and configuration owner bind each response. APPLIED must match the
requested values and increment the expected revision exactly once. A lost,
invalid or uncertain result clears verified public values until a fresh READ;
the caller never retries an uncertain write automatically. Session replacement
and disconnect invalidate the old owner and its responses.

Visibility defaults ON only in the absent settings snapshot. Saving either
visibility value never emits discovery traffic, enables TX or completes setup.
A future presence publisher must revalidate the product display-alias rules
before using a stored name; this storage profile grants no publishing authority.
Cross-language synthetic vectors: `tests/fixtures/companion_public_profile_v1_vectors.csv`.
