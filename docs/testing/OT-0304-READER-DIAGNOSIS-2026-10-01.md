# OT-0304 USB-reader failure attribution

## Result and limits

VERIFIED, 2026-10-01: the reader discarded the failing substep. It now retains
bounded, privacy-safe failure metadata without changing commands, reset policy,
identity checks, flash spans, authorization or acceptance decisions.

The [previous original-only check](OT-0304-READONLY-FIRMWARE-CHECK-2026-10-01.md)
reported `read_application`, followed by failed automatic restart. That label
does not identify whether connection, transfer, parsing, output validation or
post-command USB enumeration failed. Its approximately 31.771-second read
operation and 14.256-second cleanup operation include prerequisite commands.
They are not isolated transfer/reset measurements.

UNKNOWN: the physical failure's cause, current application/version/profile,
executing boot slot and complete original recovery custody. Failed receipts and
manual screen-return closeout remain unchanged. No device was accessed in this
task, and no firmware/app build, installation, reset or radio test occurred.

## Complete reader flow

The parent verifies the frozen interpreter/packages and launches an isolated
child using stdin for private identity/route data. Before each ROM command, the
child requires the unique matching current USB route. Separate `read-mac` and
`flash-id` calls establish the expected electronic identity and flash capacity.
The selected read then connects, executes and performs vendor cleanup; output
limits and the matching post-command USB route must pass before file length and
the parent response are accepted. Original restart repeats the identity/capacity
checks and uses the existing explicit `run` plus hard-reset sequence.

A completed transfer followed by failed enumeration remains a refusal. The
change does not add retries, wait out a failed guard, substitute another route,
accept partial data or claim a successful restart from a returning screen.

The production read uses `no-reset` after the command. Reset/port closure are
owned by the pinned vendor CLI. The old inert release test assumed unconditional
port closure; it now explicitly describes only its simulated argv contract.

## Correction

`tools/ble_confirmation_trial_transport.py` records the requested operation,
exact fixed stage and fixed error category. Stages distinguish runtime/request,
identity/capacity parsing, USB checks, command output, file/length validation,
connection, command execution and teardown. In-memory observers delegate to the
actual pinned vendor functions with unchanged arguments, return values and
exceptions, then restore their aliases in `finally`. They capture the first
exception before Click cleanup can replace it and erase its context.

The parent requires an exact schema, approved vocabulary and operation/stage
compatibility. Untrusted or malformed replies remain refused and receive a
fixed parent boundary. Public exception text stays `ble_trial_transport_refused`;
raw exceptions, diagnostics, hardware addresses, routes and payloads are never
retained. `failure_diagnostics` returns copies of a history bounded to the first
failure plus the latest fifteen, so later cleanup cannot erase the primary
failure. Serial timeout exceptions belong to the serial category; categories
do not claim a unique physical cause.

This history is available to a future fresh operator. The consumed reader and
cleanup wrappers/receipts were not modified or replayed. A new authorized
original-only runner must persist the history after both read and cleanup;
this correction does not retrospectively recover missing diagnostics.

## Validation

Exact commands, final source hashes, logs and receipts are under
`.private/ot0304-reader-diagnosis-20261001`; `closeout.json` owns the final inventory.

The actual pinned esptool 5.3.1 CLI fixture replaces only its physical device
provider with an inert ESP and forbids serial open/enumeration. Sixteen cases
exercise the unchanged vendor CLI and actual worker observer. They establish
that teardown can replace an earlier command error, erase its context and skip
explicit port closure; the observer retains the first failure and restores all
aliases. Production read cases retain `no-reset`; read-plus-hard-reset cases are
explicitly artificial stress cases. Process exit still releases OS handles, so
this is not proof of a persistent handle leak or the previous physical cause.

The transport suite passes 48/48 methods, including 19 new regressions covering
post-command route refusal, parsing and file
boundaries, malformed/spoofed metadata, standalone probe failure, cleanup and
bounded copied history. The five-suite affected operator/transport/startup/pair/
enrolled matrix passes 124/124 methods; the actual-vendor fixture passes 16/16.
The frozen-runtime device-free probe passes with zero serial enumeration/access.
Repository documentation checks and exact final outcomes are recorded in the
private closeout. No firmware target includes this Python-only tooling change;
no firmware or Android rebuild is required for its acceptance boundary.

Two intermediate vendor-fixture assertions incorrectly assumed exception context
would survive Click cleanup; actual execution disproved that assumption. An
intermediate worker fixture lacked the newly required observer globals/aliases.
Those test-fixture development failures are preserved separately from production
failures. Independent review found missing standalone-probe history initialization
and an impossible runtime-operation metadata combination; both were corrected.

## Continuation

Prepare a fresh original-only measurement with current tool pins and durable
failure-history capture. Obtain a new bounded device grant/readiness before
executing it; do not replay the closed read-only session. Stop at the first
distinguishing rejection and preserve the failed read separately from cleanup.
No phone or candidate installation is needed to inspect this USB boundary.

OT-0304 remains incomplete: settings/first-use, complete recovery custody,
warm-board and production gates remain separate. The existing Note20 checkpoint
is preserved. No V1 credit, public website capability change or Git publication
is claimed. Changes are local and uncommitted.
