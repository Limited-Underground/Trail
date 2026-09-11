# OT-194 actual-source policy lifecycle diagnosis

Status: host-only lifecycle diagnosis; no new physical trial.

## Scope

The [OT-193 diagnostic trial](OT-193-DIAGNOSTIC-TRIAL-2026-09-11.md)
established transport acceptance of the command and zero received bytes, with
both originals returned. It did not establish firmware command consumption,
completion of evaluation, or the physical cause of the silent result.

This increment exercises the current application orchestration, command parser
and console binding using host-controlled SDK seams. It changes tests and the
host matrix only. The OT-187 firmware target, strict host capture and retained
runtime/package remain unchanged. No device, phone, grant or live snapshot is
operated or created.

## Applicable target preflight

1. Boundary: unchanged Heltec WiFi LoRa32 V4.2 / ESP32-S3 evaluation target;
   host-only source tests. Live ports and physical inventory are not queried.
2. Bytes/builds: preserve the admitted firmware and component bytes. New host
   executables are test artifacts. Two ESP32 builds are skipped because the
   deployable target and its configuration do not change.
3. Boot/transport: exercise source lifecycle and input/output boundaries with
   controlled clocks and SDK seams. Host tests do not establish physical boot,
   USB delivery or a new readiness handshake.
4. Ordering: cover startup, command consumption, evaluation and terminal output,
   including aborts; observe stages in test memory without new protocol output.
5. Persistence: injected SDK storage is isolated from device NVS. Earlier backup
   snapshots remain stale and consumed grants are preserved.
6. Composition: focused actual-source tests and one complete affected host
   matrix form the software gate. No target or physical acceptance is inferred.
7. Hardware: skipped in full; this task has no hardware execution scope.

## Findings and validation

The console binding tests execute the unchanged `policy_console.cpp` in fresh
host processes. Controlled cases cover quarantine failure, output bypass,
fault during actual command reception, unavailable FIFO, zero-byte writes,
fault during writing, deadline overrun, terminal flush failure and reentry.
A terminal flush failure can leave every formatted byte submitted while no
complete receipt is delivered in the fixture. This is a controlled possibility,
not a diagnosis of the physical trial.

The application harness compiles a byte-identical copy of `app_main.cpp`, the
actual command control, NVS policy backend, persistence and policy session, and
the pinned scalar libsodium/Noise implementation. Only external device services
and entropy lifecycle are simulated. Three cases also link the actual console
translation unit: normal success, pre-install console fault and fault during
input. The 22 application lifecycle groups and four strict receipt rejection crosschecks pass, alongside 12 console binding groups.

| Controlled boundary | Required observation |
| --- | --- |
| Installation/session/input refusal | No NVS initialization or entropy start; no receipt |
| Missing, partial, invalid or expired command | Bounded return without evaluation |
| NVS unavailable | One `nvs_unavailable` receipt attempt; no entropy start |
| Entropy/crypto/evaluation failure | Cleanup precedes a terminal result; failed output remains unaccepted |
| Successful actual composition | Real policy evaluation produces one exact challenge-bound receipt |
| Receipt timing/identity/extra output | Strict capture rejects late, stale-challenge, duplicate and trailing output |

Successful host evaluation is not physical entropy or security acceptance.
A delayed entropy-stop seam exercises orchestration timing, not the duration or
interruptibility of any actual SDK call. No SDK runtime bound is inferred.

A separate retained-build audit verified the OT-187 application, link map and
configuration against the [accepted build record](../../tests/benchmarks/crypto/OT-187-SECURITY-POLICY-BUILD-2026-09-10.json).
The selected USB Serial/JTAG configuration and priority-zero quarantine callback
agree with the inspected local SDK startup order. The SDK VFS defaults to a
polling reader; registration/open do not themselves install an interrupt RX
driver. The presence of a driver archive in the map therefore does not establish
that another reader consumed the command. The inspected connection monitor
checks USB frame/connection state and does not drain input. No deterministic
startup/configuration contradiction was found in this bounded audit. Local SDK
text inspection does not independently attest every source byte to the old ELF.

## Validation record

- `python tests/host/security_policy_lifecycle_tests.py`: 22 groups and four
  strict rejection crosschecks pass, including three actual app/console cases.
- `python tests/host/security_policy_console_lifecycle_tests.py`: 12 fresh-process
  console fault groups pass.
- Toolchain: Python 3.14.6 and MSYS2 UCRT64 GCC 16.1.0; C++17, with new application
  and harness code compiled using warnings as errors. Existing upstream signing
  primitives emit unused-static warnings in their separate C compilation.
- All 33 accepted OT-187 target/source pins match their exact sizes and hashes.
- Complete local host matrix before the clean-checkout correction: pass under
  the normal Windows user, including both new suites and 13/13 simulator UI checks.
  Fresh dependency/compiler correction checks pass separately; the final required
  GitHub matrix gates merge. Documentation, privacy and raw-byte gates pass.

The first complete-matrix run stopped in the existing CurrentUser DPAPI
round-trip test: `CryptProtectData` could not create its synthetic temporary key
under the sandbox context (`key_store_unavailable`). The exact unchanged suite
passed all 15 groups under the normal Windows user. This is an execution-context
failure, not evidence of a lifecycle-code regression. The complete matrix
then passed in that context, retaining the earlier failed log and all gates.

The application source is copied byte-for-byte into the temporary test build to
inject device headers; it is not rewritten. Test output uses binary stdout on
Windows, preserving exact receipt bytes for the existing capture parser. At the
exact first-byte deadline the input callback can consume one byte before the
parser refuses; the test preserves that actual ordering and verifies no NVS or
entropy work begins. The delayed-stop case asserts an injected delay beyond the
host horizon and models the resulting receipt as late. It does not share a real
USB/SDK clock or establish a physical timeout cause.

## Clean-checkout dependency correction

The first required GitHub run reached the new lifecycle suite, then failed before
compilation because its retained local managed-component directory was absent.
The initial local pass did not establish a complete clean-checkout setup.
The correction explicitly obtains the pinned official Espressif libsodium
1.0.22 package in a fresh temporary dependency directory and verifies its archive,
checksum inventory and all admitted source files before compilation. It retains
the independent interoperability probe and uses the selected native compiler
rather than a machine-specific compiler path. The old retained cache and frozen
interop helper are not modified. Fresh acquisition and an actual alternate-compiler run pass for this correction;
no lifecycle or physical behavior is changed. The final required GitHub matrix
gates merge.

The package is 2,545,109 bytes, SHA-256
`865ea3aba354b16be4c7051da48b63247a395f30d1e0537269b253918aa78c5f`.
Its separately pinned checksum inventory and tracked managed-source manifest
admit all 731 archive files plus the two managed metadata files before any source
is written or compiled. Twelve dependency-admission tests cover malformed paths,
duplicates/types, archive integrity, size/deadline limits, destination preservation,
endianness and selected compiler routing. The actual fresh run retains the
26-case interoperability probe and passes the 22 application groups and four
receipt rejection crosschecks. The new helper leaves frozen acquisition evidence
and interop sources unchanged. Its 90-second download admission deadline is
checked before and after reads; an in-flight read may overrun before the
45-second socket timeout. This is not hard cancellation at 90 seconds.

## Next gate

Prepare a small additive stage-diagnostic target and bounded observation contract
that distinguish startup/input, evaluation and terminal output during a later
trial. Keep the current candidate and strict capture frozen. Validate the new
source composition and matched builds before proposing fresh physical admission;
more host-only fault cases cannot identify which stage the old physical run
actually reached.

## Acceptance limits

A controlled fault reproducing silence is evidence of a possible source path,
not proof that it caused OT-193. SDK execution timing, physical USB delivery,
entropy quality, interrupted flash persistence and product security acceptance
remain separate gates. No V1 completion increase or website capability change.
