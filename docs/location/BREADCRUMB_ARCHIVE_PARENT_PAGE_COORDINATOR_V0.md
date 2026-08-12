# Optional Breadcrumb Archive Parent Page v0

Status: **host-tested optional nested page; not the complete client home/menu,
renderer, physical input, or on-device implementation**, 2026-08-12.

## Purpose

The exact-revision navigation handoff intentionally leaves the parent frame to
the application. A wider client shell is not yet complete across messaging,
quick status, critical alerts, position controls, diagnostics, and optional
features, so claiming a finished home UI would be premature.

`BreadcrumbArchiveParentPageCoordinator` provides the narrow, reviewable page
needed around the archive workflow without pretending to be that wider shell.
A broader application explicitly activates it with a newer revision and a
fixed semantic status summary. It presents one `status` frame with exactly two
actions: `open_archive_controls`, then `cancel` (Back).

## Page sequence

1. Activation presents only the parent page. It does not read archive storage,
   acquire the runtime lock, poll input, allocate a lease, or Start capture.
2. Idle service polls one local event against the exact active parent revision.
3. Open delegates to the exact-revision navigation coordinator. First entry
   commits/readbacks the lease but leaves the parent frame visible until the
   next cooperative service presents archive controls.
4. Cancel from archive controls exits the nested workflow and restores this
   parent at the returned minimum newer revision.
5. Cancel from this parent returns `exit_requested`; the broader application
   shell must replace the frame with its own newer page.

The semantic parent status is copied at activation and contains no labels,
coordinates, identities, messages, credentials, endpoints, or record content.
An invalid summary is rejected by `CheckedLocalInterface` before it becomes an
active frame.

## Deferral and failure behavior

A display-not-ready result during initial activation leaves the coordinator
inactive so the same revision can be retried. A display-not-ready result while
restoring after nested Cancel retains a distinct `restoring_parent` state and
retries the exact pending parent revision without re-entering navigation or
allocating another lease.

Display failure, input-source failure, or nested navigation/bootstrap failure
latches this optional page. A lease failure occurs only after the already
presented parent has received the exact local Open action; it cannot touch the
archive runtime or replace the parent with controls. No failure here directly
stops or reconfigures base messaging.

## Authority boundary

This page can present its two-action semantic parent, poll those local actions,
and service the already bounded nested navigation/workflow. It has no broader
home/menu routing, raw touch/button/GPIO access, message/quick-status/critical-
alert action, position trigger, radio, server, remote command, upload override,
automatic Start, or base-client authority.

`open_archive_controls` remains navigation only. A later controls request and
separate exact-revision held confirmation are still required before Start.

## Host evidence

Nine deterministic groups plus 100/100 focused repeats cover:

1. activation presenting only the exact two-action parent with no archive I/O;
2. idle parent polling with no optional-path side effects;
3. Open committing the lease before nested controls presentation;
4. nested Cancel restoring the parent without a new lease;
5. parent Cancel returning control to the broader shell and later reactivation;
6. initial display deferral and same-revision retry;
7. deferred post-workflow restoration without navigation/lease re-entry;
8. lease failure latching after parent display but before runtime access; and
9. invalid summary/revision rejection without archive mutation.

Compile-time checks make the page owner non-copyable and non-movable. The
complete 108-executable C++ host matrix and Python publication checks pass.

This is not rendered wording/layout, a complete application shell, an ESP-IDF
task/backend, physical touch/button behavior, accessibility/localization,
concurrent stress, recovery UX, or device evidence.

## Next gates

- Define one broader client shell that owns the home/status/quick-status/
  critical-alert/position/optional-page revision sequence without making this
  page a base-operation requirement.
- Bind the semantic parent page, nested workflow, target renderer/input,
  runtime lock, secure seed, and `ot_archive` backend on one selected device.
- Physically evaluate page discoverability, Back behavior, stale touches,
  display failure, Start hold, immediate Stop, active indication, and reboot.
