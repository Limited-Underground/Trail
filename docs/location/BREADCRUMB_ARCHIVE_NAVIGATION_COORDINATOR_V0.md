# Exact-Revision Breadcrumb Archive Navigation Handoff v0

Status: **host-tested parent/workflow handoff; no complete application shell,
renderer, physical input, or on-device claim**, 2026-08-12.

## Purpose

The durable workflow bootstrap guarantees lease-before-workflow construction,
but a parent application still needs one safe way to enter and leave this
optional page. Ad hoc routing could accept a stale touch, allocate a lease from
an unrelated event, reset the boot cursor during re-entry, or accidentally
create a second control path.

`BreadcrumbArchiveNavigationCoordinator` owns the exact-revision handoff. It
does not create or poll the parent frame; the external application shell keeps
that responsibility. It accepts only an already resolved local
`open_archive_controls` action whose revision exactly matches the currently
active parent frame in `CheckedLocalInterface`.

## First entry

The first valid open action derives the workflow's initial revision as
`parent revision + 1` and explicitly initializes the durable bootstrap. A lease
must commit and read back before the navigation mode changes to `workflow`.
Controls are not presented during `open()`; a later cooperative `service()` is
the first runtime snapshot and display call.

A stale, unresolved, wrong-action, wrong-screen-state, already-open, or
revision-exhausted request is rejected before storage mutation. A lease failure
latches the optional navigation path without a workflow/runtime call. No open
action maps directly to Start, Stop, upload, discard, retrieval, or server
authority.

## Exit and re-entry

Cancel inside the archive controls closes the workflow. The navigation owner
returns `exit_requested` plus the minimum next parent revision; it does not
invent or present a home/status frame. The application shell must present its
own newer parent frame.

If a later exact active parent frame resolves `open_archive_controls`, the
owner delegates to the existing workflow re-entry contract. The same
non-copyable boot workflow and already committed lease are retained; no new
storage read/write or session-range allocation occurs. Intervening parent
screens may advance the shared revision, so re-entry may use a later revision
than the reported minimum as long as the checked interface proves it is the
exact active frame.

## Authority and failure boundary

This coordinator has no parent-frame renderer, raw touch/button/GPIO input,
radio, group, message, position, upload, server, remote command, automatic
Start, or base-client authority. While parent mode is idle, `service()` does
nothing to the optional path. A failed archive handoff cannot directly stop or
reconfigure base messaging.

Temporary workflow snapshot/display/runtime outcomes remain typed and owned by
the existing workflow; navigation forwards them rather than converting them
into a new control path. Only bootstrap unavailability or a structurally
impossible workflow entry latches this handoff.

## Host evidence

Eight deterministic scenario groups plus 100/100 focused repeats cover:

1. idle parent service with no storage/runtime/display side effects;
2. stale or invalid open rejection before lease allocation;
3. first exact local open committing the lease before controls presentation;
4. an open while already active being rejected without another lease;
5. Cancel exit, minimum-parent-revision handoff, exact re-entry, and reuse of
   the same boot lease;
6. re-entry from a later exact active parent revision;
7. lease failure latching before runtime/control presentation; and
8. exhausted parent revision rejection before storage access.

Compile-time checks make the handoff non-copyable and non-movable. The complete
107-executable C++ host matrix and Python publication checks pass.

This is not a complete parent menu/home coordinator, ESP-IDF task/backend,
renderer, localization/accessibility review, physical input proof, secure seed
source, recovery UI, concurrent stress result, or on-device measurement.

## Next gates

- Define the broader parent application shell and decide where this optional
  entry belongs without making archive service prominent or required.
- Bind the checked handoff, durable bootstrap, renderer, input adapter, runtime
  lock, and `ot_archive` backend on one selected client target.
- Physically verify stale-touch rejection, Start hold, immediate Stop, Cancel,
  active indication, display failure, reboot/recovery, and base independence.
