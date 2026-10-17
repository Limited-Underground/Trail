# Decision: tracking development boundary

Date: 2026-09-07. Status: parallel server preparation authorized;
tracker firmware deferred until accepted V1, release unscheduled.

The owner directs that current Trail communication firmware V1 proceed unchanged.
Server integration with all actually available current firmware capabilities takes
priority. Tracking preparation may avoid future rework but must not delay that work.
Tracker firmware starts only after V1 is complete and accepted.

Server clarification: communication integration and tracker preparation may proceed
in parallel now, including shared server implementation where feasible without
delaying existing commitments. Prepare identities, roles/capabilities, permissions,
gateway intake, latest-location/history separation, and map support together.
Future tracker fixtures and contract tests are synthetic evidence only; complete
tracker integration cannot be accepted until the firmware exists and is tested.
Coordinate implementation in the server project with its existing work. This is
not permission to expand current V1 firmware or begin tracker firmware early.

Owner clarification: ALL additional Trail communication firmware options wait
until after V1, including inexpensive or preparatory features. The only exception
is a documented, demonstrated necessity to meet an existing accepted V1 requirement;
use the smallest correction needed for that requirement. Server preparation cannot
justify extra firmware scope. Existing in-scope V1 implementation and fixes continue.

Local tracker viewing through Trail communicators/the communications app provides
only the current or most recently received location, with no location logging or
history. Server collection/history is a separate explicitly configured capability.
Future shared Trail relay roles require measured compatibility and capacity evidence.
Third-party ecosystem integration is server-side tracking only, not messaging or
an assumption of shared radio protocols.

The [development plan](../TRACKING_DEVELOPMENT_PLAN.md) records the product modes,
management responsibilities, dependencies, open decisions, and acceptance gates.
This decision does not supersede Decision 0033's V1 topology. It permits server
preparation, not early tracker firmware or unvalidated capability claims. No release
score or tested capability changes.
