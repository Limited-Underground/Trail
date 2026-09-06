# OT-170 candidate device-name payload v1

2026-09-05. Computer-only codec increment; no deployed/advertised capability,
frame allocation, transport handler, persistence driver or device receipt.
This does not change the current OTB0 v0.0/v0.1 records or OTC0 envelopes.
Activation requires an explicit negotiated protocol extension plus target
commit/readback/reset integration. Unknown versions must continue to fail closed.

## Payload

All payloads have a16-byte header followed by0..96 UTF-8 name bytes.

| Offset | Bytes | Meaning |
|---|---|---|
|0|4|ASCII OTNC (engineering format identity)|
|4|1|Version1|
|5|1|Kind: READ1, WRITE2, SNAPSHOT0x81, APPLIED0x82, REJECTED0x83, UNCERTAIN0x84|
|6|1|Reason: NONE0, UNAUTHORIZED1, STALE_REVISION2, INVALID_NAME3, STORAGE_FAILURE4, UNSUPPORTED5|
|7|1|UTF-8 name byte count|
|8|8|Unsigned little-endian durable revision or expected revision|
|16|0..96|Exact name bytes, no terminator|

Kinds here are payload discriminators, not allocated OTC0 frame-kind values.
READ has no reason, name or revision. WRITE requires a nonempty valid name,
NONE reason and expected revision below UINT64_MAX. Initial write expects0.
SNAPSHOT with revision0 means no saved name; otherwise revision is positive
and name nonempty. APPLIED requires positive revision, exact nonempty committed
name and NONE reason. REJECTED requires a nonzero known reason with zero revision
and empty name. UNCERTAIN requires STORAGE_FAILURE with zero revision/empty name.
All lengths must match exactly; trailing bytes are invalid.

Names use strict shortest-form Unicode UTF-8, at most32 UTF-16 code units and
96bytes. Reject malformed encoding/surrogates, C0/C1 controls and leading/trailing
ASCII space. The existing app name validator may impose stronger whitespace
restrictions. Exact Unicode storage does not imply OLED glyph support; rendering
fallback and truncation remain a separate decision before target integration.

Shared WRITE fixture, expected revision0x0102030405060708, name Trail:

    4f544e43010200050807060504030201547261696c

## Future execution boundary

Only current authorized encrypted owner sessions may issue this request.
Expected revision is compare-and-swap, not an unconditional last-writer-wins
update. APPLIED must follow durable commit and exact readback. Ambiguous writes
must reconcile through a fresh read, never blindly retry as though unmodified.
Duplicate exchange handling belongs to the request coordinator, not this codec.
Persistence must execute outside the NimBLE callback and survive restart.
Any new settings namespace must join factory-reset inspection/erase/readback;
the existing owner blob must not be repurposed for names.

## Preflight scope

Mandatory firmware-porting lessons read. This increment changes only independent
target-neutral codec sources and deterministic tests. No target CMake/linkage,
advertised capability, pin/partition/configuration, boot, callback or storage
mutation changes. Exact target builds, two-build artifact identity and physical
installation gates are deferred because no target composition consumes this
codec. They become mandatory before target integration/hardware execution.
No hardware was opened, flashed, reset or radio-tested for this increment.

## 2026-09-06 isolated parity integration

Existing codecs imported unchanged with 151 shared vectors; payload remains a candidate with no negotiated frame, runtime handler or storage driver. See [evidence](../testing/OT-170-NAME-PAYLOAD-PARITY-2026-09-06.md). The preliminary receipt checks label/session/name only; request/revision correlation is required before live issuance.
