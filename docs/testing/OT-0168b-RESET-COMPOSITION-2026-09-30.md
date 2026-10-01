# OT-0168b reset-domain and receipt composition

Canonical C:/lu/OpenTrail; active worktree C:/lu/OpenTrail/.private/ot177-publication.
Hosted approved revision 1/start receipt: reset-task-started.json. Root owns final
matrix, target build applicability, canonical records and checklist submission.

## Evidence and boundaries

The new factory_reset_target_composition_tests.cpp compiles the real Heltec
marker/user/bond ports, real DeviceFactoryResetExecutor and real protected-app
reset authority. Only ESP-IDF NVS/partition and NimBLE I/O is simulated. The
maintained enrollment_identity_nvs_tests.cpp fixture-only hook is reused without
modification. Physical-trigger coverage here begins at the executor's zero-receipt
entry seam; it does not execute physical input or the complete live BLE runtime.

Focused commands (all exit 0):

- python tests/host/factory_reset_target_composition_tests.py: 26 groups.
- python tests/host/enrollment_identity_nvs_tests.py: 14 groups.
- python tests/host/heltec_v4_factory_reset_storage_tests.py: storage admission.
- python tests/host/heltec_v4_bench_target_tests.py: 19 groups.

Initial new-harness run failed because its SDK mock propagated the bond-iterator
callback stop result. Pinned ESP-IDF v6.0.2 NimBLE ble_store.c:683-747 consumes
nonzero callback as successful early termination. Correcting the mock to match
that implementation made the suite pass; no firmware defect was inferred.

## Transition review

| Before/owner | Trigger and actual implementation | Required effect/evidence | Forbidden effect | Failure/recovery |
|---|---|---|---|---|
| Owned, current protected runtime | App prepare/commit; companion_factory_reset_authority.cpp | Preparation does not write; commit passes nonzero receipt to same executor | Receipt or ADMITTED grants identity/new access | Abandoned preparation preserves every seeded byte; uncertain commit contains |
| Owned, physical input owner | app_main.cpp:405 -> companion_nimble_runtime.cpp:1695 | Real runtime contains old stack, serializes and invokes shared executor with zero receipt | Destruction at prompt/cancellation | Existing gesture/target tests plus source mapping; physical input not newly executed |
| Before durable intent, marker port | commit_intent_and_readback | Single exact marker record, commit and readback before erasure | Cleanup on ambiguous intent | Both applied/not-applied SDK commit failures deny access; reconstructed durable intent resumes |
| Intent verified, user port | erase_all_and_verify_absent | Owner/name/region/identity and whole ot_state erased, reread; identity before counter-bearing state | Successful erase status substitutes for absence | Erase/commit/raw erase/read/false-success failures remain cleanup-required |
| User absent, bond port | erase_all_and_verify_empty | Exact installed store callbacks; all peer-associated classes empty; marker survives | Local IRK mistaken for peer bond or empty assertion without iteration | Clear/iteration/residue failure retains cleanup; reconstructed executor resumes |
| All absent, marker executor | complete_cleanup_and_readback | Commit/readback pending receipt or marker absence before reboot permission | Completion before all domains verified | Completion commit uncertainty denied in-process; reboot rechecks durable state |
| Reboot with pending receipt | restore -> consume_completion_receipt | Recheck user+bonds; clear/commit/readback marker before RAM-only receipt | Receipt grants authorization or bypasses residue | Residue re-enters cleanup; uncertain receipt consume denies in-process; fresh pairing remains separate |

Runtime wiring: companion_nimble_runtime.cpp:1402 constructs the executor and
injects that same instance into app authority; its physical entry invokes that
instance. configure_secure_connections_bonding reads reset marker before owner
restore, retries committed cleanup, and publishes RAM receipt only after exact
consumption. This is source evidence plus focused real-port tests, not a compiled
full-runtime behavioral claim.

## Persistence inventory

Ordinary bench default NVS: ot_v1_owner, ot_name_v1, ot_region_v1, dormant
enrollment identity namespace and NimBLE peer/bond classes. ot_reset_v1 is the
isolated intent/receipt record, consumed after cleanup. Whole exact ot_state
partition covers existing application state/map selectors; active map packages
are explicitly disabled by a compile-time admission constant. Calibration and
other non-user factory namespaces are preserved.

The isolated enrollment candidate uses distinct ot238_nvs/ot238_eval storage,
not present in ordinary bench partitions.csv. Its accepted host candidate reset
inventory covers fixed generation capacity including orphan slots. Its target
intentionally commits intent and stops; pending reset refuses startup. Those
custody boundaries remain untouched and do not become production reset claims.
Protected ot_auth/ot_owner is a future encrypted-context path and has no partition
in the ordinary supported layout; selection/binding remains separately gated.

A demonstrated gap existed: the optional confirmation evaluation writes seven
known ot216_* default-NVS namespaces (confirmation_nvs_backend.hpp:17-24).
Ordinary firmware used that same partition but did not inspect/erase these
records. The retained-record negative control failed against original source
(disk[name].empty assertion after allegedly complete cleanup). The narrow source
correction includes all seven in absence and cleanup after identity retirement,
with boot-generation retirement last among these namespaces. Absent namespaces
are inspected read-only and skipped rather than allocated. Each namespace now
has erase-failure/reconstruction, receipt-residue and marker-absent-residue cases;
ordinary devices with no such namespaces allocate none. Reset marker and factory
calibration survive until their own defined transition. This does not enable the
optional profile or select a cryptographic suite. Independent source review found zero unresolved implementation blockers.


## Final batch gate

The complete current-source matrix passed all 73 suites. Fresh A/B builds of
ordinary bench, optional confirmation bench and isolated enrollment candidate
passed; all nine raw artifact pairs per configuration match. Current firmware
inputs, actually consumed SDK/toolchain inputs, unchanged configuration and
partitions are recorded in the private batch build audit. ESP-IDF v6.0.2,
esp-15.2.0_20251204, Python 3.14.6, CMake 4.0.3 and Ninja 1.12.1 were reused.

Required computer-only implementation is complete and submitted for owner
review. OT-0168c retains both physical reset paths and interruption acceptance;
no device was touched. No V1 credit or public website status changed.

Porting preflight: exact ESP32-S3/16MB target sources, unchanged configurations
and partition tables are pinned in build receipts; no current board identity or
RF plan was inferred. Source-linkage and actual compiler inputs were audited.
Nine raw artifact pairs per profile prove the current build boundary. Reset
reconstruction and uncertain persistence were host-tested. Existing serialization
and callback ownership remain unchanged; USB/serial startup, stack, display,
radio, power interruption and live physical inputs were not changed or executed.
The complete affected matrix ran once at the final source freeze. Hardware gates
are separate because this batch grants no device operation.

Private evidence directory: `.private/firmware-batch-20260930`; reset-result.json,
independent-review.md, matrix-final/result.json and reset-builds/build-result.json
contain exact commands, inputs, source pins, failures and results.
