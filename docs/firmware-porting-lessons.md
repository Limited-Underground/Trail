# Firmware target porting lessons and preflight

This checklist is mandatory before implementing or materially changing an
OpenTrail firmware target. Its purpose is to stop known host, build, transport,
state-ordering, and recovery mistakes from being rediscovered on each board.
Apply only the checks relevant to the target and record why any item is not
applicable.

## Evidence boundary

This is a recovered, evidence-backed checklist, not a claim that every past
failure is known. The public Git history begins with a sanitized snapshot on
2026-08-10. The incident table below covers failures that can be tied to an
accepted decision, hardware note, regression test, or the current OT-167
working-tree investigation. It does not promote injected negative-test cases
to physical incidents, infer private serial contents, or assign a root cause
where the retained evidence does not prove one.

Items marked **open** are preflight requirements learned from OT-167 work in
progress. They are not accepted completion evidence and must be reconciled with
the final OT-167 decision and tests when that work closes.

Current V1 authority is Decision 0103 and `DEVICE_FACTORY_RESET_V1`. The open
OT-167 replacement-expiry/cleanup entry below is retained only as investigation
history and a warning about exact-link cleanup; it must not be implemented as a
phone-replacement path. OT-168 owns destructive factory reset, full user-data/
map/bond erasure, and automatic 60-second unowned-boot pairing.

## Mandatory target preflight

### 1. Freeze the real target boundary

- Record the exact board/revision, MCU, flash/PSRAM profile, radio region,
  partition table, application offset, boot and user inputs, display, battery,
  GNSS, radio, UART/USB console, reset behavior, and toolchain commit/version.
- Treat specifications, pinout guesses, and sibling-board behavior as
  candidates until the received target or an authoritative board source is
  checked. Keep board code under `firmware/targets`; keep target-neutral state
  and protocol logic under `firmware/components`.
- List every target source in its build definition and prove that the intended
  source, configuration, partition table, and dependency version enter the
  final ELF. A source file existing in the repository is not build evidence.
- Define what remains unavailable. Never advertise a radio, action, power,
  location, storage, or security capability merely because its protocol enum
  or UI exists.

### 2. Make bytes and builds reproducible

- Pin `.gitattributes` for every hash-authoritative text input. Audit existence,
  length, actual and expected SHA-256, EOL form, BOM, final newline, and
  effective attributes. Never update a canonical digest from an unreviewed
  checkout or generated output.
- Use a full-enough Git checkout when a validator reconstructs historical
  commits. Test Windows `core.autocrlf=true` behavior as well as the canonical
  Git blob.
- Build twice from initially absent build directories with the exact toolchain,
  explicit project version, offline/dependency policy, and cache policy. Compare
  the application BIN, ELF, map, bootloader, partition table, generated config,
  dependency lock, and resource reports that the task treats as authoritative.
- Validate one cross-layer manifest: firmware image, runner, parser, adapter,
  coordinator, restoration image, write offset, authority, and tests must name
  the same lengths and digests. Do not assume independently green layers compose.

### 3. Prove boot, USB, serial, and reset lifecycles

- Decide whether bootloader, ROM, ESP-IDF console, application logging, and the
  control protocol share an endpoint. Either isolate them or give startup data
  an explicit bounded state; do not charge unknown pre-control bytes to a
  post-control frame budget accidentally.
- Use an explicit device/host readiness handshake. Accumulate partial records
  across read timeouts, keep exact deadlines, accept only the exact transition
  marker, and retain strict post-transition parsing.
- Treat reset as a transport boundary. Close the old serial handle, verify the
  expected endpoint lifecycle, reopen a fresh handle with explicit DTR/RTS
  state, and reject stale pre-reset data. A longer timeout does not repair an
  invalid handle.
- Derive timeouts from the full boot and first-response path. Include a test
  where the first valid response occurs at the latest permitted time.

### 4. Model concurrency and event ordering before board wiring

- Write the successful sequence and every abort sequence before implementation:
  connect, security, bond persistence, application authorization, request,
  disconnect, expiry, host reset, reboot, and recovery. Identify the owner of
  each mutable value and the context of every callback.
- Keep NimBLE callbacks bounded. Queue work to the runtime owner; do not perform
  ADC, UART, display, storage migration, or another blocking operation from a
  GATT callback. Publish cross-task telemetry as one coherent fixed-memory
  snapshot rather than reading task-owned structs directly.
- Persist or stage device authority before protected GATT evaluates the same
  encrypted link. Conversely, do not publish a replacement owner before the
  exact record is committed/read back and the old bond is deleted and verified.
- Classify normal asynchronous races explicitly. For NimBLE termination,
  `0`, `BLE_HS_EALREADY`, and `BLE_HS_ENOTCONN` are accepted completion races;
  a genuine termination failure still enters containment. Expected
  `window_expired`, `candidate_cleanup_required`, `connection_in_use`, and stale
  events must not be converted into queue overflow or terminal `BLE:E`.
- Test event permutations, not only functions: callback before queued event,
  disconnect during expiry, reset during cleanup, duplicate completion,
  callback reentry, lock contention, queue overflow, and watchdog expiry.

### 5. Make persistence and cleanup exact

- Separate pre-mutation failure from post-mutation uncertainty. After a durable
  write begins, any ambiguous failure remains uncertain until exact readback or
  reconciliation proves otherwise.
- For single-owner replacement: stage the exact candidate; require a second
  physical confirmation; compare-and-swap and read back the new owner; delete
  only the exact old bond; verify fresh inventory; only then publish the new
  owner and close the window.
- On cancellation, failed bonding, expiry, disconnect, restart, or reboot,
  delete and verify only the exact candidate and retain the old owner. Never
  clear all bonds, remove the oldest bond, or guess which record is orphaned.
- Capture any live connection handle needed for post-cleanup termination before
  clearing volatile replacement context. Storage cleanup alone does not close
  an encrypted link.
- Define generation exhaustion, corrupt/extra inventory, cleanup failure,
  power loss, and callback reentry. Fail closed into an explicit reconciliation
  state when exact authority cannot be reconstructed.

### 6. Validate the composed target before hardware

- Run focused pure state/protocol tests first, then tests that exercise the
  actual composed adapter/coordinator path with real bytes. Mocks may isolate
  physical I/O, but they must not replace the function boundary being proved.
- Add static target tests for source linkage, initialization order, security
  configuration, callback routing, exact error classification, and forbidden
  broad cleanup APIs. Static substring tests supplement but do not replace a
  behavioral composed test.
- Run the complete affected host matrix once at the final gate, then build the
  exact target. Inspect the ELF/map/config and hash the exact writable image.
- Exercise success plus late/partial input, clean-checkout builds, stale
  handles, wrong image binding, helper-name collisions, unsupported library
  error behavior, disconnect/expiry/reset interleavings, and every recovery
  path that can mutate persistent state.
- Confirm that the phone/UI behavior matches implemented authority. A
  successful authorization followed by a denied snapshot or action authority
  is not a complete Ready session.

### 7. Use hardware only after the gate is green

- Verify exact installed application readback before writing. Write only the
  authorized application range unless the task explicitly requires another
  region. Preserve a verified restoration route for every touched node.
- Keep antennas attached for radio work and record the exact regional profile.
  Reset all preflight devices on every exit path, including nodes not benchmark-
  flashed after a later failure.
- Use privacy-safe but diagnostically useful stage codes. Do not retain ports,
  MAC addresses, keys, passkeys, raw private traffic, or exception text; do
  retain enough allowlisted stage information to distinguish preflight,
  endpoint, handshake, parser, radio, restoration, and verification failures.
- After installation, perform the real acceptance sequence, not only boot:
  display concealment/failure behavior, phone pairing, Ready snapshot, action
  boundary, disconnect/reconnect, reboot persistence, replacement/cleanup, and
  two-device/two-phone isolation as applicable.

## Recovered incident-to-prevention map

| Incident | Evidence-backed cause or boundary | Required prevention and preflight | Regression/evidence pointers |
|---|---|---|---|
| OT-073 protected-storage read denial | The sanitized one-use result proved only `DENY-READ-FAILURE`; it retained too little stage information to distinguish the failure and therefore proved neither installed layout nor blank source media. The physical root cause is unknown. | Use fixed privacy-safe stage codes and prove the read-only executor against the real command boundary before consuming a physical attempt. Never authorize a partition transition without exact source layout, complete-region evidence, and a recovery route. | [OT-073 note](../tests/hardware/OT-073-2026-08-17.md), [Decision 0015](decisions/0015-safe-heltec-protected-storage-partition-transition.md), `tests/host/protected_storage_transition_evidence_tests.py` |
| OT-124 and OT-126 Monocypher capture aborts | OT-124 retained no confirmed cause. OT-126 proved a deterministic deadline mismatch: firmware waited at least 3 seconds before its first frame while the runner abandoned a fresh endpoint after at most 2 seconds; preflight also left nodes in download mode and reset only the benchmark-touched node. | Derive first-frame grace from firmware boot, reset every preflight node on every exit, and prove the latest valid first response. Do not respond to a timeout by merely retrying hardware. | [Decision 0063](decisions/0063-monocypher-comparison-execution-abort.md), [Decision 0065](decisions/0065-monocypher-corrective-retry-execution-abort.md), `tests/host/ot125_monocypher_retry_runner_tests.py`, `tests/host/ot126_monocypher_retry_abort_tests.py` |
| OT-128 second corrective abort | Retained evidence did not identify the physical cause, but source review found no START/READY handshake, loss of partial lines across 250 ms reads, and fixed sleep instead of verified endpoint disappearance/return. | Require an idempotent readiness handshake, byte accumulation across empty reads, verified endpoint lifecycle, fixed deadlines, and adversarial fragmented-input tests before hardware. | [Decision 0067](decisions/0067-record-monocypher-second-corrective-retry-abort.md), [Decision 0068](decisions/0068-host-only-monocypher-start-ready-protocol-correction.md), `tests/host/ot129_control_protocol_tests.cpp`, `tests/host/ot129_monocypher_protocol_runner_tests.py` |
| OT-131, OT-133, and OT-137 preamble aborts | OT-131 bounded the failure to opaque pre-READY data. OT-133 proved the independent eight-record ceiling rejected nine records even within 512 bytes. OT-137 then showed queued startup bytes could exceed the single 512-byte post-START budget before firmware accepted START. | Bound opaque data by bytes/time rather than an observed line count; keep exact READY and strict post-READY parsing; separately account for startup transport data. Test fragmentation and alternate chunking of identical bytes. | [Decision 0070](decisions/0070-record-ot131-monocypher-execution-abort.md), [Decision 0072](decisions/0072-record-ot133-immutable-successor-execution-abort.md), [Decision 0074](decisions/0074-host-only-monocypher-byte-bounded-preamble-correction.md), [Decision 0076](decisions/0076-record-ot137-ot136-execution-abort.md), `tests/host/ot132_monocypher_protocol_runner_tests.py`, `tests/host/ot135_monocypher_protocol_runner_tests.py` |
| OT-093 through OT-134 deterministic Windows CI failures | The authoritative artifact existed and was below `MAX_BYTES`; Windows CRLF conversion changed 5,454 LF bytes into 5,494 bytes and changed the digest. After byte policy was fixed, shallow checkout omitted commits required for historical reconstruction. | Pin raw-byte attributes, print all three digest-guard inputs, audit EOL/BOM/final-newline/effective attributes, test `core.autocrlf=true`, and fetch enough history for reconstruction. Never weaken or auto-update the digest. | [Decision 0073](decisions/0073-freeze-windows-raw-evidence-checkout-bytes.md), `tests/host/raw_byte_checkout_policy_tests.py`, `tests/host/crypto_candidate_acquisition_inspection_tests.py` |
| OT-138 boot/control conflict | Generated config routed ESP-IDF INFO console output and the direct control protocol over the same USB Serial/JTAG endpoint. The runner could exhaust its preamble budget before application START handling. | Audit generated console/log settings and the linked driver. Isolate the control endpoint or build a reproducible quiet target; do not hide the conflict by enlarging parser limits. | [Decision 0077](decisions/0077-classify-monocypher-boot-control-transport-conflict.md), [Decision 0078](decisions/0078-accept-reproducible-monocypher-quiet-target.md), `tests/host/ot138_monocypher_boot_control_investigation_tests.py`, `tests/host/ot139_monocypher_quiet_target_tests.py` |
| OT-143 non-executable authority | Build evidence and authority bound the corrected 149,824-byte image, but the accepted coordinator still required the historical 149,920-byte image. Independently green layers did not describe one executable composition. | Require a real-file cross-layer identity test covering image name, size, digest, offset, restoration image, runner, adapter, coordinator, and authority before hardware. | [Decision 0081](decisions/0081-correct-ot143-runtime-binding-before-hardware.md), `tests/host/ot144_monocypher_binding_consistency_tests.py`, `tests/host/ot144_monocypher_hardware_adapter_tests.py` |
| OT-150/151 mbedTLS/PSA abort | The fixture correctly expected `PSA_ERROR_INVALID_ARGUMENT` for an all-zero X25519 peer but incorrectly required zero output length. The pinned implementation deliberately randomized the buffer and reported full output size after the error. | Test the pinned library's documented and actual error contract, including output length and buffer behavior. Keep status validation and zeroization while removing only the nonportable assertion. Recognize canonical early failure transcripts separately from an incomplete successful transcript. | [Decision 0087](decisions/0087-record-ot150-abort-and-correct-mbedtls-psa-successor.md), `tests/host/ot151_mbedtls_psa_corrected_target_tests.py`, `tests/host/ot151_mbedtls_psa_failure_transcript_tests.py` |
| OT-155 Noise XK radio abort | The exact physical cause remains unproved because the runner collapsed several failures into `radio_run_failed`. Host review found a reproducible lifecycle defect: `esp_restart()` can invalidate or re-enumerate the Windows serial handle, while the runner expected reboot receipts on the old handle. | Treat reset as a handle boundary, reopen both endpoints only after restart acknowledgement and bounded settling, reject stale receipts, and retain an allowlisted exact failure stage. Do not retroactively call a simulated cause the physical cause. | [Decision 0091](decisions/0091-record-ot155-noise-xk-radio-execution-abort.md), [Decision 0092](decisions/0092-accept-ot156-reset-aware-noise-xk-radio-host-correction.md), `tests/host/ot156_noise_xk_radio_runtime_tests.py`, `tests/host/ot156_noise_xk_radio_runner_tests.py` |
| OT-159 pre-consumption blockage | A successor coordinator's `_sha256(Path)` shadowed the inherited `_sha256(bytes)`. Exact readback bytes reached the path helper and failed before journal creation, write, radio use, or authority consumption. | Use purpose-specific helper names and exercise the real composed preflight path with exact, corrupt, and short bytes. Mock transport, not the helper/composition boundary being proved. | [Decision 0095](decisions/0095-record-ot159-preconsumption-noise-xk-blockage.md), [Decision 0096](decisions/0096-accept-ot160-noise-xk-hash-helper-host-correction.md), `tests/host/ot160_noise_xk_radio_hardware_adapter_tests.py`, `tests/host/ot160_noise_xk_radio_coordinator_tests.py` |
| OT-163 Noise XK radio abort | The retained stage is only `restart_ack_a`. It proves an incomplete acknowledgement for anonymous role A, but not whether the root cause was endpoint, host, USB, firmware, or hardware. | Keep stage-specific diagnostics and add deterministic simulations for missing/late/wrong restart acknowledgements before another bundle. Do not infer or advertise a radio result from planned operations. | [Decision 0099](decisions/0099-record-ot163-noise-xk-radio-execution-abort.md), `tests/host/ot163_noise_xk_radio_abort_record_tests.py` |
| OT-164 pairing-window V0 rejection | The first installed build lacked an emergency concealment path when footer restoration failed, so a PIN could remain visible. It was rejected before acceptance and replaced under separate authority. | Enumerate display failure transitions before flashing. Pairing secrets must clear on timeout, reset, close, render failure, and footer-restore failure; the emergency path must not depend on the failed renderer. | [Decision 0100](decisions/0100-accept-ot164-fresh-ble-pairing-window.md), [OT-164 note](../tests/hardware/OT-164-2026-08-29.md), `tests/host/ot164_heltec_v4_ble_pairing_window_v0_receipt_tests.py`, `tests/host/ot164_heltec_v4_ble_pairing_window_v1_success_record_tests.py` |
| OT-166 composition boundary | Initial durable ownership was host/build accepted, but physical pairing, reconnect, replacement, and Ready were explicitly unproved. OT-167 exposed why build-linked seams are not end-to-end proof. | Before porting, turn every deferred consequence into an acceptance test. Persist the exact initial owner before same-link GATT authorization, and test the complete phone-to-snapshot path rather than stopping at bond creation. | [Decision 0102](decisions/0102-accept-ot166-heltec-v1-bond-owner-integration.md), `tests/host/companion_v1_bond_owner_tests.cpp`, `tests/host/heltec_v4_bench_nimble_order_tests.py`, `tests/host/heltec_v4_bench_target_tests.py` |
| **Open OT-167: live connect reached `BLE:E`** | The observed sequence was advertising, phone connection attempt, terminal display error, then advertising after reboot. Current work has found ordering and error-classification gaps, but no accepted OT-167 evidence yet proves one exclusive physical root cause. | Exercise the exact callback/event order; persist/stage owner state before GATT; classify normal disconnect/termination races; keep expected window/connection outcomes nonfatal; prove genuine failures still contain. Re-run the exact live sequence only after composed host tests pass. | Current working tests: `tests/host/heltec_v4_bench_target_tests.py`, `tests/host/companion_pairing_window_tests.cpp`, `tests/host/companion_v1_bond_owner_tests.cpp`; final OT-167 evidence still required |
| **Open OT-167: replacement expiry and cleanup** | Current review found that candidate cleanup can delete/verify the replacement bond and clear volatile context without explicitly terminating the still-live candidate connection. This is a source-level open defect, not accepted physical evidence. | Capture the exact handle/generation before cleanup; on expiry, disconnect, reset, and abort, delete/verify only the candidate, then terminate that exact link outside the pairing lock. Accept already/not-connected races and contain a real termination failure. Test every interleaving behaviorally. | Current `companion_nimble_runtime.cpp` and the three working test files above; final OT-167 regression/evidence still required |
| **Open OT-167: authenticated status is still denied** | The Heltec runtime still composes `DeniedSnapshotAuthority`; the app cannot sustain a truthful Ready status even after authorization. The status wire contains categorical radio/GNSS/power/position state, not raw battery percent or satellite count. | Publish one coherent fixed-memory snapshot from the application task; never read task-owned GNSS/battery structs directly in the NimBLE callback. Keep unimplemented radio/actions unavailable and denied. Align advertised/UI capabilities before calling Ready fully functional. | `firmware/components/companion/include/opentrail/companion_request_coordinator.hpp`, `firmware/components/companion/include/opentrail/companion_semantics.hpp`, `tests/host/companion_request_coordinator_tests.cpp`; target regression still required |
| **OT-168 pre-hardware review: pairable marker mismatch** | Firmware replaced the ordinary D0 service marker with D1 during the unowned PIN window, while Android still filtered only D0. Each side passed its narrow tests, but a fresh device could never appear in Add Device. | Freeze advertising and GATT identities as a cross-layer contract. Enrollment scans and revalidates D1 only; protected GATT remains D0. Test the actual Android filter against the target constants before building or flashing. | `companion_nimble_runtime.cpp`, `AndroidBlePlatformPolicy.kt`, `AndroidBluetoothGattFacade.kt`, `tests/host/heltec_v4_bench_target_tests.py` |
| **OT-168 pre-hardware review: reset gate versus orphan recovery** | A marker-absent boot with no owner record and one bond is the exact recoverable residue of a failed initial-owner commit. The new reset-domain coherence check originally rejected it before the existing exact-orphan cleanup could run. | Read the reset marker first. A committed marker always resumes destructive cleanup without owner restoration; an absent marker may run exact owner/orphan reconciliation before the reset executor re-inspects both domains. Never replace this with broad bond deletion. | `device_factory_reset_executor.cpp`, `companion_v1_bond_owner.cpp`, `companion_nimble_runtime.cpp`, `companion_v1_bond_owner_tests.cpp` |
| **OT-168 pre-hardware review: owned reconnect continuity** | Owned D0 advertising initially initiated security only while a PIN window was open, and Android's selected endpoint existed only in process memory. An unknown central could hold a raw public link briefly, while the real owner could become unreachable after service/process restart. | Initiate Secure Connections on every accepted link. Keep Add Device D1-only; make returning-owner discovery a separate D0 path intersected with Android's bonded-device authority, forbid `createBond`, require one unambiguous candidate, and require protected device authorization before Ready. Do not persist MAC addresses or private pairing identifiers in the app. | `companion_nimble_runtime.cpp`, `AndroidBluetoothGattFacade.kt`, `BleCompanionRuntime.kt`, Android policy/runtime tests |
| **OT-168 pre-hardware review: terminal deadline before owner commit** | A secure-bond completion callback could arrive at the exact 60-second deadline before serialized expiry ran. The original composition could persist the owner first and only then discover that `finish_attempt` correctly considered the window expired. | Reserve the exact secure-bond terminal synchronously under the pairing lock at the callback timestamp before any owner write. Accept only timestamps strictly before the deadline; defer OLED cleanup to the owner task; on late or failed publication, clear pairable advertising and reboot through exact orphan reconciliation. Test 59,999 ms and 60,000 ms explicitly. | `companion_pairing_window.cpp`, `companion_nimble_runtime.cpp`, `companion_pairing_window_tests.cpp`, `tests/host/heltec_v4_bench_target_tests.py` |

## Coverage gaps to preserve

- No pre-2026-08-10 unsanitized history was available in this audit, so earlier
  failed experiments may be absent.
- Several physical aborts intentionally retained only privacy-safe categories.
  OT-073, OT-124, OT-128, OT-155, and OT-163 therefore do not support a unique
  physical root-cause claim beyond the boundaries stated above.
- OT-167 is still a dirty working-tree investigation. Its open entries must be
  updated or removed when accepted decision/evidence records establish the
  final behavior.
- This checklist does not replace board datasheets, target-specific errata,
  radio-region compliance, power calibration, GNSS fix validation, signed-
  release admission, or physical two-device/two-phone acceptance.
