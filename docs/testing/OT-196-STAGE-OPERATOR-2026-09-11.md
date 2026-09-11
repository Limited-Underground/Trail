# OT-196 executable stage diagnostic and recovery package

Status: composed host and isolated-runtime validation passed; physical trial remains gated.

## Purpose and boundaries

The [OT-195 preparation](OT-195-DURABLE-STAGE-DIAGNOSTICS-2026-09-11.md)
provides a built diagnostic target and a bounded NVS decoder. This successor
binds them to an isolated operator, original-storage custody and restoration.
The existing operators, grants, source files and runtime capsules remain frozen.
The [OT-193 trial](OT-193-DIAGNOSTIC-TRIAL-2026-09-11.md) still establishes a
receipt timeout with zero received bytes, not its physical cause.

The candidate remains `ot195-policy-diag-v0`; its strict wire protocol remains
`SEC_EVAL1 ot187-policy-v0`. The stage record is diagnostic evidence, separate
from receipt acceptance. In particular, `send_return` with no current error does
not establish policy success, writer acknowledgment or USB delivery. This task
does not select a product crypto suite or integrate product radio messaging.

## Package and authority

The new package binds the exact candidate, accepted build report and build
sources, decoder, observation seam, executor, both operator layers, launchers,
runtime manifest and complete original application/NVS descriptors. It rejects
the old image, old schemas, altered sources and unsupported original NVS layouts.
Frozen predecessor sources have independent fixed hashes as well as package pins.

Execute authority explicitly includes the candidate's pre-console NVS
initialization and stage writes and a single pre-restoration full-NVS observation.
Recovery authority excludes candidate write, candidate boot, command submission
and diagnostic recapture. A missing candidate file must not prevent restoration
of otherwise verified originals. Tools validate externally supplied authority;
they do not issue a physical grant.

Freshness has two parts. The decoder proves no retained diagnostic namespace in
the supported original bytes. The backup controller proves current, finite held
ROM custody, exact snapshot descriptors and an unused handoff. File hashes alone
do not prove current device state. Earlier captures remain stale and earlier
grants consumed.

## Execution, interruption and restoration

The controller checks both held originals before consuming custody. Immediately
before each candidate write it compares protected regions, the full original
application and full NVS against the held bytes. The trial completes A's original
restoration and reset before B can begin. A failed evaluation stops further
candidate work; untouched B retains its guarded read/reset-only release path.

The unchanged ROM transport uses `default-reset` to enter ROM before a command
and `no-reset` afterward. The pinned esptool implementation selects the USB
Serial/JTAG reset strategy from the observed USB identity. Its fixed configuration
excludes custom reset sequences. Only an admitted `run` command uses the explicit
hard reset into an application. This is the intended transport ordering, not a
new physical observation that no unintended boot can occur. Identity or reset
uncertainty requires reconciliation.

After receipt success or failure, actual serial closure and idle state precede
the durable restore-intent. A separate durable observation intent binds the role,
attempt, original NVS, package, runtime and journal boundary before the one read.
Raw NVS remains in an exclusive private file; a separate custody receipt binds
its exact bytes. Public results contain only fixed availability, stage and error
fields. Capture, save or decode failure cannot substitute for restoration status.

Recovery does not repeat the observation after interruption. Unconfirmed serial
closure, unhealthy journals and changed original bindings prevent ROM work.
An uncertain original boot prohibits replaying the earlier NVS snapshot over an
original application that may already have run. Original application, full NVS
and protected regions must compare exactly before the final reset.

## Applicable preflight

1. Target boundary: unchanged candidate Heltec WiFi LoRa32 V4.2 / ESP32-S3,
   16 MB flash, application at 0x10000 with a 589,824-byte span and full
   12,288-byte NVS at 0xd000. Prior US915 setup is context only; this is nonradio.
   Product BLE, display, buttons, GNSS and battery measurement are not exercised.
2. Bytes/build: all 37 OT-195 build-source pins, 33 frozen OT-187 source pins
   and both seven-artifact tuples were rehashed. No target, configuration or
   pinned build input changed, so the accepted two fresh builds are reused.
   The new host/runtime package has its own exact source and inventory checks.
3. Transport: strict parser and endpoint are retained. Parent and ROM child
   admission, actual dispatch and failure paths require composed tests. Hardware
   timing and endpoint behavior remain a physical gate.
4. Ordering: preserve the execution journal grammar and A-before-B rule. New
   diagnostic custody is separate, so untouched-role release and no-write
   interruption reconciliation can reuse their accepted validators.
5. Persistence: distinguish intent from completion and diagnostic evidence from
   successful restoration. No observation retry or uncertain original-boot
   NVS replay is allowed. Simulated failures do not prove interrupted flash.
6. Composition: package, real authority objects, custody, executor, backend,
   endpoint, observation and parent/child isolation require validation together.
   Mocked physical transport is software evidence only.
7. Hardware: skipped. No flash, erase, reset, serial access, phone action, fresh
   live snapshot or physical authority is created. A future proposal must name
   the final package and additional NVS behavior, then use fresh device identity,
   finite custody and one-use authority. Complete A restoration before B.

## Validation and remaining gate

The [package preparation record](../../tests/benchmarks/crypto/OT-196-STAGE-OPERATOR-PACKAGE-2026-09-11.json)
pins the exact image, build report and 19 tooling sources. The final runtime has
3,557 admitted files (96,321,672 bytes); normal and hostile-environment
probes verify actual parent imports, isolated child startup and unchanged final
inventory. The real image, source closure, runtime and both retained original
files also compose in an offline package check. Those original files remain
stale; that check grants neither live custody nor physical authority.

| New suite | Passed | Scope |
| --- | ---: | --- |
| Package | 22 | Exact image/report/source/runtime, originals and refusal cases |
| Runtime inventory | 16 | Exact files, dependency closure, configuration and origin boundaries |
| Execution and recovery | 18 | Actual authority/journal/observation/restoration with simulated storage |
| Operator and launcher | 13 | Admission, request parsing and actual PowerShell sentinel dispatch |
| Composed operator | 10 | Actual custody, worker, authority, executor, backend, endpoint and ROM admission |
| Isolated ROM integration | 2 groups | Nine actual PowerShell to isolated Python paths |

The composed operator uses synthetic esptool/serial, image/layout fixture hashes
and an injected platform admission seam for the host process. An additional local
composition passes the accepted 438,784-byte image through both role writes and
restoration, retaining synthetic devices and original layout. The separate
isolated suite runs the actual copied Python, policy files and launchers with an
inert esptool boundary. It covers both ROM modes, hostile startup settings and
malformed, legacy, stale and replayed requests. The Windows job runs these checks;
only non-Windows platforms skip that Windows-specific suite.

The complete normal-user Windows host matrix passed in 15 minutes
35 seconds, including all 13 simulator UI checks. CurrentUser DPAPI used
the established normal-user execution path; no test was disabled. CI now pins
Python 3.14.6 to match the admitted runtime, replacing 3.13. The existing
20-minute job budget and strict protocol deadlines remain unchanged.

After the full host run, the staged byte check caught one trailing whitespace
byte in the new executor. Removing it preserved the complete AST and compiled
code. A fresh second runtime, both startup probes, actual image composition and
30 affected execution/composition/isolated tests passed again. The first runtime
remains an unused, superseded software artifact; no authority or device used it.

Public website capability and V1 completion remain unchanged. A controlled
physical trial, physical entropy and interrupted persistence acceptance, Phase 3,
explicit crypto selection and end-to-end product messaging remain open.
