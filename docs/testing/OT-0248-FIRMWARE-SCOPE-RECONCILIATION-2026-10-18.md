# OT-0248 firmware scope, target and fixture reconciliation

Date: 2026-10-18.
Status: local host-only reconciliation; owner review and publication pending.
Source: main `bef47d27dfc65b14b7bd6c6ab652b2fe95ab5fa9`.
Branch: `codex/ot0248-firmware-scope-reconciliation`.

## Scope and rules

The [accepted scope](V1_V1_5_ACCEPTANCE_SCOPE_V0.md) fixes two phone/device
pairs and direct LoRa for V1; four-node V1.5 is separate. Coherent messaging,
rejection, interruption, restart/reset and signed Android acceptance require
one frozen evidence set. Source presence, existing test vectors and historical
passes do not establish current execution, supported hardware or production
integration. UNKNOWN means not established by this bounded inspection, not
missing everywhere or failed. No new results or thresholds are selected.

[Architecture](../ARCHITECTURE.md) separates evaluation frames from selected
production wire. [Status](../PROJECT_STATUS.md) and [backlog](../../tasks/BACKLOG.md)
retain dated evidence; older pending/next statements do not override later
acceptance records. Historical observations below were read, not rerun.

Active root: `C:/Projects/LimitedUnderground/Trail`. Old
`C:/lu/OpenTrail/.private/ot177-publication` paths in reports are provenance,
not active source. Nested `.private/ot177-publication` copies are historical.
No old root was followed. The reset/unpaired Heltec V4 Trail Bench condition
is owner-reported; current firmware, settings, pairing and readiness are UNKNOWN.
No E: or hardware access, firmware tests/builds, implementation or push occurred.

## Scope to target/source to fixture to evidence

Links identify current files, not fresh test results. Inline scenarios are
identified explicitly rather than inventing shared wire corpora.

| Scope / owner | Target or source | Fixture and consumer | Existing evidence / limits |
| --- | --- | --- | --- |
| Protected BLE/versioned Companion control; OT-168 / V1 authorization | [Bench composition](../../firmware/targets/heltec_v4_bench/main/CMakeLists.txt), [protocol](../../firmware/components/companion/src/companion_protocol.cpp) | [Protocol-v0 CSV](../../tests/fixtures/companion_protocol_v0_vectors.csv), [C++ test](../../tests/host/companion_protocol_tests.cpp), [Kotlin test](../../android/protocol/src/test/kotlin/io/github/nbjelanovic/otprotocol/CompanionProtocolCodecTest.kt). Kotlin reads the resource; exact per-row cross-language expectation equivalence is UNKNOWN here. | Architecture/status record bounded protected BLE and saved-owner observations. No current pairing, complete reset, production messaging or secure-LoRa selection claim. |
| Name/configuration; OT-170/171/178 | [Name codec](../../firmware/components/companion/src/companion_device_name_codec.cpp), [configuration codec](../../firmware/components/companion/src/companion_configuration_codec.cpp) | [Name TSV](../../tests/fixtures/companion_device_name_v1.tsv), [v02 TSV](../../tests/fixtures/companion_configuration_v02.tsv), [v03 TSV](../../tests/fixtures/companion_configuration_v03.tsv); existing C++ [name](../../tests/host/companion_device_name_codec_tests.cpp), [configuration](../../tests/host/companion_configuration_codec_tests.cpp), [region](../../tests/host/companion_region_owner_tests.cpp) and Kotlin [name](../../android/protocol/src/test/kotlin/io/github/nbjelanovic/otprotocol/CompanionNamePayloadTest.kt), [region](../../android/protocol/src/test/kotlin/io/github/nbjelanovic/otprotocol/CompanionRegionConfigurationTest.kt) consumers. | [Dated codec parity](OT-170-178-CONFIGURATION-CODECS-2026-09-06.md) records isolated validation and deployment limits. Consumers already exist; v02 results do not establish all v03 behavior. Fresh execution/complete production acceptance UNKNOWN. |
| Fail-closed reset/recovery; OT-0168 | [Reset contract](../platform/DEVICE_FACTORY_RESET_V1.md), [executor](../../firmware/components/companion/src/device_factory_reset_executor.cpp), [bench storage](../../firmware/targets/heltec_v4_bench/main/heltec_v4_factory_reset_storage.cpp) | Inline [executor](../../tests/host/device_factory_reset_executor_tests.cpp) and [target composition](../../tests/host/factory_reset_target_composition_tests.cpp) scenarios; no standalone shared reset corpus asserted. | Architecture/status distinguish host implementation from physical destructive reset/erasure, interruption, old-owner rejection and two-pair acceptance. Full physical acceptance UNKNOWN. |
| Evaluation radio completion/rearm; OT-0246 / OT-0247c-g | [Enrolled target](../../firmware/targets/heltec_v4_enrolled_eval/main/CMakeLists.txt), [transport](../../firmware/components/security_evaluation/include/opentrail/enrolled_peer_transport.hpp), [packet codec](../../firmware/components/protocol/src/packet_codec.cpp) | Inline [completion workflows](../../tests/host/enrolled_completion_composed_tests.cpp), [packet tests](../../tests/host/packet_codec_tests.cpp). Companion CSVs are not selected production LoRa fixtures. | Preserve [OT-246 failure](OT-246-FAILURE-CAPTURE-2026-09-18.md), [audit](OT-247-END-TO-END-RADIO-AUDIT-2026-09-20.md), [correction/erratum](OT-0247g-GUARDED-COMPLETION-2026-09-22.md), [corrected nearby RF trial](OT-0247d-G-PHYSICAL-2026-09-23.md). Later status/backlog record corrected-trial acceptance; older pending text remains historical. Nearby RF is not range/endurance, complete lifecycle, phone-path or production acceptance. |
| Isolated enrollment/rekey/recovery; OT-0238b/c | [Candidate composition](../../firmware/targets/heltec_v4_enrollment_candidate_eval/main/CMakeLists.txt), [session](../../firmware/components/security_evaluation/include/opentrail/enrollment_candidate_session.hpp) | Inline real-owner/simulated-SDK [device-session tests](../../tests/host/enrollment_candidate_device_session_tests.cpp); not selected production wire. | [Clock/input host report](OT-0238b-CANDIDATE-CLOCK-2026-09-30.md) and [USB rekey](../../tests/hardware/OT-0238c-RETAINED-REKEY-2026-10-12.md) remain distinct from RF/phone. OT-0237e selection and OT-0005i production binding remain separate. Full physical lifecycle UNKNOWN. |
| GNSS/clock; OT-0101e | [Bench composition](../../firmware/targets/heltec_v4_bench/main/CMakeLists.txt), [clock](../../firmware/components/time/src/oled_clock.cpp), [observation tool](../../tools/gnss_lifecycle_observation.py) | Existing host lifecycle tools/tests are preparation, not live GPS observations or wire fixtures. Exact physical coverage UNKNOWN. | [Partial lifecycle report](OT-0101e-LIFECYCLE-PROGRESS-2026-09-29.md): passed same-boot disconnected-clock retention; inconclusive GPS loss, later cases not reached, original restoration separate. Historical evidence does not describe the reset Bench now. |

## Reconciled boundaries and UNKNOWNs

- A bench/evaluation directory does not confer supported-target status. Exact
  MCU/radio/pins/band/install/recovery require their own evidence.
- Existing fixtures do not select a production library, handshake/KDF/wire,
  timing or PHY. Final selection and production binding keep their own gates.
- Accepted radio corrections/trial should not be reimplemented or repeated
  solely because a broad parent row or historical report says pending.
- OT-0250a already owns the local five-lane acceptance index; do not recreate it.
  Lifecycle, current pairing, raw-artifact integrity and field acceptance stay
  UNKNOWN. Original raw artifact availability/byte identity was not verified.
- Other target families, current deployment identity and exact row-level
  protocol-v0 cross-language coverage are UNKNOWN in this bounded mapping.
  No implementation defect is inferred from an untraced coverage claim.

## One remaining concrete host-only task: proposal only

Perform a source-only coverage reconciliation of the two named rows in
[companion_protocol_v0_vectors.csv](../../tests/fixtures/companion_protocol_v0_vectors.csv)
against [C++ protocol tests](../../tests/host/companion_protocol_tests.cpp),
[Kotlin protocol tests](../../android/protocol/src/test/kotlin/io/github/nbjelanovic/otprotocol/CompanionProtocolCodecTest.kt)
and [protocol source](../../firmware/components/companion/src/companion_protocol.cpp).

Done: a two-row map records exact bytes, consumer/assertions, decoded fields,
and relevant rejection expectations; distinguish shared-resource from embedded
vectors. Any unproved equivalence or coverage remains UNKNOWN. Absence of a CSV
reader is not a defect. Existing name/configuration consumers are not missing.
No new codec, fixture changes, test execution, hardware or acceptance is implied.
Any concrete discrepancy needs a separate exact fix plan. Agree the diagnostic's
output and task registration before starting; it is not started or registered
as a new board item by this increment.

## Validation and review boundary

Run the repository documentation checker and documentation regression suite,
check this report's local links remain inside the C root, and verify the final
three-file scope. Independently review source claims, historical boundaries,
UNKNOWNs and bounded follow-up. Actual command/review outcomes are reported to
the owner, not asserted merely by this plan text.

No public website capability, V1 score, physical or release acceptance changes.
This local documentation increment is distinct from owner acceptance and
publication; no commit or push is included.
