# OT-236 libsodium capture preparation

The subsequent authorized physical preflight stopped before a candidate write.
The [result and required correction](OT-236-PREFLIGHT-RESULT-2026-09-15.md)
supersede the proposed sequence below where it assumes historical NVS hashes
remain current. Host preparation evidence remains valid for its tested boundary;
the existing live caller must not be reused for another trial.
The [revision-2 host correction](OT-236-NVS-INTERVAL-CORRECTION-2026-09-15.md)
now supplies fresh ROM-held intervals and recovery semantics. Its replacement
[physical caller](OT-236-INTERVAL-CALLER-2026-09-15.md) is integrated and reviewed;
the trial still requires current authorization and fresh physical preflight.

## Scope and reason

Host-only preparation of one sequential two-Heltec, eight-operation benchmark
capture. The [custody assessment](../../tests/benchmarks/crypto/OT-163-CAPTURE-CUSTODY-ASSESSMENT-2026-09-10.json)
could not establish retention of the historical libsodium captures. This
successor retains the actual canonical frame bytes and independently recomputes
their results. It does not reconstruct historical traces from summary statistics.

The candidate is the [admitted OT-163 reproducible build](../../tests/benchmarks/crypto/OT-163-LIBSODIUM-MATCHED-RESOURCE-2026-09-10.json),
not the different OT-122 physical image. Both retained build copies and their
ELF/map/configuration are reverified against accepted evidence; all ten recorded
source/configuration inputs still match. No firmware source changes or rebuild
are required by this host-tool increment. This reuse adds no physical acceptance.

## Exact proposed image boundary

| Role | Image | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| A and B candidate | `ot121_libsodium_primitive_bench.bin`, `ot163-libsodium-v1` | 293120 | `cf4377f56ad5d2dc4b2e53ae961f98f6e091fc170bf76c8d9f438a7fd6373d7e` |
| A original | `opentrail_heltec_v4_bench.bin`, `ot178-phone-v1` | 586736 | `43ac6dbc506c03faa63aae7a8e27195750598f06f3118bfff1ef7af84bc8d9f2` |
| B original | `opentrail_heltec_v4_bench.bin`, `ot171-label-v1` | 587968 | `984e241dc72fad956d34b5e83f04fd307dcb95e0f18c629ac46d21da9d96d87b` |

Only the application at `0x10000` may be written. The original application span
is 589824 bytes, including verified erased padding. Bootloader `0x0/32768`,
partition table `0x8000/4096`, OTA metadata `0x9000/8192` and **full NVS
`0xD000/12288`** must match their independently checked preflight hashes before
writes and during readback. NVS and protected regions have no write API.
The proposal names roles, not volatile ports. Fresh exact device identity,
installed-original readback and protected-region preflight remain mandatory.

Both intended boards are the existing Heltec V4 / ESP32-S3 / 16MB bench pair.
No radio or phone operation is part of this trial; radio region configuration
does not authorize a transmission. No bond clearing, factory reset, storage
erase, bootloader replacement or automatic benchmark retry is included.

## Maintained implementation

- [Execution composition](../../tools/libsodium_capture_execution.py) verifies
  the unchanged mbedTLS comparison template before compiling a private module.
  Exact substitutions change only the namespace, candidate, frame protocol and
  capture payload. Existing per-role restoration, one-use journal and recovery
  ordering remain. Package validation rechecks every bound source and image.
- [Autorun capture](../../tools/libsodium_capture_protocol.py) resets once,
  observes bounded return/stable presence, opens a fresh endpoint and receives
  without START/READY writes. The unchanged app waits 3000 ms before its header.
  Reset-to-open timing is host-tested, but remains physically untested for this
  composition; a missed header aborts rather than initiating another reset.
- Partial reads survive timeouts. Startup ANSI/text is discarded within 4096
  bytes. After the first frame, parsing is strict. At most eight canonical
  frames may share a line; 1621 records must validate eight operations, 800 cold
  and 800 warm samples, summaries, gates and resources. Capture is bounded by
  180 seconds and 2 MiB. Backend calls have finite timeouts; the protocol cannot
  preempt an indefinitely blocked foreign backend.
- Canonical extracted frames are saved using exclusive creation, flush/fsync,
  exact readback and reparse before marking custody successful. Boot chatter is
  excluded. Receipt hashes describe the retained frames separately from the
  canonical parsed-result hash. No raw capture or private identifier enters a
  public receipt. The independent audit requires the expected approval digest
  and rejects missing or changed frames, results, roles, binding or metadata.
- [Hardware composition](../../tools/libsodium_capture_hardware.py) reuses the
  verified backend with fresh identity checks and exclusive serial leases. It
  adds the exact full-NVS read range and a distinct authority namespace. It
  neither issues authority nor accesses hardware on construction.

Terminal parsing rejects trailing data already received in the same read; it
does not claim that no later serial byte could arrive. The independent frame
audit verifies captured measurements and the receipt's restoration report; it
does not replace physical restoration/readback or the user's screen check.

## Proposed physical sequence and stop conditions

1. Obtain separate approval for one application-only A-then-B attempt, its exact
   preflight/readback/reset operations and restoration. Bind the reviewed
   package, caller, current private role registry and preflight to a fresh,
   non-reusable authority. Prior OT-234 grants remain consumed.
2. Re-enumerate and independently identify each role; verify installed originals,
   padded application spans, all protected spans and recovery artifacts. Reject
   any discrepancy before a candidate write. Never identify by list order.
3. Write and independently read back A's candidate. Reset, reopen, capture and
   retain its complete validated stream. Close the serial lease.
4. Restore A's exact original, verify the full application/protected spans and
   reset to Trail before considering a write to B.
5. Repeat the same bounded operation on B only after A's successful restoration.
6. On capture, persistence or write failure, stop benchmark work and restore
   every touched role. An uncertain/unclosed serial lease blocks unsafe ROM
   operations. Restoration-only recovery follows the journal; it needs neither
   the candidate image nor another benchmark attempt.
7. Independently reparse both retained captures and compare receipt hashes,
   then verify restoration evidence and obtain confirmation of both normal
   Trail screens. Missing evidence is a failed/unreached gate, not a pass.

## Validation and limits

The [host proof](../../tests/benchmarks/crypto/OT-236-LIBSODIUM-CAPTURE-HOST-2026-09-15.json)
records exact source pins, commands, results and retained artifact verification.
The affected suites are wired into `tools/Test-Host.ps1`; injected hardware
tests are explicitly simulated. Existing parser, comparison execution and
backend regressions are retained.

Firmware porting preflight: target/configuration/artifacts are reused with exact
source and artifact checks; no target edit triggers new builds. USB/autorun
timing, parser bounds and cleanup are covered by host tests. Fresh device
identity, physical boot timing, captures, restoration and screen confirmation
remain unexecuted. RF configuration/range tests and phone UI tests are outside
this no-RF/no-phone boundary. No stage is substituted for physical acceptance.

This preparation neither completes Phase 2/3 nor selects crypto/wire protocols.
Monocypher custody, physical entropy/interrupted persistence, product trust
provisioning, retained membership/rekey/reset and full phone-to-phone acceptance
remain open. No V1 milestone or public website status changes.
