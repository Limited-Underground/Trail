# OT-192 bounded receipt diagnostics

Status: **complete host matrix and successor isolated-runtime probes pass.**

## Scope and behavior

The [OT-191 trial](OT-191-BACKUP-LAUNCH-2026-09-10.md) returned no accepted
solicited receipt. Its physical cause remains unknown. This software increment
adds a passive observer around the frozen endpoint without changing capture
acceptance, command retries, deadlines or recovery behavior.

The observer uses existing clock samples and I/O results. It adds no reads,
writes, guard calls, scheduled waits or device operations. Observer CPU overhead
is not measured; equal call traces do not establish identical physical timing. A bounded shadow parser reports
whether a syntactically matching challenge-bound receipt was observed.
`matching_receipt_observed` can be true when later trailing output, a guard failure
or a deadline violation causes rejection. `receipt_accepted` becomes true only
when the actual frozen endpoint succeeds. These are diagnostic facts, not a new
security acceptance rule.

Isolated execution and the typed backup handoff attach an
`OT192-RECEIPT-DIAGNOSTICS-1` envelope while retaining the original operation
status. The fixed `inner_endpoint_error` distinguishes read transport failure
from an inner admission or deadline rejection; it identifies the software
refusal point, not its physical cause. Roles, allowlisted stages/errors, bounded
counts and elapsed time survive
confirmed closure. Raw received bytes, exception messages, device identities,
port routes, grants and secrets are excluded. An unexpected summary failure
produces an unavailable envelope with no role details and preserves the result.
Unavailable diagnostic rows provide no evidence about I/O or closure. Accepted
command bytes mean the transport accepted the write, not that firmware executed
the command. Frozen endpoint, capture and original application/full-NVS restoration sources
remain unchanged.

## Focused validation

- Thirty-two differential and compatibility groups compare actual endpoint behavior with and without
  observation, including ordered I/O, guard and clock traces, late output,
  matching data followed by refusal, and failure privacy.
- Eight composition groups execute the actual frozen executor and diagnostic
  backend against synthetic flash and serial handles. They cover A restoration
  before B, failed A stopping B, retained observation after trailing output or
  guard failure, and exception sanitization.
- Composition also runs actual isolated operator dispatch and backup handoff,
  including consumed held custody, grant checks, journals and restoration.
  Diagnostic summary failure preserves the original successful result.

Sources and reproducible tests:
[observer](../../tools/security_policy_diagnostics.py),
[differential tests](../../tests/host/security_policy_diagnostics_tests.py), and
[operator composition tests](../../tests/host/security_policy_diagnostics_operator_tests.py).

The new isolated runtime passed ordinary and poisoned-environment parent/child
probes, with 3,548 files and 96,213,574 runtime bytes. Its manifest SHA-256 is
`7474ab7c898f2c556e3c8fabb77ad817f4d51a9ee0c981f9392c07e1bdaac2ea`.
Python 3.14.6, esptool 5.3.1, pyserial 3.5 and 18 dependency distributions are
bound by the manifest; post-probe inventory verified and no injected startup
marker executed. Six frozen policy modules and all 33 OT-187 target-source pins
remain exact. No firmware rebuild is needed for unchanged target inputs.
The complete affected host matrix passed, including the 32 diagnostic checks,
8 operator composition tests and all 13 simulator UI checks. Documentation,
publication-safety and raw-byte checkout gates pass.

The first GitHub Windows run rejected four new operator fixtures at resolved-path
containment: the fixtures passed an unresolved temporary root directly to a
helper that expects the canonical root. The fixture now canonicalizes its root
before creating descriptors. Production path checks and runtime sources are
unchanged. The exact temporary-path alias spelling was not captured, so no
specific Windows alias mechanism is claimed. A deterministic alias-resolution
regression retains the real containment and non-reparse checks. All nine
composition/path tests pass locally; required CI is rerun on the fix.

## Next admission gate

The software validation gate is complete. Any later physical trial requires
freshly verified originals and full NVS, exact runtime/package binding and new
one-use physical authority. Prior snapshots are stale after original reset and
prior grants remain consumed. This increment performs no hardware access or
new grant consumption. It establishes no physical root cause, security pass,
V1 completion increase or public website status change.
