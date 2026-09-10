# OT-163 proposed receipt-console hardware test

## Package and authority boundary

The [source/image package](../../tests/benchmarks/crypto/OT-163-RECEIPT-CONSOLE-PACKAGE-2026-09-10.json)
binds the proposed successor application, source closure, build record and distinct
original restoration images. Its [builder and isolated session](../../tools/noise_xk_console_execution_package.py)
reuse the accepted observation runtime and independent recovery coordinator.
The [consolidated batch evidence](OT-163-BOUNDED-RECEIPT-BATCH-2026-09-10.md)
owns the host/target validation result and unresolved limitations.

This is a proposed test plan. A source/image package and successful mocked
execution do not establish current physical identity, installed contents, readiness
or permission to execute. No fresh physical preflight or execution grant is
created by preparing this package. The consumed attempt-4 grant is unusable;
the separate attempt-5 namespace conveys no authority by itself.

## Admission before any mutation

1. Verify the final package and its externally reviewed build-record digest in
   a fresh source-verified caller. Check the complete successor closure, original
   historical inputs and all three image identities against the package.
2. Re-enumerate both intended nodes and bind their current routes to independently
   verified ROM identities. Record actual board models, MCU/flash configuration,
   setup, antenna state and the inherited US915 bench profile. A historical port
   or role assignment is insufficient.
3. Independently read each installed original application with its complete erased
   sector tail. Verify the protected bootloader, partition and OTA regions against
   the retained role-specific baselines; keep NVS outside every write. Any mismatch
   stops admission before candidate installation.
4. Verify a usable independent restoration path for each role. After all applicable
   source, target, readiness and recovery gates pass, bind exactly one fresh
   non-reusable grant to the final caller/package, role mapping and bounded scope.
   Preparing or choosing that binding does not replace explicit execution authority.

## Single bounded run

Install the candidate sequentially, never on both devices simultaneously. Before
each mutation recheck that node's current identity, exact artifact, offset and
recovery path. Write only the application at `0x10000`, within the package's exact
write and erase spans. Independently verify each write before its guarded reset.
Generated bootloader, partition or whole-flash commands are not an installation
plan and are not permitted by this application-only proposal.

Use the unchanged [solicited runner](../../tools/noise_xk_solicited_runner.py)
and observation endpoint. It checks readiness and restart contracts, then executes
two role cycles with initiator/responder reversed. Each cycle contains the baseline
handshake and the existing forced-withheld-message timeout followed by one retry
handshake. Successful admission requires the existing 14-frame validator and its
per-node counters; do not change expected results, receipt deadlines or retry
policy to make a partial run pass. The expected full run is a test contract,
not a measurement already obtained on this candidate.

Late host opening uses solicited readiness, not retained boot output. After each
final serial open, `query_ready()` sends a fresh challenge before requiring a
matching READY followed by PROFILE and STATUS. BOOT, STALE_SELFTEST and COMMANDS
are optional startup records; the accepted runner does not require them first.
The successor suppresses receipt output and non-ready commands/RX processing
until a valid challenge, then emits the unchanged READY/PROFILE/STATUS sequence.
The [purged-boot runner regression](../../tests/host/noise_xk_solicited_runner_tests.py)
exercises loss of initial and reopened boot records through the full simulated
14-frame path; it is host evidence, not physical late-open acceptance.

Retain only the existing privacy-safe diagnostic fields, receipt observations,
accepted frame results and failure stages. Record observed packet counts, loss,
duplicates, timing, bench range context and radio configuration using the existing
validator's meanings. Command-window timing must not be relabeled as pure RF
airtime. A missing receipt does not by itself establish RF failure or root cause.

## Abort, restoration and completion

Any admission, receipt, console, radio or verification failure ends the bounded
attempt through the existing coordinator. Do not retry the physical attempt with
the consumed grant. An uncertain write remains uncertain until independent
readback or recovery proves the exact image.

On success or failure, restore each touched node to its own original application;
never substitute the other role's image. Independently verify the full original
application span and erased tail plus bootloader/partition/OTA preservation.
Every preflight-touched node needs its guarded reset on exit, including a node
that was not candidate-flashed after a later failure. If restoration is incomplete,
use only the package-bound restore-only recovery path; it does not require the
benchmark image and does not grant another radio run. Preserve incomplete recovery
as an explicit blocker rather than claiming successful handback.

Report complete benchmark acceptance only if the entire unchanged validator and
both independent restoration checks pass. Otherwise retain the exact partial
result and outstanding gate. Reset/readback success alone does not establish OLED
contents, phone Ready, pairing persistence or product messaging. No field-range,
release-readiness or physical-root-cause claim follows automatically.
