# OT-169 through OT-177 V1 UX host-foundation evidence

- Date: 2026-09-04 through 2026-09-05 EDT
- Classification: computer-only Android architecture and host tests
- Hardware, Bluetooth, radio, firmware write, app install, and physical UI: not run
- Publication: not performed
- V1 completion: unchanged at exact 43.75%, displayed 44%

## Accepted scope

The owner accepted Decision 0104 as the final V1 profile: one group per person,
exactly one administrator, six total members, group location ON at join, and an
administrator-selected Required or Optional rule. The approved group-admin
visual is design direction, not rendered-application evidence.

## Implemented host foundations

- OT-170: bounded setup label, device/public names, default-on public visibility,
  fixed onboarding stages with no group step, and matching session-bound
  authorization and region-readback receipts before radio transmission.
- OT-172: Messages-first navigation state, portrait bottom navigation,
  landscape navigation rail, draft preservation, and a compiling reusable
  Compose shell. It is not connected to the current activity entry point.
- OT-173: exactly one administrator, six-member maximum, location ON on join,
  actor-bound self location changes, explicit re-consent before changing an
  opted-out group to Required, one-group registry, and confirmed
  delete-before-administrator-leave behavior.
- OT-174: generated direct-contact request, accept/decline/block transitions,
  no typed-chat admission before acceptance, and per-chat location OFF.
- OT-177: deterministic user-reviewed `text/plain` report schema and a typed
  512-record diagnostic ring. Ordinary diagnostics have no arbitrary text
  field; tester notes require a separately enabled channel. Diagnostic code is
  compiled only into the distinct `io.github.nbjelanovic.otclient.v1test`
  application with visible `Trail V1-Test` label and `-v1test` version suffix.

Opaque token objects copy their input and redact string rendering. These models
are isolated from the accepted BLE runtime and do not change OT-168 behavior.

## Independent correction pass

The read-only review found and the implementation corrected three material
issues before this evidence was admitted:

1. Optional-to-Required no longer silently restarts an opted-out member's
   location. The member must re-enable it first or leave.
2. Caller-provided setup trust booleans were replaced by label/session-bound
   receipts; radio transmission becomes eligible after verified region
   readback and does not depend on the later public-profile step.
3. Ordinary V1-Test diagnostic records are typed and bounded. User-entered
   tester notes are a separate opt-in channel, and time/sequence rollback or
   overflow fails closed.

## Validation

The complete affected Gradle matrix passed:

```text
:protocol:test
:app:testDebugUnitTest
:app:testReleaseUnitTest
:app:testV1TestUnitTest
:app:lintDebug
:app:lintRelease
:app:lintV1Test
:app:assembleDebug
:app:assembleDebugAndroidTest
:app:assembleRelease
:app:assembleV1Test

BUILD SUCCESSFUL in 1m 41s
184 actionable tasks: 43 executed, 141 up-to-date
```

Parsed JUnit evidence:

| Suite | Tests | Failures | Errors | Skipped |
| --- | ---: | ---: | ---: | ---: |
| Protocol | 30 | 0 | 0 | 0 |
| Android debug | 216 | 0 | 0 | 0 |
| Android release | 215 | 0 | 0 | 0 |
| Android V1-Test | 221 | 0 | 0 | 0 |
| Total executions | 682 | 0 | 0 | 0 |

The existing debug-only automatic-termination policy test accounts for the
one-test debug/release difference.

Generated artifacts were inspected only as local build outputs and were not
installed:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `app-debug.apk` | 12,592,948 | `FD9BA188E8192A5334494E61B65E7EB4B0574A7D6AB3E1F92A064C3F7972DDAB` |
| `app-release-unsigned.apk` | 8,557,744 | `71D9269C636A6A000645484D4D2F2404C4EE1BC7A05612BA304C0773AFC48041` |
| `app-v1Test.apk` | 11,912,278 | `6014564EF657C87A13EA0B1FCC2EBA385D033FFDE78CBD373058F69433131194` |

`aapt2` confirms production application ID/version
`io.github.nbjelanovic.otclient` / `1.0.0` and test application ID/version
`io.github.nbjelanovic.otclient.v1test` / `1.0.0-v1test`; the test label is
`Trail V1-Test`. Complete DEX header inspection finds zero
`V1TestDiagnosticRingBuffer` matches in release and 19 descriptor/member
matches in V1-Test.

Additional gates:

- publication-safety tracked/untracked scan: PASS;
- V1/V1.5 scope admission: PASS, 16 scenario groups;
- canonical V1 weights: 100;
- recomputed exact V1 completion: 43.75;
- `git diff --check`: no patch errors (line-ending warnings only).

## Remaining gates

None of this proves the production user workflow. Setup persistence and
rollback, Compose/activity integration, invitations and joining-window
admission, authenticated presence and radio wire, typed/quick message packet
budget, coordinate policy/wire, support Save/Share, durable V1-Test storage and
Clear UI, OLED firmware, two-pair physical behavior, signing, and release
acceptance remain open.
