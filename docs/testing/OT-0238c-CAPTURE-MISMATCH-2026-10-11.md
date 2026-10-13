# OT-0238c mismatch cleanup correction, 2026-10-11

VERIFIED host-only correction. The [physical attempt](../../tests/hardware/OT-0238c-CAPTURE-MISMATCH-2026-10-11.md)
failed before candidate installation; its exact changed settings remain UNKNOWN
because the rejected read was not saved. This correction does not establish
retained restart/rekey acceptance or make a differing baseline eligible for
candidate installation.

## Corrected boundary

Candidate freshness remains strict: any retained mismatch stops installation.
The old cleanup also required retained NVS equality, which could prevent release
of untouched original firmware. The new path applies only to terminal,
pre-adoption capture cleanup in the same process and continuous device hold.
It validates all five non-NVS spans against retained originals, parses current
NVS, checks absence of live reset-marker keys and verifies a stable reread.

Differing current NVS is durably saved and read back as separate private failure
evidence before reset intent. It never replaces retained originals or becomes a
candidate/backup proof. The reset worker independently checks all five retained
non-NVS pins and the exact saved current NVS, then checks the marker and rereads
NVS immediately before one reset. Uncertainty remains single-use; no automatic
retry or historical journal reconciliation was added. Complete prior freshness
avoids a redundant snapshot sweep, and equal NVS creates no duplicate backup.

Reporting distinguishes cleanup with NVS drift, unresolved mismatch and
unverified settings. It no longer labels every retained request as verified.
Snapshot/override requests outside capture-only cleanup are rejected before
device access. The pending marker parser is a limited release check, not proof
that every setting is semantically correct.

## Tests and review

The four established host scripts ran in separate Python processes:

| Script under `tests/host` | Result |
|---|---:|
| `enrollment_candidate_rom_adapter_tests.py` | 40 passed |
| `enrollment_candidate_original_capture_tests.py` | 32 passed |
| `enrollment_candidate_budget_adoption_tests.py` | 44 passed |
| `enrollment_candidate_capture_runner_tests.py` | 21 passed |

All 137 cases passed with simulated SDK/transport. Coverage includes stable
NVS-only drift, five immutable mismatches, malformed/pending/unstable settings,
persistence/readback failure, identity/custody/deadline changes, tamper/replay,
pre-terminal misuse, post-snapshot changes and one-attempt reset uncertainty.
Existing candidate, restoration and historical uncertain-recovery guards remain
covered. The command form was `C:\Python314\python.exe -B <script> -f`.

An earlier aggregate run was not acceptance evidence: adapter module reloads
invalidated class identities in later suites, and a real new ordering defect
checked idle custody after acquiring the command lock. The latter was corrected
without weakening lease checks; the isolated ambiguity regression and final
four-suite matrix passed. The failed aggregate log is preserved.

Independent review checked the incremental changes against pre-task snapshots,
the exact final file hashes, source/worker guards and test coverage. Review
evidence, file pins, incremental patch and all test logs are private under
`C:\lu\OpenTrail\.private\ot177-publication\.private\ot0238c-capture-mismatch-20261011`.

## Remaining gates

The sealed runtime and familiar launcher remain unchanged; this is source
correction, not a reviewed successor package or device readiness. The historical
failed session remains held with uncertain reset intent and its active marker.
Owner-confirmed normal screens are separately retained. Its automated custody
requires independently reviewed reconciliation; it was not cleared by this fix.
The original mismatch still needs discriminating evidence before any baseline
decision or new candidate execution. No device operation, firmware build, Git
publication, V1 credit or public website capability change occurred during this
host correction. Full OT-0238c remains In Progress.
