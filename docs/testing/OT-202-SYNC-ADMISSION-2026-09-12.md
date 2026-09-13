# OT-202 fresh synchronization trial admission — 2026-09-12

## Prepared checkpoint, superseded by approved execution

The owner subsequently approved this exact scope. The trial and restoration are
closed in [OT-202 physical outcome](OT-202-SYNC-TRIAL-2026-09-12.md). The preparation
record below retains its earlier checkpoint; its pending labels are historical.

The exact [OT-201 package](OT-201-SYNC-OPERATOR-2026-09-12.md) is unchanged.
Passive USB enumeration matched both expected roles without opening serial ports.
The 22 executable source pins, 43 firmware source pins, 3,560 runtime files,
candidate and retained restoration images reverified. This is passive preparation;
ROM identity/geometry and fresh application/NVS/protected custody remain untested.
No grant was issued and no hardware mutation occurred.

## Proposed bounded operation

Use the existing two Heltec WiFi LoRa32 V4.2/ESP32-S3/16MB roles, subject to fresh
ROM verification. Candidate `heltec_v4_security_sync_diag` / `ot200-sync-diag-v1`
is 440,432 bytes with SHA-256
`25e18eb5f852ea53a005272af13cbbe6fa828833920ecd5c8c79e826984d5a62`.
Runtime manifest SHA-256 is
`cb9171ed24be9023e904a8710847e14e73a791daf637e3c3be3a289ffbc7bc97`.

1. Re-enumerate immediately before device access. Confirm target, power/cable,
   ROM identity, geometry and exact role. Read fresh application/full NVS and
   protected regions; reject unexpected originals or existing diagnostics.
2. Hold custody for at most 30 minutes and bind it to the exact runtime, source,
   image, record contract and new one-use authority. Old grants are consumed and
   historical snapshots cannot provide executable custody.
3. Attempt A first: write/read back the diagnostic application at `0x10000`,
   reset into the candidate and perform one strict command/receipt attempt.
   Candidate NVS initialization and stage/input writes are part of this scope.
4. Confirm serial closure, consume at most one durable NVS observation claim,
   restore the original application/full NVS, verify protected regions, then
   restart the original. B proceeds only after A's required gates pass;
   otherwise guarded readback/reset releases untouched B.
5. On interruption, use independent restore-only recovery or guarded release.
   No retry, diagnostic recapture, radio transmission, phone/bond change, factory
   reset or bootloader/partition/OTA write is proposed.

The application restoration span is 589,824 bytes at `0x10000`; full NVS is
12,288 bytes at `0xd000`. Bootloader, partition and OTA remain readback-protected.
Fresh per-role storage determines the exact restoration files before mutation.

## Preflight disposition

| Gate | Disposition |
| --- | --- |
| Exact image/source/runtime and composed workflow | VERIFIED from unchanged OT-200/201 evidence and current hashes |
| Repeat builds | Reused unchanged accepted two-build artifact tuples; no target changes |
| Endpoint/reset lifecycle and restoration | Software-validated; fresh physical confirmation pending |
| Explicit readiness handshake | Deferred for this bounded diagnostic trial; no product transport acceptance or new wait is claimed |
| Persistence | One-use observation and restoration validated in software; physical result unknown |
| BLE callbacks, UI and radio acceptance | Not applicable to this nonradio diagnostic-only scope; no phone or radio changes |
| Current power/cable, ROM geometry and fresh custody | UNKNOWN until approved physical preflight |
| Exact scope authorization | PENDING; no executable authority created |

The hardware tracker still contains the initial OT-199 approval-blocked checkpoint.
The retained resumed trial evidence establishes A restoration and untouched B
release; neither historical record is current physical custody. The earlier
invalid-length cause remains unknown. No security pass, V1 credit or public
website status changed. Work is local and uncommitted; publication is pending.

Exact proposal, passive request, command and hash evidence are retained privately
under `.private/ot202-admission` in the active worktree. That proposal is not a grant.
