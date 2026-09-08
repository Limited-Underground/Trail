# OT-163 Bounded retry diagnostics

## Evidence boundary

Host validation only, 2026-09-08. The previous physical attempt stopped at the
broad `cycle1_retry_timeout` stage. It retained no individual retry receipts,
so its exact failing operation cannot be recovered from that record. Both
boards were restored; see [execution evidence](OT-163-SOLICITED-EXECUTION-2026-09-08.md).

## Change

[`RetryDiagnostics`](../../tools/noise_xk_retry_diagnostics.py) wraps injected
endpoints without opening a port, changing runner validation or issuing extra
commands. Its bounded ring retains operation start/success/failure, anonymous
A/B role, expected and returned receipt kind, and allowlisted numeric, flag and
enum fields. It omits raw commands, session/attempt identifiers, challenges,
digests, hardware identifiers and raw exception text. Unknown errors become
`operation_failed`. Capacity defaults to 128 records and is limited to 512.

The endpoint returns the original result object or raises the original exception.
A successful `expect` event means a receipt was returned, not that the runner
accepted its contents. The last event and safe fields locate a validation
boundary; they do not alone establish root cause. Snapshots are independent
copies. Diagnostics remain in memory until the caller explicitly records them.

## Validation

Run `python tests/host/noise_xk_retry_diagnostics_tests.py`. Eight groups cover:

- The real solicited runtime and runner retain the byte-identical simulated
  14-frame, 736-byte validated result.
- Thirteen corrupted retry receipt boundaries identify the last A/B operation
  while retaining the original `cycle1_retry_timeout` error: prepare, M1
  transmit/receive and stage checks, M2 arm/withhold, timeout and abort.
- Delayed asynchronous timeout delivery occurs at the actual 2,196 ms deadline
  for both roles and preserves the complete result.
- All delegated arguments, return identities and exception identities survive.
- Private fields and unknown/raw errors are excluded; scalars and ring size are
  bounded; snapshots cannot mutate retained records; hostile receipt properties
  cannot mask endpoint results.

The test is part of the required Windows host matrix. The existing 24-file
execution snapshot is unchanged. No firmware build or physical test is implied.
Source review found matching timeout and withheld-M2 behavior, not a demonstrated
explanation for the physical abort. The old immediate-timeout fixture was a
coverage gap; the delayed test closes that gap without claiming a hardware fix.

## Next gate

Bind the observer and its integration into a fresh exact execution snapshot and
one-use attempt with new recovery records. Recheck both board identities and
installed/preserved spans, then capture the actual failure boundary while keeping
independent per-role restoration. The consumed prior grant must not be reused.
This standalone observer is not retroactively part of the prior binding.

No hardware, phone, bond or firmware changed in this increment. No complete radio
benchmark, product messaging or crypto selection is admitted. V1 milestones
remain unchanged at 45.50 weighted / 46 displayed; website updates remain deferred.
