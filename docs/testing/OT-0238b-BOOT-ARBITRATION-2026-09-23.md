# OT-0238b target BOOT and reset arbitration

Date: 2026-09-23

Status: implemented and host-validated; both affected firmware profiles built
reproducibly. Product enrollment routing and physical acceptance remain pending.

## Bounded result

The actual Heltec bench application now routes normal and contained BOOT/reset
polling through one serialized input arbiter. It composes the existing reset
gesture recognizer, GPIO0 sampler and real display owner with the existing
trusted enrollment device-IO contract. Existing reset gesture thresholds and durable
execution semantics are retained; the new cached sampling and stable-release
barrier govern input handoff. Enrollment initiation remains dormant: no production
caller binds a review context or starts an enrollment review.

This follows the [display-lease increment](OT-0238b-DISPLAY-LEASE-2026-09-23.md)
and accepted [review-device contract](OT-0238b-REVIEW-DEVICE-PORT-2026-09-23.md).
The coordinating agent verified live OT-0238b revision 1 approval and In Progress
status before implementation. Canonical project is `C:/lu/OpenTrail`; active
worktree is `C:/lu/OpenTrail/.private/ot177-publication`. Starting source is
`1212b43add9278cbc79961dc483819549514cbd2`.

## Source preflight

The [mandatory firmware preflight](../firmware-porting-lessons.md) and complete
input/render/reset lifecycle review were applied before source changes.

| Gate | Source-backed boundary |
|---|---|
| Exact target | Existing ESP32-S3 Heltec V4 bench, unchanged flash/PSRAM, OTHP0/v1 partitions, 0x10000 application offset, GPIO0 active-low BOOT input and existing OLED wiring. Both standard and confirmation builds are affected by shared source compilation. No hardware-revision claim. |
| Sole sampler | `heltec_v4_factory_reset_input.cpp` now configures and reads GPIO only. `heltec_enrollment_input_arbiter.cpp` owns the original `CompanionFactoryResetGesture`, a checked signed timer observation, exact sample generation, cached button state and display lease. |
| Actual callers | Both normal and contained `app_main.cpp` loops poll this arbiter and capture its current sample generation before cancel/rearm. Normal reset still reboots after its executor result; contained recovery reboots only verified/uncertain intent and rearms only known noncommit. The confirmation profile retains its existing reset exclusion. |
| Review types | `enrollment_review_io.hpp` extracts the existing context/sample/frame/IO types without cryptographic includes. The boot array has exactly the existing InvitationToken type; the role enum's underlying type and values remain unchanged. No security predicate was moved into a lower-trust caller or weakened. |
| Context authority | The binding API requires a future trusted product owner to supply an already-admitted exact context; it grants no peer admission or confirmation. It refuses immediate identical rebinding, but is not a history ledger or comprehensive old-context replay detector. No production binding caller exists. |
| Byte/build closure | New files use LF/no BOM. Unchanged raw-authoritative lines retain their original bytes; inserted lines pass whitespace admission. Conservative quoted-include traversal across 18 targets reaches changed implementation only from the bench target. Final source bytes are pinned before matrix/builds. |
| Timing/ownership | One clock/GPIO observation per service tick; observation consumers do not resample. A successful review draw explicitly starts a new service tick so post-visible debounce never uses pre-render time. More than one service tick may occur in an application loop. Calls are serialized, not thread-safe. |
| Persistence/transport | No storage encoding, namespace, erase implementation, BLE admission, radio, USB/serial, security selection or durable reset executor changed. Original review/reset deadlines are preserved. Hardware transport/recovery gates are not exercised because no device operation is authorized. |

## Complete lifecycle

| Transition | Actual effect | Failure or forbidden effect |
|---|---|---|
| Initialize / service tick | Configure the original input and take one checked time/button sample; run reset recognition before exposing cached observation | Invalid GPIO level, negative/regressing clock or exhausted generation invalidates the observation and review; no stale sample may advance reset. |
| Trusted context / acquire | Require reset idle after its stable-release gate, no active lease and available display; copy exact context | PIN/reset display ownership refuses; context binding alone never confirms a peer. |
| Render / post-frame service | Draw canonical layout through the real driver, then sample new time/button state and process reset first | A button change or ten-second threshold during a slow draw is observed before successful review revision publication. The original review deadline is not extended. |
| Observe | Return the cached exact context, current matching display lease/revision, time/button and reset state | No GPIO or clock reads are hidden in observe; stale display ownership or invalid sampling refuses. |
| Ordinary release / cancellation | Remove the exact overlay and cancel precommit reset; require a NEW service sample followed by the original 40 ms stable release | A prior released sample or time consumed by slow restoration cannot satisfy the next reset gesture. Held input cannot carry into reset. |
| Reset prompt / commit intent | Invalidate review before the reset frame; preserve a post-render event until application poll | Late review cleanup can acknowledge only exact, successfully drawn and still-available reset preemption; it never restores normal pixels over reset. |
| Known noncommit / rearm | Exact current unconsumed sample token and terminal recognizer state; then new sample/stable release | Old, zero, reused or uninitialized tokens cannot cancel/rearm. Generic review cleanup cannot cancel a committed request. |
| Reentry / failure | Defer cleanup to the outer operation, invalidate review and fail closed | External callback reentry latches the arbiter unavailable until restart; it cannot recursively sample/draw or mint confirmation. |

Independent review found a draft cleanup defect: it could acknowledge reset
preemption after the reset draw failed. The final code requires recorded success
and current display availability; actual draw-failure and total-concealment-failure
tests both reject release/handoff. Physical concealment remains unproved when all
hardware paths fail; software never reports successful handoff in that case.

## Validation

Focused compilation uses the existing admitted native dependencies and actual
`EnrollmentFingerprintReview`, `EnrollmentReviewDevicePort`, arbiter, GPIO adapter,
reset recognizer, display owner and OLED driver. Only SDK clock/GPIO/panel I/O is
simulated. The new test is registered in the maintained current-source matrix,
which `tools/Test-Host.ps1` already invokes.

- Actual composed arbiter/review: 17 groups passed. Covers cached observations,
  exact per-service GPIO counts, held entry, slow render, changed button after
  render, reset threshold crossing, reset preemption event delivery, original
  review expiry, old mutation tokens, noncommit recovery, fresh-release barrier,
  PIN exclusion, clock/GPIO failure, render/concealment failure and reentry.
- Existing display owner 18, actual OLED driver 14 and presentation 7 groups
  passed against the updated SDK fixture.
- Existing pure reset recognizer: 10 groups passed, retaining sparse sampling,
  debounce, ten-second hold, inclusive confirmation deadline, cancellation,
  terminal commit and known-noncommit rearm behavior.
- `python tests/host/heltec_v4_bench_target_tests.py`: 17 groups passed; actual
  app routing and confirmation-profile exclusions remain statically admitted.
- `python tests/host/heltec_v4_factory_reset_storage_tests.py`: passed.
- `git diff --check`: passed before source freeze.

The first new focused-test compile caught a mixed-type `auto` test declaration;
it was corrected before execution. Static admission checks were updated to the
new generation-token API and passed before the final gate. No earlier failed
check is presented as a pass. The final current-source matrix passed all 57 suites with its source-pin gate
intact. All 13 owned source-file hashes still match the pre-run freeze.

`python -X utf8 -B tests/host/security_current_source_ci.py --output-root
C:/lu/OpenTrail/.private/ot177-publication/.private/ot0238b-input/security-matrix-final`

This is a new actual-source run, including the existing identity, review,
enrollment, retained-state, transport and target consumers of the extracted
types. Its immutable result and command records are retained privately.
### Target build and reproducibility evidence

Four clean builds passed: standard A/B and confirmation A/B. Each profile's
application binary, ELF, map, bootloader, partition table, initial OTA data,
`sdkconfig` and generated configuration JSON matched byte for byte across its
A/B pair. The unique repository dependency hashes also matched. The audit found
zero compiler warnings, and all 13 source hashes still match the pre-matrix freeze
after all builds. No source normalization or other byte changes followed validation.

| Profile | Application bytes | Application SHA-256 |
|---|---:|---|
| Standard | 592304 | `1b1f57b338a253ae9b4fb5189be4d4c1efe7c9ff2a71c1c3421b2294f7b5aef8` |
| Confirmation | 731392 | `0bda6b7ee3cbd00e4b24c258b22c336b07a6093af9025e0ef22c6622af90ae7a` |

The maintained build path used ESP-IDF 6.0.2, Xtensa
`esp-15.2.0_20251204`, CMake 4.0.3, Ninja 1.12.1 and the IDF Python 3.14
environment. Both profiles explicitly select `esp32s3`, 16 MiB flash, application
offset `0x10000`, reproducible builds, Secure Connections and persisted bonds.
Standard/confirmation NimBLE host stacks remain 4096/8192 bytes. Build versions
are `ot0238b-input-v1` and `ot216-ble-confirmation-v1`, respectively. These are
configuration facts, not measured stack-headroom or hardware claims.

Compile commands verify the actual arbiter, GPIO adapter and application with
each profile's expected definition. ELF symbols confirm the real review renderer
is linked in both profiles and the arbiter polling path is linked only in the
standard profile, preserving confirmation-profile reset exclusion. Repository
include/dependency evidence establishes the extracted IO header's consumer closure.

Private evidence is retained under `.private/ot0238b-input/`: immutable
`final-source-pins.json`, `security-matrix-final/result.json`, per-build logs and
results, deduplicated dependency records, and `build-result.json`. The final
repository documentation checker, its 18 regression tests and whitespace checks
passed after this report was finalized.

## Remaining gates

Product routing must admit the exact current context, retire it on disconnect,
expiry/restart and cancellation, and invoke the existing reviewed enrollment
owners. This adapter is not an admission ledger, trusted-pairing shortcut or
new Boolean confirmation route. Complete enrollment persistence/reset coverage,
product request composition, measured task timing/stack headroom and separately
authorized physical button/display/two-device acceptance remain open.

No hardware action, installation, new product choice, V1 completion credit,
website change or release acceptance is established by this host increment.
