# OT-163 actual-source console probe

## Evidence boundary

The host probe compiles the hash-pinned ESP-IDF simple-stdio console functions
used by the accepted benchmark configuration. It injects ROM-output successes
and failures below the actual `_write_r_console` and
`esp_system_console_put_char` source bodies. Valid writes report the requested
length even when the injected ROM output fails. This establishes an error-reporting
boundary under simulation; it does not establish the physical cause of the
[attempt-4 timeout](OT-163-OBSERVATION-RUN-2026-09-09.md).

Source presence is insufficient for this claim. Self-contained admission checks
exact upstream source bytes, selected candidate configuration lines and selected
map sections. These positively establish live simple-stdio functions and discarded
`_fsync_console`; they do not prove absence of every VFS symbol. A matching symbol
in discarded map text cannot qualify it as a live implementation.

Independent full-artifact verification checks the original map/configuration
hashes and exact selected line ranges. The complete accepted map confirms the
VFS console path is not linked. That full-map finding is distinct from what the
smaller portable fixture alone proves. The recorded configuration uses picolibc,
USB Serial/JTAG console and ROM serial port 4.

## Reproducible validation

Twelve focused unittest groups pass, including seven compiled behavior cases:
successful stdout/stderr; all-output failure on both; selective failures; CRLF;
newline failure; zero-length writes on both descriptors; and invalid descriptors,
including zero-length invalid writes. Compilation uses
`-Wall -Wextra -Werror -DNDEBUG`.

From the repository root with native `g++` available on PATH:

```powershell
python -B tests/host/noise_xk_console_source_probe_tests.py -v
```

The [probe](../../tools/noise_xk_console_source_probe.py) retains the actual
upstream function bodies; the [tests](../../tests/host/noise_xk_console_source_probe_tests.py)
also reject source changes/normalization, changed provenance, map/license
changes, missing/duplicate/malformed anchors, discarded-as-live symbols, wrong
configuration/object selection and wrong full artifacts. The
[provenance manifest](../../tests/host/fixtures/noise_xk_console_source/provenance.json)
pins the 574-byte and 2,906-byte source fixtures, including their original CRLF,
and the ROM header contract declaring zero for success and one for failure.
The full header digest is retained without redistributing the entire header.
Local `verify_full_artifacts` passed against the exact sdkconfig/map digests and
numbered excerpts. The complete local host matrix also passed through
`tools/Test-Host.ps1`, including the new suite and simulator checks. Historical
loader checks remain OpenTrail compatibility evidence only.
The probe is host-only. It neither changes target firmware nor sends serial,
flash, reset or radio commands. Previously consumed grants remain consumed.

The reported write count does not independently prove physical emission or USB
delivery. ROM failure injection is a controlled host scenario, not a captured
device failure. The two receipt logs are adjacent in the responder prepare path;
no separate arm-rx command exists and no intervening radio operation was found.

## Acceptance boundary

Evaluate a bounded console-delivery correction with an actual-source regression
before considering any hardware attempt. A correction must retain explicit
failure handling and preserve the existing parser, receipt deadline, recovery
and one-use execution boundaries. This probe implements no firmware correction
and grants no hardware or publication authority.

No complete benchmark, physical root cause, product messaging or milestone
completion is added. Detailed weighted progress remains in
[V1_PROGRESS.json](../V1_PROGRESS.json); milestone weights and completions are
unchanged. Accepted evidence does not change public website status.
