# Durable Breadcrumb Archive Workflow Bootstrap v0

Status: **host-tested fixed-memory lease-to-workflow composition; no ESP-IDF
backend, parent navigation, renderer, or on-device claim**, 2026-08-12.

## Purpose

The restart-safe lease store and local archive workflow were separately tested,
but a target could still construct the workflow with an arbitrary range or make
Start available before durable allocation completed.

`BreadcrumbArchiveWorkflowBootstrap` closes that common-code composition gap.
It owns the lease store and constructs the non-copyable workflow in inline
optional storage only after one complete lease has committed and read back.
No workflow object exists while the bootstrap is dormant or failed.

## Explicit initialization

Initialization is deliberate; `service()` and `enter()` never allocate storage
implicitly. Before initialization they return `not_initialized` without a
storage read, runtime lock, display write, or input poll.

One successful `initialize()` call:

1. validates nonzero seed/size and a usable initial UI revision;
2. asks the exact `OTBL/v1` store to reserve a durable inclusive range;
3. refuses to continue unless allocation and full readback succeed; and
4. constructs the workflow with exactly the committed first/final IDs.

Repeated initialization in the same boot returns `already_ready` and does not
read or mutate storage again. A later workflow service is the first point at
which archive runtime status or local display state can be touched.

The bootstrap, workflow coordinator, and consent controller are explicitly
non-copyable and non-movable. Target code therefore cannot accidentally clone
an in-memory cursor and consume the same leased ID from two owners.

## Failure behavior

Invalid configuration fails before storage access. Any read, integrity,
version, conflict, exhaustion, write, sync, or verification failure latches the
optional bootstrap for that object lifetime. It does not retry, erase, reset,
construct a workflow, poll input, touch the runtime, or present controls.

This one-boot latch matters when a write outcome is uncertain. If the final
commit actually became durable, a fresh boot/store inspection will treat the
whole range as consumed and reserve the following range. If it did not apply,
the durable predecessor remains authoritative. The failed object itself never
guesses which outcome occurred.

The bootstrap has no radio, group, packet, GNSS, message, upload, server,
account, remote-command, automatic-Start, or base-client authority. Its absence
or failure therefore cannot directly stop/reconfigure base messaging. That is
a structural common-code boundary; a selected target must still prove its task
and dependency graph preserve the same independence.

## Host evidence

Eight deterministic scenario groups plus 100/100 focused repeats cover:

1. dormant service/entry rejection with no storage/runtime/display access;
2. invalid request/revision failure before storage or runtime access;
3. full durable commit before workflow construction and first service;
4. idempotent same-boot initialization with exactly one lease;
5. restart allocation of the next nonoverlapping range while ignoring a later
   seed once durable state exists;
6. storage failure latching with no same-object retry or workflow call;
7. applied-but-reported-failed commit recovery abandoning the uncertain range;
   and
8. a one-ID lease starting once, stopping, then refusing another Start without
   a clock read or range crossing.

Compile-time checks reject copying/moving the bootstrap, workflow, and consent
owners. The complete 106-executable C++ host matrix and Python publication
checks pass.

This is not an ESP-IDF/NVS adapter, CSPRNG seed source, authenticated or
rollback-resistant store, recovery UI, parent menu owner, renderer, physical
input, concurrent task proof, brownout/endurance test, server protocol, or
on-device result.

## Next gates

- Bind the bootstrap to the selected target's exact `ot_archive` backend and an
  approved secure nonzero seed source for genuinely blank storage.
- Make parent navigation initialize this optional path before presenting its
  archive page; failure must remain a local archive-unavailable result while
  base messaging stays usable.
- Define explicit operator recovery for corrupt, uncommitted, unsupported,
  exhausted, or rollback-suspect state without adding an automatic reset.
- Run concurrent target stress, reset/brownout injection, flash-wear
  measurement, rendered consent review, and physical Start/Stop tests.
