# OT-163 bounded completion-receipt observations

## Evidence and limitation

[Physical attempt 3](OT-163-CONTAINED-EXECUTION-2026-09-09.md) retained successful
m3 transmit and receive-rearm returns but timed out awaiting TX_DONE. Its zero
parser-miss counters cannot distinguish absent bytes from an unterminated line:
the inherited endpoint clears pending bytes when it fails. This remains an
unknown physical cause, not proof that USB lost a receipt.

A host-only reproducer checked the exact accepted generated TX_DONE format with
synthetic identifiers: a complete 319-byte receipt and all 318 two-fragment splits
are accepted. All 319 proper prefixes time out, including a missing newline
delivered in 64-byte chunks. Every prefix retains both preceding checkpoints and
zero parser misses. Injected truncation establishes this diagnostic ambiguity;
it does not establish what the physical device emitted or the host received.

## Additive observer and integration

The separate `ReceiptObservationEndpoint` retains one bounded row per `expect`:
finite expected-kind/outcome categories, read/empty-read/error/invalid-read
counts, returned-byte counts, parsed-line counts, last-read size and pending
length/state. Pending state is captured before the predecessor clears its buffer.
A read returned at the deadline is counted as returned bytes but is still rejected
before parsing, exactly as before. Counts saturate at 65,535 and each endpoint or
archived runtime retains at most 128 rows. Raw bytes, identifiers, receipt fields,
caller strings and exception text are not added to observations.

The observer delegates to the existing parser and temporarily wraps the serial
handle during `expect`, restoring its original identity in `finally`. It adds no
clock calls, retry, read, deadline extension or newline repair. Readiness queries,
return-object identity, exceptions, framing and budgets retain their prior policy.

`ReceiptObservationSession` composes the observer through the guarded concrete
backend, preserving role identity, serial leases, fresh handles and separate
restoration images. It adds two source files to the accepted 40-source closure;
all 40 historical inputs and firmware bytes remain unchanged. The 42-source
verifier runs before the unchanged predecessor admission filters. Recovery uses
its own namespace and retains restore-only operation without the benchmark image.
Observations are archived across detachment/reopen/close; diagnostic failures are
separate from restoration evidence.

## Validation checkpoint

Twelve endpoint test groups and eight composed execution groups pass. Endpoint
tests cover all splits/prefixes, unchanged clock/read calls and failure outcomes,
late reads, buffered next lines, original read/size budgets, private-value
redaction, exception identity, bounded snapshots and readiness parity. Composed
tests exercise actual byte flow, lifecycle retention and admission/recovery.
The required Windows host matrix and repository documentation checks gate
publication; their results are recorded on the corresponding pull request.

Reproducible checks from the repository root:

```powershell
python tests/host/noise_xk_receipt_observation_endpoint_tests.py
python tests/host/noise_xk_receipt_observation_execution_tests.py
./tools/Test-Host.ps1
python tests/host/repository_docs_tests.py
python tools/check_repository_docs.py
python tools/Test-PublicationSafety.py
python tests/host/v1_v15_scope_admission_tests.py
```

The full matrix uses the normal Windows account for the existing DPAPI checks.

No new grant, source-bound physical snapshot, hardware operation, firmware flash,
phone change or radio result is created by this host-only increment. Previous
attempt grants remain consumed. V1 remains 45.50 exact / 46 displayed; the
historical baseline remains 31.75 exact / 32 displayed. Public capability and
website status are unchanged.

## Next gate

After the required publication checks pass, prepare a fresh exact
42-source/image-bound caller and one-use namespace. Its preflight must verify
current role identities, installed originals, protected regions and independent
restoration paths before any new bounded physical attempt. Preserve the existing
five-second receipt deadline. A later observation can distinguish transport byte
states; neither a successful host test nor a partial receipt establishes a
physical root cause, complete radio-cost result or product messaging.
