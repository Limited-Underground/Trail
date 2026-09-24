# OT-0238b dormant identity NVS and reset integration

Date: 2026-09-23

Status: host/build-validated dormant adapter; full product integration remains open.

## Bounded result

The Heltec V4 bench target now compiles a dormant application-protected identity
storage adapter for the existing `ot_identity_v1` namespace and includes that
exact namespace in its all-domain factory reset. No startup constructs the
identity adapter or provisions a key; no enrollment request route, display lease,
radio workflow, selected cryptography or product capability is activated.

The accepted [product enrollment design](../security/PRODUCT_ENROLLMENT_REKEY_DRAFT_V1.md)
requires isolated durable identity and complete reset coverage. The prior
[boundary reconciliation](OT-0238b-TARGET-INTEGRATION-BOUNDARY-2026-09-23.md)
identified their missing target binding. This increment closes the adapter and
reset-inventory portion only. Decision 0033's physical-access exclusion remains;
ordinary application-protected NVS is not sealed or hostile-rollback-resistant.

ACTIVE_PROJECT_ROOT: `C:/lu/OpenTrail`.
ACTIVE_WORKTREE_ROOT: `C:/lu/OpenTrail/.private/ot177-publication`.
Starting source: `fb1d558d4791b24efdf2a4a95308e6981dc109ca`.
The coordinating agent verified the live OT-0238b revision 1 approval and
In Progress status immediately before delegation.

## Adapter and exact cleanup

- The lightweight identity storage contract owns the existing namespace and
  `record_v1` key without importing cryptographic code into the ordinary target.
  The owner still uses its existing versioned 64-byte identity encoding. One
  exact blob backs secret-material slot 0; the other nine logical slots have no
  backing keys and read as erased only after the namespace inventory passes.
- Missing namespace/record is distinct from SDK read failure, wrong type,
  truncated/oversized record, unknown key or iterator failure. Failure does not
  create, erase, repair or provision data, and rejected reads leave output intact.
- Writes stage a whole candidate record in private RAM. Sync requires the
  original durable preimage, no reset-namespace residue and a current adapter
  generation, then writes, commits and independently reads back exact bytes.
  Failures from possible mutation onward poison the instance with no erase or
  retry fallback. Scratch and staged bytes are explicitly zeroed when retired.
- Normal adapter access exposes no erase route. Actual factory reset invalidates
  every previous adapter before the first user-domain erase, erases every key in
  `ot_identity_v1`, commits and verifies absence through a fresh handle. Partial
  failure leaves cleanup incomplete and a later reset attempt can finish.
  Reset intent and unrelated calibration namespaces remain outside that erase.
- A retained adapter cannot read or commit its staged old seed after reset even
  when the reset record later disappears. A fresh adapter can observe verified
  empty storage after cleanup. The reset marker remains owned by the existing
  coordinator; identity absence alone never authorizes normal startup or pairing.

All operations require one serialized runtime owner shared with reset. The
adapter's reset generation is a same-process stale-instance fence, not an
independent persistent rollback floor or a concurrency lock. Future runtime
composition must retire the identity owner and all volatile key copies before
reset publishes completion; this dormant increment does not establish that
whole-runtime handoff. The existing identity owner rechecks storage before
signing, but complete live owner/reset/signing composition is not claimed here.

## Preflight and lifecycle review

The [mandatory firmware preflight](../firmware-porting-lessons.md) was applied:

| Gate | Applied boundary / evidence |
|---|---|
| Exact target | Existing Heltec V4 bench ESP32-S3 target, pinned ESP-IDF 6.0.2; standard and opt-in confirmation profiles both affected by shared reset source. Existing 16 MiB flash/PSRAM, board inputs, region rules, partitions and application offset remain unchanged. No new compatibility claim. |
| Source/build identity | New adapter source explicitly enters main CMake; exact target inventory and source-count admission updated by one source. Build outputs and reproducibility are recorded below. Unchanged tracked line bytes preserved; new lines in raw-authoritative files follow their byte policy. |
| Boot/USB/serial | No startup, transport, reset-line or console change. Hardware readiness and serial recovery not exercised because no device operation is authorized. |
| Ownership/concurrency | Dormant adapter; one serialized owner required. Reset invalidation, stale staged writes, reset residue and fresh preimage/readback exercised at actual adapter/reset-port boundaries. Cross-task target runtime retirement remains unwired. |
| Persistence/cleanup | Actual target adapter and reset source compiled into a behavioral test; SDK seams simulate old/new durable outcomes, short/corrupt data, unknown keys, erase/commit failures and interrupted cleanup. No normal autoerase. |
| Composed validation | Focused behavioral and target-static checks plus current security source matrix and both target-profile builds. Static admission supplements actual port behavior. No physical power-interruption or target timing acceptance. |
| Hardware | Not applicable: no flash, reset, port enumeration, radio, phone, battery/case or eFuse action. Existing recovery images and historical grants unchanged. |

| Transition | Success effect | Failure / forbidden effect |
|---|---|---|
| Empty namespace to staged record | RAM-only pending bytes; no provisioning side effect from constructor/read | SDK failure is not emptiness; unknown keys never become a fresh identity. |
| Staged to durable | Reset absent, current generation, exact preimage; set/commit/exact readback | Changed preimage, reset or any uncertain mutation poisons owner; no retry/erase recovery. |
| Active/staged adapter to reset | All old instances invalidated, exact identity namespace erased with other user domains | No old staged seed can be written back; commit/erase failure cannot report verified absence. |
| Interrupted cleanup to retry | Existing reset owner resumes cleanup and verifies all domains | No partial cleanup or identity-only absence permits pairing/traffic. |

## Validation and limits

The focused test compiles actual `enrollment_identity_nvs_storage.cpp` and
`heltec_v4_factory_reset_storage.cpp`; only NVS, partition and NimBLE SDK calls
are simulated. It covers 14 groups, including both durable outcomes after
reported set/commit failure, wrong length/type/extra key, short read, changed
preimage, reset residue, rejected writes/erase, exact reset scope, stale-instance
rejection and failed cleanup followed by retry. It does not model physical NVS
wear, SDK power-loss behavior, malicious firmware or simultaneous target tasks.

- `python tests/host/enrollment_identity_nvs_tests.py`: PASS, 14 groups.
- `python tests/host/heltec_v4_factory_reset_storage_tests.py`: PASS.
- `python tests/host/heltec_v4_bench_target_tests.py`: PASS, 17 groups.
- The focused runner is registered in `tools/Test-Host.ps1`; no duplicate full
  legacy host rerun was requested for this bounded increment.

- `python -X utf8 -B tests/host/security_current_source_ci.py --output-root
  C:/lu/OpenTrail/.private/ot177-publication/.private/ot0238b-identity/security-matrix-final`:
  PASS, all 56 current-source suites with unchanged run source pins.
- `python tools/check_repository_docs.py` and
  `python tests/host/repository_docs_tests.py`: PASS, including 18 regressions.
- `git diff --check`: PASS for tracked changes; final staging verification is
  owned by the coordinating publication task.

The first full behavioral run passed its 56 suites but correctly rejected its
source-pin gate after an inserted-line EOL correction. That failed report is
preserved in `security-matrix/result.json`; it is not used as the complete pass.
The later `security-matrix-final/result.json` is the immutable successful run.

Both affected profiles were built twice from initially absent directories with
ESP-IDF 6.0.2, the installed Xtensa esp-15.2.0_20251204 toolchain, CMake 4.0.3,
Ninja 1.12.1, component downloads disabled and compiler cache disabled. Each pair
matches in all eight raw artifacts: application BIN, ELF, map, bootloader BIN,
partition table, OTA initialization data, sdkconfig and sdkconfig.json. The
standard project version is `ot0238b-identity-v1`; the opt-in confirmation profile
retains `ot216-ble-confirmation-v1`. This candidate is not a flash grant.

| Profile | Application bytes | Application SHA-256 |
|---|---:|---|
| Standard | 589680 | `17b1544218c0ffd1ce0ccb73b14d5d292849477156c6f074e53e8438c1098e24` |
| Confirmation evaluation | 731056 | `40e52701148256f1a4e5bd468844fd8b1159bad0ad5e77dd6cc6fbce8f44b748` |

Compile commands and Ninja dependencies establish both actual adapter/reset
source units and the shared lightweight contract in each profile. The linked
map establishes reset invalidation, including target 64-bit atomic support.
No compiler warnings were found. Checked generated configuration retains
ESP32-S3/16 MiB, Secure Connections, NimBLE security and bond persistence,
reproducible build and application offset `0x10000`; BLE host stacks remain
4096 bytes standard and 8192 bytes confirmation. Dormant identity methods being
compiled does not mean they are activated in startup or retained as live code.

### Final six-line EOL-only correction

After that successful matrix and the four clean builds, the new lightweight
storage-contract header alone was normalized from six CRLF endings to LF for
its raw-text Git policy. Its before/after SHA-256 values are
`fc7964f862ec62869f22d13093a078b09815c40271ac11674ceecfa87cc15301` and
`fb940098fed703901e68de028609d681eca1b1d22b3795f497489330eed09223`.
Normalized text and the entire identity-owner test's preprocessed output were
byte-identical (preprocessed SHA-256
`cc92f180cec160bccae336e365a236bb9aa5f349265e22b42eec6b82fd9e3b55`).
On the final bytes, rebuilding the identity-owner test passed 895 groups and
recompiling actual target storage/reset tests passed 14 groups. Both A profile
incremental builds passed and every one of their eight artifacts still matched
the retained clean A/B hashes above. The successful full-matrix report retains
its original pre-normalization source pins; it is reused as unchanged behavioral
evidence with this explicit byte-only equivalence proof, not misrepresented as
a new 56-suite run over the final header hash.

Private retained evidence lives under `.private/ot0238b-identity/`:
`build-result.json`, `normalization-result.json`,
`normalization-build-result.json`, per-profile logs/results and the two matrix
reports. Independent source review found no blocking issue within the stated
dormant, serialized boundary.

Initial target build attempts stopped before compilation: missing explicit
ESP32-S3 selection was corrected, then the sandboxed compiler wrapper failed to
resolve its own path (Windows error 5), yielding a misleading CMake compiler-not-
found message. Scoped host-build escalation was approved; failed attempt logs
remain under `.private/ot0238b-identity/`. This was an execution-environment and
build-invocation failure, not firmware test evidence. No global toolchain or Git
configuration was changed; the successful invocation uses only process-local
paths and the existing SDK/compiler.

The adapter currently verifies namespace inventory and reads the 64-byte record
for each logical slot check. This deliberate conservative implementation has
not been measured on hardware; no whole-flow latency, read-cost reduction or
resource-efficiency claim follows. Live enrollment storage composition, other
product namespace mappings, display/input arbitration, request routing, complete
reset retirement, target timing and separately authorized physical acceptance
remain open. No V1 credit or public website status change is earned.
