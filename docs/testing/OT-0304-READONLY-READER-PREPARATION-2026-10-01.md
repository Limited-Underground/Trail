# OT-0304 Fresh original-only reader preparation

## Result and limits

VERIFIED, 2026-10-01: the fresh private reader is prepared and host-tested. It
saves the first failing USB/check boundary before cleanup and saves cleanup's
outcome separately. No device was enumerated, opened, reset or written during
this preparation. No phone is needed for the proposed USB check.

The [previous diagnosis](OT-0304-READER-DIAGNOSIS-2026-10-01.md) established that
the tool discarded failure detail; it did not establish the physical cause.
The [closed device attempt](OT-0304-READONLY-FIRMWARE-CHECK-2026-10-01.md), its
failed USB receipts and the owner's manual screen-return confirmation are
unchanged. Current firmware/profile, executing slot and complete recovery
custody remain UNKNOWN. OT-0304 physical first-use remains incomplete.

## Reviewed sequence for one separately authorized check

| Step | Expected result | Refusal and recovery |
| --- | --- | --- |
| Verify frozen inputs | Exact runner, six maintained dependencies, registry and isolated runtime match the preparation receipt | Stop before enumeration if anything changed; external receipt hashes must be checked before issuing/using a grant |
| Reserve one attempt | A write-once reservation and custody lease bind this runner, inputs and Trail Bench inventory | Existing reservation/custody refuses replay; preparation creates neither |
| Identify Trail Bench | Fresh unique USB route, expected ROM identity and flash capacity | Stop on mismatch; one original restart follows any attempted claim |
| Read partition metadata | Exact accepted 4,096-byte table at 0x8000 | A different table stops before OTA/application reads |
| Read OTA metadata | All 8,192 bytes at 0x9000 are erased | A different selection stops before application reads |
| Read factory application prefix twice | Two exact 733,184-byte reads at 0x10000 agree; reuse an existing identical original file | Stop at the first refused read or mismatch; partial bytes never pass |
| Save primary result, then restart | Durable first outcome precedes one maintained original restart, even after an interruption or evidence-write failure | No automatic retry or separate cleanup replay; preserve the first failure |
| Save cleanup and close | Separate cleanup outcome and final result are durable; their exact lease binding is unchanged | Failed restart or evidence closure retains custody for review; owner screen confirmation remains separate |

The table/erased OTA checks guard this fixed read layout. They do not establish
which application is currently executing. The prefix is not a full flash backup
or complete candidate-restoration custody. No candidate, flash/settings write,
phone operation, radio transmission, case opening or battery disconnection is
included. Read-only ROM access can temporarily blank the screen and restarts the
board; the device grant must include those effects.

No short human-response deadline is imposed. Six maintained child operations
each retain their existing 240-second ceiling, plus manifest checks and final
evidence. Ordinary duration is unmeasured; the previous application read failed.
Do not promise a short overall duration or silently extend/retry a refusal.
The owner need only be available for the final normal-screen check and, if
automatic restart fails, clear instructions for a brief RESET/RST press. That
manual fallback is recorded separately from a successful USB restart; it is
not the ten-second BOOT/user reset.

## Implementation and validation

The fresh runner is private under `.private/ot0304-reader-preparation-20261001`.
It delegates identity, ROM commands and reset to the maintained transport;
only verify/read/restart and the three exact read spans are allowed. Write and
candidate entry points refuse. Old wrappers, grants and receipts are immutable.

Failure history is copied and validated against the normalized fixed schema,
bounded to sixteen entries, without raw exceptions, identifiers or payloads.
Primary and cleanup snapshots are write-once and fsynced. Log/capture/snapshot
failures cannot suppress the attempted-claim cleanup and cannot authorize lease
removal. Final-result persistence is independent of journal persistence. A
failed lease removal has a separate closure receipt and cannot be reported as
completed closure. No replay or release-only retry is prepared.

Twenty-eight inert host methods pass: fourteen retained checks and fourteen new
methods cover input/self pins, copied primary/cleanup diagnostics, interruption,
layout refusals, evidence-write failures, binding changes and prohibited writes.
One intermediate assertion expected the old generic journal label; the new
specific boundary required fixture adaptation. No physical result was obtained.
Independent review caught and corrected transient journal/capture failure closure.
The actual frozen Python 3.14.6 capsule's isolated `check` mode passes, using
esptool 5.3.1/pyserial 3.5 without serial enumeration or attempt reservation.

Maintained transport/tool bytes are unchanged from the accepted diagnosis. Its
124 affected methods and sixteen actual-vendor inert cases are reused; another
full matrix or firmware/Android rebuild would not test this private-wrapper
change. Exact commands, hashes, reviews, documentation checks, preservation and
live checklist readback are recorded in the private `closeout.json`.

## Continuation

Obtain fresh Trail Bench readiness and authorization for exactly one original-only
check and original restart. Independently compare the approved runner/input
hashes against the preparation receipt before execute; mutable self-consistent
inputs are not a substitute for that external approval binding. No device grant
was issued by this preparation. Leave the S24 and other pair untouched.

The result will either establish repeatable exact prefix bytes and guarded restart
or preserve the precise first refusal separately from cleanup. A fixed boundary
may narrow the investigation but does not guarantee a unique physical cause.
Settings/full first-use, complete originals and warm-board/production gates remain
separate. No V1 credit or public website capability change. Work is local and
uncommitted; Git publication was not performed.
