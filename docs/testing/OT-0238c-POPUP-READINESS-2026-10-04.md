# OT-0238c computer-side popup correction for independent review

## Scope and evidence boundary

The owner authorized computer-only diagnosis and preparation after the
[v11 trial](../../tests/hardware/OT-0238c-FIRST-V11-2026-10-04.md), requiring
Claude review before any further device access. No device enumeration, serial
connection, reset, firmware write, radio operation or new live grant belongs
to this increment. Full enrollment and physical acceptance remain unfinished.

## Identified defects

The real Windows backend accepted a window with its visible style set while
its process thread belonged to a desktop different from the active input
desktop. The synthetic window was never foreground in the inherited launch.
The same frozen backend, launched on the active desktop by a fixed synthetic
test launcher, remained visible and foreground in every sample and cleared
successfully. These are current-host native results, not simulated API results.
Historical v11 retained no desktop telemetry, so its exact disappearance cause
remains unproven; the current reproduction demonstrates a missing launch guard.

Separately, checkpoint failure permanently disables its `CheckpointUI`. The
original final-screen path reused that failed UI and immediately refused.
Custody then masked the exact owned operator error as a generic operation
failure. This source-supported lifecycle defect explains why the final
checkpoint event did not establish a new popup. Native expiry and a host
regression independently reproduced the terminal-handler behavior.

## Bounded correction

| Boundary | Required behavior | Failure behavior |
| --- | --- | --- |
| New real capture or execute | Check the calling process is on the active local console/input desktop before acquiring device ownership | Refuse before capture/runtime/lease construction; release and recovery stay available |
| Native window creation | Recheck desktop availability before creating a window or presenting private rows | No private presentation or implicit acknowledgement |
| Waiting for input | Recheck desktop, visible state and non-minimized state before and after pumping messages | Clear and refuse; retain the actual failure |
| Final original-screen check | Use one fresh cleanup-only view after verified previous clearing, idle resources and the unchanged restoration deadline | Never revive failed enrollment UI or retry uncertain clearing |
| Final acknowledgement | Preserve fresh token, explicit input, verified clearing and durable ACK through the existing controller | Stale, malformed, cancelled, late or duplicate input cannot acknowledge |
| Diagnostic projection | Preserve only exact owned operator errors from a small fixed allowlist | Unknown errors, subclasses and arbitrary strings remain generic |

The live desktop gate intentionally supports the local-console bench; it does
not admit RDP/disconnected sessions. Production default factories use the early
guards. Injected host-test factories remain a separate simulated boundary.
Capture exclusion, private-row minimization, original deadlines, restoration
and retained-backup checks remain enforced. Firmware bytes are unchanged.

## Review and remaining gates

Exact source hashes, native receipts, old/new regression evidence, final host
validation, frozen runtime and review instructions are retained in the selected
checkout under `.private/ot0238c-popup-review-20261004`. The synthetic launcher
can run only its fixed test helper; it is not a production hardware launcher.

Claude must independently review the full actual launch/readiness path and
these source changes before later device admission. A genuine human visibility
check on that launch path is still required before an expensive session. Native
state checks and synthetic presentation do not establish human readability.
No failed or consumed device attempt can be replayed. No physical success,
V1 completion credit, Git publication or public website capability is claimed.
