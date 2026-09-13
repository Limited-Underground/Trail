# OT-203 receipt boundary correction — 2026-09-12

## Finding and correction

The existing USB FIFO adapter's initial drain delivers retained output before the
new receipt. ROM/bootloader output precedes application console quarantine. With
64 retained bytes and the 65-byte CRLF pass receipt, the actual writer/adapter and
old host endpoint reproduce OT-202's 129-byte `read_invalid` signature.
This establishes a concrete failure mechanism. The physical trial did not retain
those bytes, so their actual content and origin remain unknown.

The successor sends `SEC_BEGIN1 <fresh challenge>` and the unchanged receipt in
one call. The host discards at most 256 preamble bytes and requires that exact
marker before passing bytes into the existing strict parser. Wrong challenge,
version or malformed marker refuses the attempt. After BEGIN, the original
128-byte receipt guard, exact grammar and trailing-output rejection still apply.
There is no extra command or extended wait. The longest combined output is
121 logical / 123 wire bytes within the existing 20 ms send budget.

Wire counters and discarded-byte counts have a separate bounded projection;
the original receipt diagnostics now count the stream after BEGIN. Neither
projection stores raw bytes or challenges. BEGIN identifies a boundary and is
not authentication, a policy pass or a readiness guarantee.

## Validation and executable package

- Both fresh ESP-IDF v6.0.2 / Xtensa esp-15.2.0_20251204 builds match all seven
  artifact pairs. All 44 compiled/build source pins and 731 library files verify.
- The actual C++ formatter/writer/FIFO passes 24 groups and exports 21 wire
  fixtures. Twelve Python tests parse these through the real host endpoint,
  exercise every split of the contaminated pass stream, all result types,
  preamble limits, malformed markers, oversize receipts and trailing output.
- The complete affected package matrix passes 284 tests across 16 suites,
  including composed workflows with the exact image and real isolated dispatch.
  Startup contamination is injected through the production backend. Original
  restoration and fail-closed cleanup remain exercised.
- The final 3,561-file runtime (96,345,673 bytes) passes
  ordinary and hostile parent/child probes and complete inventory verification.
  Exact-candidate package admission also passes with synthetic originals only.

Target: `heltec_v4_security_receipt_sync`, version `ot203-receipt-sync-v1`.
Candidate: 440,480 bytes; SHA-256 `d47b09dae0aa1ae878769410a5daaabdac07af2d4c95c9f9d3436a1e03321cee`.
Runtime manifest SHA-256: `55430360c11ff7113fb55008d6fdc0f67e5ef313c1164455346b98f0dee12e8a`.
Package: `OT203-RECEIPT-BUNDLE-1`, with 23 executable source files.
The NVS record format remains `OT200-SYNC-INPUT-RECORD-1`; its new decoder binds
the successor image. Previous source families, evidence and grants are preserved.

See the [build audit](../../tests/benchmarks/crypto/OT-203-RECEIPT-SYNC-BUILD-2026-09-12.json) and
[software evidence](../../tests/benchmarks/crypto/OT-203-RECEIPT-SOFTWARE-2026-09-12.json).
Reproduce with `tools/Test-SecurityReceiptBoundary.py` and
`tools/Test-SecurityReceiptOperator.py --candidate <exact candidate path>`.
Private commands, logs, inventories and proposal are in `.private/ot203-receipt`;
the complete package matrix is in `.private/ot203-operator/matrix`.

## Concrete next trial

Passive USB enumeration matched the two existing roles without opening ports.
The prepared proposal binds this exact image/runtime, preserved restoration files
and the existing Heltec WiFi LoRa32 V4.2 / ESP32-S3 / 16 MB roles. It issues no
authority. Fresh ROM identity, geometry, power/cable and storage custody remain
physical preflight gates. Prior grants are consumed and snapshots are stale.

One newly authorized trial starts with fresh application/full-NVS/protected
readbacks, held for at most 30 minutes. A receives the candidate first, followed
by one BEGIN/strict-receipt attempt. Confirm serial closure, allow at most one
claimed NVS observation, independently restore originals, verify protected regions
and restart the original. B proceeds only after A's required evaluation and
restoration gates pass; otherwise guarded readback/reset releases unflashed B.
An interruption permits restore-only recovery or guarded release, with no retry.

Only the application span (589,824 bytes at `0x10000`) and full NVS (12,288 bytes
at `0xd000`) are writable under that future scope. Candidate initialization and
stage/input writes are included. Bootloader, partition and OTA stay protected;
radio, phones, bonds and factory reset are excluded. The proposal and passive
request are saved privately as `trial-proposal.json` and
`backup-request-PROPOSED.json`; neither is an executable grant.

Implementation is complete locally. Physical confirmation and any publication
remain pending their distinct authorization. No physical trial or security pass
occurred in OT-203; no V1 credit, crypto selection or public website status changed.
