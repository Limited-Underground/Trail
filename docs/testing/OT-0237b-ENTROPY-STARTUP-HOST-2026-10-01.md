# OT-0237b exact candidate startup and entropy admission

Host validation only, 2026-10-01. Live task revision 2 was Approved before start;
the owner has not accepted completion. No device access or firmware installation.
Physical entropy quality, target restart and cold-power/brownout evidence remain
open. This report does not select production cryptography or complete enrollment.

## Source-bound behavior

The new suite compiles the real
`firmware/targets/heltec_v4_enrollment_candidate_eval/main/app_main.cpp`, candidate
controller/store/runtime, identity/counter owners and guarded random adapter.
Every startup case runs in a new subprocess, preventing a previous case's static
ready state from qualifying another case. No product source or target configuration
was changed. SDK, USB, GPIO, OLED, clock and scheduler behavior is modeled;
`sodium_init` has a controlled success/failure return seam, not real initializer
execution. The scalar host crypto source closure is not the complete target library.

The twenty cases cover healthy and fragmented startup, USB/layout/NVS/reset-marker/
controller/library/display/input/storage faults, unqualified random input, entropy
loss during startup and before or inside BEGIN, zero context, public export after
qualification loss, guard retirement and a reconstructed owner. Assertions follow
the actual producer and retained records, rather than only return codes.

| Transition | Required and observed host outcome | Unobserved boundary |
|---|---|---|
| New process starts candidate | Actual entrypoint establishes owners; first startup failure remains queryable through bounded fragmented USB input. | Physical USB/reset, scheduling and display timing. |
| Random input cannot qualify | No secret identity or session/generation records are produced. | Hardware RNG quality or statistical health. |
| Source disappears before BEGIN or its request fill | Attempt refuses without secret creation or committed identity/generation. | Real peripheral source-loss behavior. |
| Source disappears during seed fill | Only the exact nonsecret OTID intent may remain; no seed, digest, DONE or generation commit. Reconstruction refuses without new writes. | Power interruption during physical NVS operations. |
| Source qualification is lost after startup | Existing public HELLO/READY/EXPORT still works without requesting new entropy; these replies are not authority for creating a private key. | No semantic change to those public commands was authorized. |
| Old owner is stopped/retired | Its guard cannot rearm; a newly reconstructed owner must qualify its own source. | Actual target restart/cold boot. |

The existing fail-closed secret-creation behavior passed these host cases; no
product correction was needed. Public READY remaining visible after qualification
loss is an observed limitation, not proof of continuing entropy qualification.

## Validation and build boundaries

Focused suite: **20/20 cases pass**. Independent implementation and CI-integration
reviews found no unresolved blockers. The new five-file fixture is isolated from
the existing candidate fixture, and the existing suite's source composition is
preserved. Bounded scheduler bailout occurs only after the owners return; no live
owner destructor is skipped. The scheduler seam also models the OLED startup delay.

One final complete current-source security matrix passes **74/74 suites**, including
the new actual-entrypoint suite. The runner freezes and rechecks its source input closure. Independent review of
all 24 retained entropy-suite depfiles found 142 repository dependencies, each
covered by those unchanged pins, plus 66 generated/admitted corpus inputs.
`final-security-matrix/result.json` records output and binary/source pins; exact
commands are in the separate `final-security-matrix/commands.json`.
The earlier focused builder's after-compilation pins alone are not claimed as a
pre-compilation freeze; the final matrix closes that gap.

Command from the selected checkout, using existing Python/MSYS2:

```powershell
$env:OPENTRAIL_MSYS2_ROOT='C:\msys64'
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH
& C:\Python314\python.exe -X utf8 -B tests\host\security_current_source_ci.py --output-root C:\lu\OpenTrail\.private\ot177-publication\.private\v1-delivery-batch-20261001\final-security-matrix
```

The existing firmware-build reuse gate correctly refused reuse: two pre-existing
accepted settings inputs differ from the September 30 build's dependency pins
(`companion_device_name_codec.hpp` and `heltec_v4_factory_reset_storage.cpp`).
Old A/B images remain historical evidence. One fresh current-source build passed
under the existing ESP-IDF 6.0.2, Xtensa 15.2.0_20251204, CMake 4.0.3, Ninja 1.12.1
and Python 3.14.6 profile, with component-manager/ccache disabled and one Ninja
`-j 4` build. The compile took 297.17 seconds and reported no warnings. Exact
launch arguments and tool versions are in the private build wrapper/run/audit
receipts; eight raw artifacts, the embedded v2 description/config/layout and
76 linked candidate symbols were checked. All 375 actual repository dependencies
and 2,489 consumed installed dependencies passed the post-build audit, including
the two changed settings inputs. Known installed inputs were checked before
launch; the complete actual graph was resolved and checked afterward.

Current image:
`build/ot0237b-enrollment-current-20261001-hostaccess/ot238c_enrollment_eval.bin`,
637,808 bytes, SHA-256
`ee58e250b63b4ded87a688bc88223dc9e450ec827df5b47980219c56acd42f42`.
Its ELF is 9,137,108 bytes, SHA-256
`cc00a133a582a4cf32c420ff36e2dbf5b393392a4259162dc818fd2043320b77`.
The eight-artifact manifest is
`ot0237b-current-target-build-hostaccess/hostaccess-audit.json`, SHA-256
`4b0ad30d41e517a6975d388b8ed71cc374fc7f75d4ca3a164203354efa8c2cb2`.
One current compile establishes build evidence; no new reproducibility claim.
It grants no installation authority. A later device plan must bind this new
image and current restoration inputs rather than reuse an old operational grant.

## Retained failures and remaining gates

The focused suite initially used an incorrect test API, linked an initializer
outside the scalar source closure, and expected deinit before controller start.
Those test/harness failures are retained and were corrected; they were not target
runtime failures. The first fresh target launch stopped during CMake's tool
admission because the installed Xtensa dispatcher could not resolve executable
access in the sandbox. Its failed log is retained as an execution-environment
failure; the same installed profile is used for the host-access continuation.

Mandatory firmware-porting preflight records distinguish applicable host checks
from skipped physical gates. Source tests do not establish physical random quality,
fresh target startup, restart, cold-power/brownout, interrupted persistence, secret
retirement on hardware, full security admission or release readiness. These remain
under OT-0237b/c/d, OT-0238c and the separately authorized physical plan. Existing
case-opening/battery-disconnection deferral remains in force.

Changed maintained files: the new entropy test and its four fixture files,
`tests/host/security_current_source_ci.py`, and owning documentation. Full pins,
commands, initial failures and build-admission conflict are retained privately in
`C:/lu/OpenTrail/.private/ot177-publication/.private/v1-delivery-batch-20261001/`:
`OT-0237b-integration.json`, `OT-0237b-host-preflight.json`,
`OT-0237b-current-target-build-preflight.json`, `build-reuse-input-conflict.json`,
`ot0237b-focused/result.json`, and `final-security-matrix/result.json`.

Local/uncommitted; no Git network operation, publication or website deployment.
Accepted V1 progress and public website capability status did not change.
