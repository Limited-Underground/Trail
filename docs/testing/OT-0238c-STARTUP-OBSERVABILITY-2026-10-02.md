# OT-0238c bounded startup diagnostics

Date: 2026-10-02. Software preparation; no new physical execution.

## Result and boundary

The previous [v7 physical attempt](../../tests/hardware/OT-0238c-STARTUP-V7-A-2026-10-02.md)
received 80 bytes without preserving their contents. Its physical cause remains
unknown. Both originals and custody were already restored and closed.

The candidate now records fixed startup stages, internal initialization steps,
first input/line-dispatch/reply-attempt markers and the chip's reset family.
The host retains bounded classifications and event order, including a final
chunk received too late for acceptance. No raw diagnostic text, command payload,
identity, code, key or secret is saved in these observations.

A separate source defect is corrected: after failed USB driver installation,
the stopped path no longer calls the absent driver's raw read function. Actual
entrypoint fault injection verifies that correction. It is not established as
the cause of the physical failure.

The combined host matrix, actual producer/collector checks, additive v8
packaging and two fresh matching firmware builds pass. Final test bindings
are checked separately; software evidence does not admit live originals or
authorize device operations.

## Firmware and host composition

- `OT_CANDIDATE_STARTUP_DIAGNOSTICS` defaults OFF. The prepared diagnostic image
  must explicitly enable it. Only the isolated enrollment evaluation target is
  affected; ordinary deployed firmware is not replaced by this work.
- `OTBOOT1` contains only bounded numeric stage, phase and SoC reset code.
  Call entry and return are distinct. Stage 13 records return from a void SDK
  function, not proof that its underlying restore succeeded.
- Internal store steps cover controller state, durable occupancy, NimBLE init,
  restore, qualification, entropy and cleanup. Runtime steps cover marker,
  storage/entropy admission, reset restoration, context and initial input/clock.
  Stage 19 still groups storage-ready/budget/entropy predicates; stage 21 groups
  random fill and nonzero context. A grouped failure does not identify its exact
  predicate without further source/evidence analysis.
- Bounded nonblocking diagnostics add no RTC/NVS writes, background task, retry
  or permission. The ROM reset query avoids newly linking the SDK's RTC-hint
  constructor. Reset-family codes cannot uniquely identify a caller or panic.
- Before input, bounded repeated markers support a late-opened observer.
  First-byte, newline and first-send markers precede the first response. A
  failed partial write gets a bounded failure marker with newline resync; all
  diagnostic output ends after the first full control reply.
- Only `startup_A`, role A, generation 1 enables the read-only observer. Passive
  open and observation share the existing 60-second startup ceiling. Unknown
  non-control boot lines can be observed within the byte/line limit until a
  canonical loop/stopped marker. They establish neither coverage nor readiness.
  Queued old control replies remain rejected, including wrapped or partial forms.
- HELLO and BOOTSTATUS still each receive at most five seconds within that same
  total ceiling. Their reply parser and fresh identity/authority checks remain
  strict. The exact diagnostic-only initial HELLO gate handles a racing beacon;
  there is no blind flush, port reopen, command replay or deadline extension.
- Raw capture is transient and capped at 8,192 bytes per observation/query.
  Saved summaries separate pre-command data from responses, retain up to 96
  fixed stage records per channel and explicitly flag loss/truncation. Diagnostic
  ANSI/CRLF normalization never changes control-response acceptance.
- `OT-CANDIDATE-QUERY-2` and `OT-CANDIDATE-QUERY-SUMMARY-2` distinguish startup
  coverage from the fresh queries. Snapshot/finalization failures are recorded
  separately, preserve an existing primary fault and release the close barrier.

## Interpreting the next result

| Retained evidence | Supported conclusion |
| --- | --- |
| No canonical startup marker | Startup/capture coverage remains unknown; absence is not proof of no boot or no panic |
| Entered step without a retained return | Last observed boundary; a blocked call, reset or dropped diagnostic remain distinguishable only with additional positive evidence |
| Fixed failed step | The firmware observed that specific failed check/group |
| USB passed, then USB stopped | Installation had passed before a later USB failure |
| USB stopped without a retained pass | Install versus later I/O is unresolved; the pass marker could have been dropped |
| First byte without a retained newline-dispatch marker | Input was observed; command assembly and capture completeness still need evaluation |
| Reply attempt without admitted response | Sending was attempted; inspect framing, delivery and late-arrival classifications |
| Strict reply captured after expiry | Received data stays rejected; diagnostics cannot turn it into success |
| Fresh READY and BOOTSTATUS zero | Startup-only control check passed; enrollment/lifecycle acceptance remains open |

Exact mask-ROM banner formatting remains unproved. Actual SDK formatter fixtures
cover supported panic/reset categories, padding, ANSI/CRLF, fragments and
truncation. SDK output can be dropped; missing fault text cannot prove health.

## Validation and provenance

Private evidence root: `.private/ot0238c-startup-observability-20261002/`.

- Actual `app_main`: diagnostics OFF **20**, ON **54** groups passed. Existing
  default-null target regression: **108** groups passed. **34** actual producer
  wire fixtures cover stopped/blocked steps, partial input and failed writes.
- [Native receipt](../../.private/ot0238c-startup-observability-20261002/firmware-focused/result.json),
  SHA-256 `6faad1616c90b73b180b84e1b808f91544739997ca108fbe6a1b0b2e41837e4e`.
- [Producer manifest](../../.private/ot0238c-startup-observability-20261002/firmware-focused/producer-fixtures/manifest.json),
  SHA-256 `08aebded84f7d1a4955a4b07df2ade8114bacef6dc0c870e26fb6822795f5272`.
- Combined controller/restoration matrix: **10 suites, 330 tests passed** on
  final inputs in 201.454 seconds; all 24 source/test pins remained stable.
  Command: `C:\Python314\python.exe -X utf8 -B
  .private/ot0238c-startup-observability-20261002/validate_host.py matrix`.
- Actual firmware producer bytes passed **34** full classifications, **99**
  fragmented observations and **88** late-reply cases. Late responses remained
  rejected. Another **12** positive compositions covered fresh HELLO and
  BOOTSTATUS after healthy, fragmented and delayed startup, with no diagnostic
  tail after successful control reply. These are source-backed host evidence.
- Independent source review found no unresolved findings, receipt SHA-256
  `460d585ce269a1508ea58b246c1084bbb35d52e6564c6a1803446efc90d715ad`.
- Additive v8 packaging passes: **14** policy files, **3,574** capsule members.
  Manifest SHA-256
  `43153b57e86d072d709f0b01c7726fefc6ae0d77548774aa65c6c316b4aaeed0`;
  assembly SHA-256
  `bd6375091f9dd0dff3075ae9cc2e01d1e8df7afc6e65c361d4bd617fb3442c0c`.
  Frozen v7 remains byte-identical. No operational grant/package is created.
- Both actual isolated v8 `preflight` and `capture-preflight` return `ready`.
  No device enumeration/access, grant, lease or operational package is involved.
- Independent runtime review matches current/copied sources, the exact manifest
  closure, both inert outcomes and retained v7 references. Receipt SHA-256
  `bce6203741a557ce38723f07b29126443eb06739fb68803968abe31e866396b2`.
- Both fresh ESP-IDF 6.0.2 builds pass with Xtensa 15.2.0_20251204 and
  diagnostics ON. All **nine** raw artifact pairs and **23** source-bound
  resource reports match. Actual closure: **377** repository and **2,489**
  installed inputs; all bound inputs stayed unchanged. Six non-application
  artifacts/settings also match the retained prior pair exactly.
- The image map includes the direct ROM reset query and excludes the RTC-hint
  reset constructor/API. One actual compiler-command-derived default-OFF
  `app_main` object contains no diagnostic publisher/format/reset-query
  reference. This is object-level OFF evidence, not a second full OFF build.
- [Pair receipt](../../.private/ot0238c-startup-observability-20261002/firmware/pair-result.json),
  SHA-256 `19d74729f9bd380d09954f1621ba6cddcc51e04b3386c74891735ac323f1f154`.
  The exact sequential build/audit command is
  `C:\Program Files\PowerShell\7\pwsh.exe -NoProfile -File
  .private/ot0238c-startup-observability-20261002/firmware/Run-StartupPairHostAccess.ps1`.

| Candidate artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Raw application, either fresh build | 639520 | `190b9ec6b2d904948a5a350f983c750f82d2d2af08a6f31c22e235a423b7be8e` |
| Existing capture-span padded application | 733184 | `76d3c9f65ebf2d7bc1fc53dc230e3f0b03b3e12b6457e73546cfb980aaf59286` |

The first SDK configure attempt was stopped by the Windows sandbox before
compilation: its ESP32-S3 compiler shim could not resolve its own path (access
error 5). The same exact compiler passed a host-access version check. That
failed directory/log is preserved. The path-only host-access successor uses two
initially absent build directories, the same reviewed source/configuration and
four compiler jobs. No tool installation or product-source change resolved it.

Native checks reuse only verified prior scalar-crypto objects; the target and
SDK-shaped C++ units are rebuilt. Development fixture expectation corrections
are retained separately and are not characterized as physical defects. The
complete firmware porting applicability record is private `firmware-preflight.json`.

## Remaining work and publication

The new [fixed test inputs](../../.private/ot0238c-startup-observability-20261002/next-trial/fixed-inputs.json)
and static validation pass against this v8 runtime and both actual fresh build
audits. No historical 375-input set or old candidate image supplies admission.
The maintained A-only session retains its original 35-minute execution and
60-minute total return ceiling. It does not create new timers at handoff.
Final independent preparation review found no unresolved findings; receipt
SHA-256 `28af4704d003aeb41a689f622bba0fa3d39a18e6d830f132e8a7c9fe5cba2bb5`.
It verifies both builds' actual artifacts, current source/configuration,
raw/padded images, private associations and preserved historical references.

A later device attempt requires
current readiness and exact artifact/original recovery admission. It uses the
maintained A-only candidate path and pair original verification/return, with
no phones, RF, case opening or timed button step.

OT-0238c remains In Progress. No new physical result, enrollment acceptance,
V1 credit, Git mutation/network/publication or public website capability change.
Changes and evidence remain local and uncommitted.
