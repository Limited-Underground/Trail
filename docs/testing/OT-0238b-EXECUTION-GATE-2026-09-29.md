# OT-0238b execution gate and correction proposal

Publication version, 2026-09-29: local workspace paths removed; dated technical
facts and evidence limits are unchanged. Original evidence is retained privately.

## Plain-language result

The firmware item cannot be completed as currently ordered. It asks us to finish
the real enrollment system before the project has approved the security format
that enrollment must store and exchange. The approval for that format, in turn,
waits for tests of the enrollment system. This is a circular requirement.

The batch review incorrectly counted this item as immediately finishable. The
saved producer-gate report already described the conflict; checking only the
task's listed prerequisites missed it. No device or owner attendance is needed
to correct the plan, but successful existing evaluation code must not be
silently declared the selected production design.

## Current verification

The pre-execution live check on 2026-09-29 recorded OT-0238b revision 1
Approved/In Progress. This review then recorded the verified conflict as Blocked
without changing its scope, revision or approval.
OT-0247d, its listed prerequisite, is Completed. Actual source and accepted
design still retain these boundaries:

- The selected phone action produces a volatile request, not a trusted peer.
- The target identity owner only restores a retained identity; it cannot
  provision a new one or expose signing authority.
- Host candidate identity storage uses Ed25519. The accepted product design
  leaves the production algorithm/domain/wire selection to its separate gate.
- A received peer key or phone Boolean cannot authorize a fingerprint review.
- Any later review handoff must retain the original request deadline.

These observations agree with the preserved
[producer-gate report](OT-0238b-PRODUCER-GATE-2026-09-24.md), the
[accepted workflow design](../security/PRODUCT_ENROLLMENT_REKEY_DRAFT_V1.md),
and [current admission assessment](../security/OT-237-CURRENT-ADMISSION-2026-09-16.md).
No code, build, hardware state or cryptographic choice changed in this review.

The current live dependencies differ in detail from the older report, but still
produce the same conflict:

| Task | Current prerequisite |
| --- | --- |
| OT-0237b entropy/startup evidence | OT-0238b |
| OT-0238c enrollment lifecycle validation | OT-0238b |
| OT-0237c persistence evidence | OT-0238c |
| OT-0237d retirement/privacy evidence | OT-0238c and OT-0168c |
| OT-0237e final admission and explicit selection | OT-0237b, OT-0237c and OT-0237d |

The production-selection prerequisite is implicit in the accepted design, so
the website's direct dependency test alone cannot detect this cycle.

## Recommended correction for review

Preserve the admission assessment's intended sequence:

1. Finish and test an explicitly isolated evaluation candidate using the
   existing evaluated profile and accepted enrollment behavior. A candidate
   is not a production selection and cannot create a production trust root.
2. Gather only the still-missing target lifecycle/security evidence. Keep
   separately authorized hardware steps and deferred power tests explicit.
3. Independently accept all eight existing security gates, including the final
   source/license/corpus binding and explicit historical Monocypher custody
   disposition, then select the production identity, fingerprint domain, suite
   and wire contract. Reuse unchanged accepted benchmarks; this does not call
   for repeating them.
4. Bind that selected contract into the production enrollment path and verify
   the resulting complete behavior before claiming product completion.

Before implementing this correction, the revised task record must distinguish
candidate completion from final product binding, preserve the latter as required
work, and adjust dependent acceptance gates consistently. It must name the peer
candidate producer, role/group authority, original timing ownership, isolated
storage, admission/refusal behavior and phone/firmware compatibility. Do not
merely remove a prerequisite or narrow a task while dropping its original goal.

This is a reviewable sequencing proposal, not a scope revision or authorization
to select cryptography, mutate hardware, publish, or bypass any security gate.
No new task IDs are allocated and no existing approval is invalidated here.
OT-0238b must remain unfinished until its scope/order conflict is resolved.

## Evidence and limits

Source review inspected `enrollment_identity_store.hpp`,
`heltec_enrollment_identity_owner.cpp`, selected `start_enrollment` routing,
and the accepted design's selection and timing clauses. Exact live task snapshots
are retained in private engineering evidence and are not public artifacts.

The reviewed source baseline is
`d8d0af223e5ba1fb5a93be5641778a08867e4a1c`. No Git operation changed that
baseline during the review. Existing unrelated work was preserved.
Documentation validation and independent review accompany the batch closeout.
No V1 completion or public website status changed.
