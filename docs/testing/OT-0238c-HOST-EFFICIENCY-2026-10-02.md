# OT-0238c guarded-read controller correction

2026-10-02; approved revision 1, host increment within In Progress OT-0238c.
Implementation, independent review, the final 76-suite affected matrix and
both actual isolated assembly-only entrypoints pass. No device access,
enumeration, target firmware change/build, operational grant issuance, publication or deployment.

## What changed and what stays required

The original capture and candidate custody readers now use an explicitly
opted-in guarded read: one isolated worker and one ROM connection perform the
full fresh admission, exact span read and full fresh admission again. Both
typed admissions, exact bytes/size, caller checks and confirmed handle closure
must pass before returned bytes or persisted coordinator pins are accepted.
Backends without literal True
capability retain guard/read/guard; an atomic failure never retries that path.
The bound runner forwards the operation through the same fixed execution and
restoration ceilings. No deadline, invitation window or accepted decision moves.

Review identified the pinned SDK's per-connection flash-ID cache. Each atomic
admission explicitly refreshes flash ID with cache=False before flash size/type
checks. Security is uncached; chip/stub/download mode, USB route, MAC,
partition layout and boot selection are checked again on the same live handle.
This includes checks after the requested read, not reused admission results.

Both independent six-span acquisition passes, the six-span freshness pass,
claim/mutation guards, exact original/image pins, journal barriers, restoration
verification, reset sweep and immediate pre-reset NVS reread remain required.
Passive ownership, lost handles, state/identity drift, short bytes, malformed
responses, expiry, clock rollback and close failures still refuse the operation.

## Failure evidence

Genuine owned adapter/controller failures retain only reviewed fixed categories
through capture, custody and runner. Worker clock/deadline checks report their
own rejection at that instant; later checks and close errors do not replace
the first worker failure. Unrecognized SDK/subprocess errors remain
rom_operation_failed. Unknown, spoofed, multi-argument and private-text errors
are normalized; raw device identifiers, routes or SDK messages are not exposed.
No later clock sample is used to infer the cause of an earlier rejection.

The [previous physical install_B failure](../../tests/hardware/OT-0238c-FIRST-V3-2026-10-01.md)
remains unknown: its generic stored category cannot recover the missing detail.
Both originals and custody ledgers remain restored/released. This increment
does not reinterpret that trial or establish successful enrollment.

## Validation and execution artifact

- Actual WORKER_LOGIC with simulated SDK/clock demonstrates capture launches
  **74 to 26** and fresh-check launches **36 to 12**, with both acquisition
  passes, all six spans and the unchanged absolute ceilings. One guarded
  operation uses one handle and two fresh admissions, including fresh flash ID.
  Counts are host evidence; physical wall-time savings remain unmeasured.
- Focused adapter, custody, original-capture and runner suites pass; the final
  complete **76-suite** current-source matrix passes once on the final inputs.
  It reuses the exact 733-file accepted dependency closure, independently
  verifies its fresh copy and disables URL access; all current tests/builds run.
- Additive frozen runtime v4 binds fourteen current policy files and 3,574
  capsule files. Maintained assembly verification and both actual isolated
  preflight/capture-preflight entrypoints return ready without a private package,
  device enumeration/access, lease or grant consumption. Frozen v3 remains
  unchanged historical evidence; its old operational inputs are not reused.

Exact source/artifact hashes, focused/final commands, independent review,
dependency reuse and entrypoint results:
`.private/ot0238c-host-efficiency-20261002/`. The maintained validation helper
is run with `C:\Python314\python.exe -X utf8 -B` and arguments `matrix`,
`package`, `entrypoints`; full native/child commands are in
`final-security-matrix/commands.json`.

## Remaining gate

Prepare and review one bounded first-enrollment attempt with v4-bound current
inputs, fresh original acquisition/profile checks, fixed authority/deadlines,
recovery path and fresh owner/device readiness. Hardware execution remains
separately authorized. First enrollment, remaining lifecycle cases, final
security/production selection and full OT-0238c acceptance stay open.
No V1 weighted credit or public website capability change. Local implementation
is ready for review; Git publication remains separately scoped.
