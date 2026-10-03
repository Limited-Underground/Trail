# OT-0238c startup-only test with terminal timing

Prepared 2026-10-02. Static preparation only; fresh device permission and
readiness remain required. OT-0238c revision 1 remains In Progress.

## What the owner needs to do

- Connect Trail Bench and Trail Bench 2 to USB, showing their usual screens.
- Keep cases closed and batteries connected. Phones are not needed.
- The computer saves both originals, temporarily installs the candidate on
  Trail Bench, checks its startup, then restores it. Trail Bench 2 keeps its
  original firmware; it is checked and restarted during cleanup.
- At the end, check that both usual screens returned and acknowledge the
  computer's final checkpoint. A blank group field there is normal.
- No comparison codes or timed button presses. Allow up to an hour including
  setup and restoration; the automatic startup check itself is brief.

This is one diagnostic attempt. It does not complete enrollment or V1.

## Why this attempt is different

The [previous startup attempt](../../tests/hardware/OT-0238c-STARTUP-A-2026-10-02.md)
ended at `hello_A / deadline_expired`; its underlying cause remains unknown.
The [validated terminal telemetry](OT-0238c-TERMINAL-TELEMETRY-2026-10-02.md)
now preserves the actual boundary, transport counts and independent timing.
No firmware, parser, authority guard or deadline changed.

| Retained observation | Supported conclusion | Do not infer |
| --- | --- | --- |
| No transport snapshot; wrapper owner/progress boundary | The wrapper did not enter the USB Endpoint. | Firmware silence or a sent command. |
| Zero attempted writes; guard boundary | Failure preceded the client write call. | A device rejection. |
| Attempted write without return | The write call did not return normally. | That no bytes reached the device. |
| Returned write; zero received bytes | The client recorded no received bytes before rejection. | A unique firmware or USB fault. |
| Received bytes without validated parse | Transport received something but no accepted reply. | The content of discarded bytes or a boot stage. |
| Parsed reply; no accepted return | A later check prevented acceptance. | Successful startup. |
| READY and BOOTSTATUS 0 | The candidate answered both startup commands. | Enrollment, radio or production acceptance. |
| REFUSED and BOOTSTATUS 1–9 | The fixed firmware stage is available. | A unique low-level fault inside that stage. |
| No durable query summary | Observation is incomplete; inspect the owned recording/authority failure. | Zero writes, no reply or successful cleanup. |

Timing is valid only when its `timing_valid` flag is true. Controller opening
cost uses existing sampled clock values and can be a lower bound. Independent
performance timings cannot be subtracted from authority-clock timestamps.
Summary persistence occurs after passive closure under the original execution
ceiling, not an expired per-command ceiling. It is not guaranteed after execution
authority expires or durable recording fails. Preserve primary and cleanup
failures separately, including restoration method and limit/call/postcheck data.

## Bound inputs and expected flow

Private prepared inputs and the independent review belong to
`.private/ot0238c-startup-v7-plan-20261002/` in this checkout.
The prepared `session.py` requires a fresh owner authorization record before
any operational action. None has been supplied by this plan.

V7 assembly SHA-256:
`e022aa7ec2047d370484f1d0071fc253494521dc801d40a9f610a32ad31698eb`.
V7 runtime manifest SHA-256:
`bedf65fca7503ec635983497a305eed2243987d40daba2474f6eb64d66c6e8ab`.
Raw application: 637808 bytes, SHA-256
`ee58e250b63b4ded87a688bc88223dc9e450ec827df5b47980219c56acd42f42`.
The private descriptors own both raw/padded image bindings, six original spans,
protected region, accepted board profiles and fourteen current policy sources.
Firmware build reuse requires its 375 source dependency pins to remain exact.

A is Trail Bench / OT-DEV-001; B is Trail Bench 2 / OT-DEV-002. Both are the
previously admitted Heltec V4.2 / ESP32-S3 / 16 MiB units. Static inventory
associations are preparation evidence; current identity/routes are live gates.

| Before state and owner | Operation and required evidence | Failure path |
| --- | --- | --- |
| No live authority or handles | Refresh checklist, input pins and prior ledger closure; obtain the owner's exact one-trial reply and current ordinary screens. | Stop before device access on drift or missing permission. |
| Authorized, no open port | Maintained route/identity admission and single-use original capture, A then B; independently retain both six-span originals and held handoff. | Restore/release acquired custody through maintained capture cleanup. |
| Originals held by capture | Admit the exact startup_A/group-1 package against fresh originals, source/runtime, lease and the same original ceilings. | No candidate write if admission or execution reserve fails. |
| Candidate custody admitted | Close B's ROM handle without booting it; install/read back and guarded-boot A; close its ROM handle before passive USB. | Restore A if required; B receives no candidate/restoration write. |
| A passive owner | One HELLO; one BOOTSTATUS only after READY or the owned HELLO refusal; preserve fixed terminal telemetry. | Any other HELLO error allows close only; no reopen, flush or retry. |
| Passive handles closed | Persist bounded summary; restore/verify A; verify unchanged B originals and guarded-boot B. | Preserve first failure plus independent cleanup failure; hold uncertain custody. |
| Verified originals, handles closed | Real pair usual-screen checkpoint/ACK, closed journals, no pending records and released leases. | Missing visual ACK or release proof is unfinished closure. |

The existing maintained controller, custody, adapters and isolated runner own
these operations. The [earlier startup plan](OT-0238c-STARTUP-PROBE-2026-10-02.md)
supplies unchanged stage semantics and six-span boundaries; its v6 artifacts
and consumed authority are historical, not execution inputs for this plan.
No BEGIN, enrollment, status transfer, RF, phone, case opening or battery removal.

## Timing, recovery and final audit

One fresh issuance sets 35 minutes for execution and 60 minutes total for
restoration, reserving at least 25 minutes. Candidate handoff must retain at
least 900 seconds of execution and cannot renew either ceiling. Startup has
one 60-second cap; each HELLO/BOOTSTATUS gets at most five seconds within it.
Human interaction is outside those query windows. Synchronous OS calls may
return late; their post-call rejection does not guarantee OS cancellation.

Use v7 readers for v7 journals. New readers accept old journals; frozen v6
readers do not accept the extended v7 shape. Do not reuse the previous physical
`recovery.py` or `audit_result.py` unchanged: they contain expired attempt
ceilings, specific old role assumptions and pre-v7 result expectations.

If cleanup is held, review the actual fresh journal, process/handle state and
saved originals before a separately authorized recovery-only operation. Use
the maintained runtime `launch(..., 'recover', ...)` entrypoint with the same
request/runtime and origin attempt. Bind UTC limits to the original capture's
cleanup expiry and clamp both child monotonic limits and process timeout to
the original capture admission's cleanup deadline. Never renew an expired cap.
An ambiguous original reset cannot justify rewriting possibly new app state.

Before outer release, require recovered outcome, no new runner failure,
released candidate custody/lease, real `usual_screen` observation plus typed
ACK, and both roles restored/verified, original-boot-allowed and handles closed.
Candidate ACTIVE, pending and unaccepted records must be absent. Check the
actual controller/child process closure. A historical cleanup failure remains
recorded after recovery and must not be mistaken for a new release failure.
If it existed before recovery, verify that it remains exactly preserved.

Only then admit the maintained `launch(..., 'release', ...)` route against the
matching original capture ACTIVE record and the same earliest ceilings.
Final outer outcome can preserve a historical failure while custody/leases are
released: audit actual flags and journals rather than the outcome label alone.
No recovery package or new recovery authority is created in this preparation.

The final physical audit must accept the fixed v7 QUERY-SUMMARY schema, bind it
to the actual request/grant/sequence, preserve absent telemetry honestly, and
read `cleanup_failure` and `restoration_failures` from candidate receipts.
Do not use the old audit's closed list of event schemas. Retain actual device
screen confirmation separately from byte/readback and commanded-reset evidence.

## Preparation acceptance

Reuse the stable 291-test matrix, fifteen independent edge checks and both
isolated entrypoints from the linked host report while their inputs match.
This increment checks static bindings, prior closure, the narrow session wrapper
adaptation and this complete procedure. It does not repeat those suites or build
unchanged firmware. Porting preflight reuses exact target/layout/build evidence;
physical identity, USB timing, actual startup and restoration remain live gates.
Radio, BLE, battery and GNSS checks are outside this diagnostic.

No device access, operational authority/package, V1 credit, public website
capability change or Git publication is established by preparing this plan.
Next: fresh readiness and permission for exactly the one described test, then
current capture/candidate package admission before the first candidate write.
