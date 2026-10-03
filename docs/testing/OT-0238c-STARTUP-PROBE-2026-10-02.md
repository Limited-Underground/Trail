# OT-0238c startup-only diagnosis: host preparation and next device plan

Date: 2026-10-02. HOST VALIDATED; historical preparation for the
[subsequently completed physical attempt](../../tests/hardware/OT-0238c-STARTUP-A-2026-10-02.md).
This file does not authorize another physical execution.
Full OT-0238c revision 1 remains In Progress. This auxiliary observation supports
the approved startup-failure investigation; it changes no product correction or
full lifecycle acceptance. [Previous host diagnostics](OT-0238c-STARTUP-DIAGNOSTICS-2026-10-02.md)
and [physical v4 failure](../../tests/hardware/OT-0238c-FIRST-V4-2026-10-02.md)
remain historical evidence. The previous physical cause is still unknown.

## For the owner

- Next proposed check uses Trail Bench and Trail Bench 2 on USB; no phones.
- We save both originals. Only Trail Bench gets the temporary candidate.
- The computer checks its startup automatically. No comparison codes or button
  presses are needed. Keep both cases closed and batteries connected.
- Trail Bench is restored. Trail Bench 2 is checked and restarted into its
  original software, without installing the candidate on it.
- At the end, confirm both usual Trail screens. A blank group field is normal.

Fresh permission, current devices and executable-input admission are still needed.
No new device enumeration, read, write, reset, operational grant or trial occurred
in this preparation. Previous originals/grants cannot be reused as live custody.

## The targeted observation

The maintained controller now has a distinct `startup_A` case. It opens only A,
sends one fresh HELLO, then one BOOTSTATUS after exact READY or the exact owned
HELLO refusal. Any other HELLO error permits close only. No BEGIN, EXPORT,
protocol CLOSE, enrollment checkpoint, restart, status exchange or retry occurs.
Existing commands, parser and firmware bytes are unchanged.

| Observation retained | What it establishes | What remains unknown |
| --- | --- | --- |
| HELLO READY; BOOTSTATUS 0 | A accepted fresh runtime commands. | Enrollment, B startup and subsequent security/lifecycle behavior. |
| HELLO REFUSED; BOOTSTATUS 1-9 | The target's fixed stopped-responder stage. | A unique low-level cause within that stage. |
| HELLO REFUSED; BOOTSTATUS 0 | Initialized runtime followed by refusal/containment. | Healthy readiness or cause of containment. |
| HELLO transport/parser failure | The original fixed failure and last recorded host boundary. | Whether target startup or transport prevented the reply. |
| HELLO refusal; BOOTSTATUS fails | Refusal remains primary; query failure is secondary. | The unobserved firmware stage. |

READY followed by nonzero BOOTSTATUS is contradictory and rejects. BOOTSTATUS 0
never clears a refused HELLO. Startup success means only this auxiliary diagnostic;
it cannot count as successful first enrollment or satisfy OT-0238c acceptance.

The actual target stage map is: 1 USB driver or later read/write; 2 NVS init;
3 reset-marker admission; 4 candidate store; 5 sodium init; 6 display;
7 input owner; 8 partition layout or candidate NVS storage; 9 runtime init.
The stopped loop can answer only if its USB path works. Silence therefore does
not prove an initialization stage or establish a unique physical cause.
Runtime stage 0 is still inspectable after containment and is not a health flag.

## Complete future path and safeguards

1. Recheck approved scope, frozen v6 sources, exact image/profiles, prior ledger
   closure, current identities/routes and ordinary screens. Present this one-case
   boundary for fresh permission before issuing any new single-use authority.
2. Use maintained original acquisition A then B, saving and independently checking
   all six original spans. Capture, candidate handoff and grant share one lease
   and cannot extend the original execution/restoration ceilings.
3. Explicit `startup_A` grant actions permit candidate install/boot/NVS provision
   only A, pair original verification/boot, and A restoration. Broad first-case
   grant actions reject. Close B's native ROM owner and durably record closure;
   logical ROM custody remains held, with no B boot before A observation.
4. Recapture/revalidate both originals before the first write. Install/read back
   A sequentially and prove its guarded boot and native handle closure. Candidate
   boot may write NVS; these operations are hardware mutations, not read-only work.
5. Enter the existing guarded passive factory only A. Runtime/source admission,
   exclusive lease, current route/identity, 115200 baud and DTR/RTS false remain.
   HELLO retains its existing bounded startup-noise handling. Do not flush input,
   relax parsing, reopen, extend a timeout or retry to obtain a different result.
6. Close passive handles. Restore and independently verify A's six original
   spans. Sweep/read back B's captured originals and guarded original boot without
   B candidate/restoration writes. Collect the final pair usual-screen checkpoint,
   close candidate/original ledgers and release the lease only after all proofs.
7. Keep the initial diagnostic failure separate from later cleanup failures.
   Uncertain bytes, boot, handles, pending journals or exhausted restoration leave
   custody held. Review a recovery-only action separately; never replay the trial.

The six spans and protected regions retain the reviewed prior plan: bootloader,
partition, otadata, NVS, application and OTA0 prefix. Original OTA contents are
not presumed blank; original ot_state at 0xf00000 stays protected.
A is Trail Bench/OT-DEV-001, B is Trail Bench 2/OT-DEV-002, using accepted Heltec
V4.2/ESP32-S3/16 MiB evidence. Current electronic identity and image/layout
admission must still be obtained at execution. Region/RF transmission is not
part of this diagnostic; no LoRa exchange or range claim is made.

Propose the existing 35-minute execution / 60-minute total authority ceilings,
including at least 25 minutes reserved for restoration and the unchanged
900-second remaining-execution handoff gate. These are upper limits, not a
physical-duration promise. Once controller startup begins, calculate one
60-second ceiling capped by original execution; each HELLO/BOOTSTATUS query has
one 5-second ceiling capped by that total. Recording and guards consume the
same ceiling. No polling renews it and no human interaction falls in those
query windows. Synchronous OS calls may finish late; post-call rejection stops
further execution rather than claiming guaranteed OS cancellation.
BOOTSTATUS failure may record its fixed secondary event under the still-live
original total cap; it authorizes no subsequent command.

## Validated host result

Independent source review has no unresolved findings. One final affected matrix
passes **10 suites / 260 tests** in 138.23 seconds using three
bounded workers. Actual controller, operator, strict parser, custody, HardwareLease
and runner are composed against simulated SDK/serial/flash and synthetic owner ACKs.
Those results are not hardware acceptance. Differential controls use the actual
frozen v5 policy bytes, preserving all prior evidence.

The controls prove A-only install/open/commands, B held in ROM until cleanup,
no enrollment, exact stage/refusal distinctions, malformed/stale/partial-input
and identity/recording/reentry failures, bounded silence, no deadline extension,
primary failure retention through expiry, exact case-bound result counts,
durable unique sequences and original restoration/lease release.

Startup observations use an exact five-field `OT-CANDIDATE-STARTUP-1` schema:
role A, literal generation 1, ordered HELLO (`ready`/`refused`) followed by one
BOOTSTATUS integer 0-9 or fixed `bootstatus_failed`. At most two observations
are accepted, only for startup_A. B/generation2/BEGIN progress and enrollment
events reject. The separate pre-operation progress schema gains `bootstatus`;
its 64-row bound and existing durable request/grant/sequence bindings remain.
No routes, identifiers, keys, codes, command arguments or exception text are
included. Successful append/readback consumes its sequence before post-write
expiry, preserving the prior correction.

All 375 firmware repository dependencies still match the nine-artifact
reproducible pair. No target changed, so no target build or native/security
matrix rerun was required. The affected Python matrix is new; earlier 76-suite
native evidence is reused only for unchanged inputs.

Applicable porting preflight is covered by exact reused board/artifact/profile
evidence, byte/source pinning, actual USB/ownership/deadline composition and
six-span recovery tests. Target build reproduction is skipped because no target
input changed. Physical identity, write/boot/runtime timing, current-original
and real-screen checks are intentionally deferred until fresh execution admission.
Radio, BLE/phone, GNSS, battery and enrollment behavior are excluded from this
startup-only check and cannot receive acceptance credit.

## Frozen inputs and evidence

Additive v6 binds 14 policy files / 3574 runtime files.
Assembly SHA-256: `06e933f61eabeee7c90a6302da1539e1e23a876bcd29876162f6245a325800b8`.
Manifest SHA-256: `3a5f4072b8c7123eafec3507f02c73619a0849985366da30127941683194c082`.
Source-pins SHA-256: `2f51f5cd5ef4514c2e8a48bd75ad46e76f18353b68ebf5f289a7c6b1b41f054b`.
Raw application remains 637808 bytes, SHA-256
`ee58e250b63b4ded87a688bc88223dc9e450ec827df5b47980219c56acd42f42`.
Historical v5 descriptors and all capsule files remain byte-identical.
Maintained verification and actual isolated `preflight`/`capture-preflight`
pass without a private package, authority, device access or lease consumption.

Private evidence root: `C:/lu/OpenTrail/.private/ot177-publication/.private/ot0238c-startup-probe-20261002`.

- [Controller differential and dedicated 38-test suite](../../.private/ot0238c-startup-probe-20261002/controller/closeout.json)
- [Operator differential and focused controls](../../.private/ot0238c-startup-probe-20261002/operator-validation.json)
- [Custody/runner differential and composed controls](../../.private/ot0238c-startup-probe-20261002/custody-runner-validation.json)
- [Final affected matrix and exact commands/input/log pins](../../.private/ot0238c-startup-probe-20261002/matrix-receipt.json)
- [Independent frozen source review](../../.private/ot0238c-startup-probe-20261002/independent-review-final.json)
- [v6 packaging](../../.private/ot0238c-startup-probe-20261002/packaging.json)
- [Actual isolated inert entrypoints](../../.private/ot0238c-startup-probe-20261002/inert-entrypoints.json)

Commands use `C:/Python314/python.exe -X utf8 -B`. Final helper runs
`.private/ot0238c-startup-probe-20261002/validate_host.py matrix`, then `package`,
then `entrypoints`. Per-suite commands and discriminating before/after failures
are retained in their receipts; documentation and preservation audit follow.

This preparation was followed by one authorized [startup_A physical observation
and complete restoration](../../tests/hardware/OT-0238c-STARTUP-A-2026-10-02.md).
It failed at HELLO's deadline without an accepted startup reply; all originals
and final custody closure are verified. The physical cause remains unknown.
That follow-on [terminal timing and cleanup-failure correction](OT-0238c-TERMINAL-TELEMETRY-2026-10-02.md)
now passes host validation with additive v7; this v6 plan remains historical.
Next: review one v7-bound startup-only procedure/inputs before fresh device
authorization. Deadlines and guards remain unchanged. No repeat trial is
authorized by this file.
No V1 completion credit, public website capability change, Git publication or
deployment. Implementation is local/uncommitted; the full task remains unfinished.
