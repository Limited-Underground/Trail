# OpenTrail Documentation

This is the organized entry point for OpenTrail design, evidence, and engineering records. The root [README](../README.md) gives the short project overview; this page routes readers to the right level of detail.

## Start with these documents

| Document | Use it for |
| --- | --- |
| [Architecture](ARCHITECTURE.md) | System layers, roles, interfaces, protocols, and failure behavior |
| [Product boundaries](PRODUCT_BOUNDARIES_V0.md) | Required base-client behavior, optional additions, allowed data, and degraded operation |
| [Project status](PROJECT_STATUS.md) | Current evidence, assumptions, hardware, unresolved decisions, and the next checkpoint |
| [Progress log](PROGRESS_LOG.md) | Public chronology grouped by date, newest first |
| [Future concepts](FUTURE_CONCEPTS.md) | Unscheduled post-release ideas and accepted directions with explicit safety and no-progress boundaries |
| [Engineering backlog](../tasks/BACKLOG.md) | Work-item status, exact acceptance evidence, and recommended sequence |
| [Hardware inventory](../hardware/INVENTORY.md) | Available, ordered, missing, and unverified equipment |

## Find information by goal

| If you want to... | Start here |
| --- | --- |
| Understand V1 and V1.5 | [Permanent V1/V1.5 acceptance scope](testing/V1_V1_5_ACCEPTANCE_SCOPE_V0.md), [scope/security decision](decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md), [host-tested BLE pairing/replacement contract](platform/BLE_PAIRING_REPLACEMENT_V0.md), [host-tested secure-LoRa lifecycle/admission contract](security/SECURE_LORA_KEY_TRANSPORT_V0.md), [pre-crypto build baseline decision](decisions/0037-pre-crypto-build-baseline.md), [blocked candidate-readiness decision](decisions/0038-host-only-ot005-candidate-readiness-contract.md), [source-lock admission decision](decisions/0039-host-only-candidate-source-lock-admission-contract.md), [capacity policy](testing/FIRST_RELEASE_CAPACITY_V0.md), and [product boundaries](PRODUCT_BOUNDARIES_V0.md) |
| Follow recent work | [Progress log](PROGRESS_LOG.md) |
| See what is complete or still planned | [Engineering backlog](../tasks/BACKLOG.md) |
| Review post-V2 future options | [Future concepts register](FUTURE_CONCEPTS.md) and [Public Assistance direction](decisions/0036-post-v2-public-lane-and-assistance-direction.md) |
| Work on a portable client target | [Portable-client composition](platform/PORTABLE_CLIENT_COMPOSITION_V0.md), [build-only Heltec V4 bench candidate](../firmware/targets/heltec_v4_bench/README.md), [OT-093 reproducible pre-crypto baseline](../tests/hardware/OT-093-2026-08-20.md), [OT-059 exact-profile evidence](../tests/hardware/OT-059-2026-08-15.md), [OT-056 runtime-owner evidence](../tests/hardware/OT-056-2026-08-15.md), [development guide](DEVELOPMENT.md), and [hardware inventory](../hardware/INVENTORY.md) |
| Work on the laptop simulator or client presentation tracks | [Dual virtual-LCD simulator and OT-058 bounded pump](testing/DUAL_VIRTUAL_LCD_SIMULATOR_V0.md), [Android position-observation and Group / Location boundary](platform/POSITION_SHARING_UI_OBSERVATION_V0.md), [shared client-track decision](decisions/0007-shared-client-presentation-tracks.md), and [local interface](platform/LOCAL_INTERFACE_V0.md) |
| Work on the Android BLE companion path | [Permanent V1/V1.5 scope](testing/V1_V1_5_ACCEPTANCE_SCOPE_V0.md), [host-tested BLE pairing/replacement contract](platform/BLE_PAIRING_REPLACEMENT_V0.md), [Android operational-release acceptance](platform/ANDROID_OPERATIONAL_RELEASE_ACCEPTANCE_V0.md), [private-pilot operational policy](platform/ANDROID_PRIVATE_PILOT_OPERATIONAL_POLICY_V0.md), [BLE Companion GATT and coded runtime owner](platform/BLE_COMPANION_GATT_V0.md), [one-phone authorization policy](platform/COMPANION_AUTHORIZATION_V0.md), [authorization storage history/current boundary](platform/COMPANION_AUTHORIZATION_STORAGE_V0.md), [Android client foundation](platform/ANDROID_CLIENT_FOUNDATION_V0.md), [request coordinator](platform/COMPANION_REQUEST_COORDINATOR_V0.md), and [threat model](security/THREAT_MODEL_V0.md) |
| Understand the working product names | [Limited Underground Trail product-family decision](decisions/0008-limited-underground-trail-working-product-family.md) and [product boundaries](PRODUCT_BOUNDARIES_V0.md) |
| Work on the Windows firmware loader | [WPF desktop shell](update/WINDOWS_LOADER_DESKTOP_SHELL_V0.md), [selected-device bundle match](update/WINDOWS_LOADER_DEVICE_BUNDLE_MATCH_V0.md), [bundle candidate format](update/FIRMWARE_BUNDLE_CANDIDATE_FORMAT_V0.md), [release-signature decision](decisions/0006-firmware-bundle-signature.md), [live inspection model](update/WINDOWS_LOADER_INSPECTION_VIEW_V0.md), [Windows USB/runtime inspection](update/WINDOWS_USB_CANDIDATE_DISCOVERY_V0.md), [final write admission](update/FIRMWARE_WRITE_ADMISSION_V0.md), [firmware-bundle admission](update/FIRMWARE_BUNDLE_ADMISSION_V0.md), [firmware-install preflight](update/FIRMWARE_INSTALL_PREFLIGHT_V0.md), [update/recovery architecture](update/UPDATE_RECOVERY_ARCHITECTURE_V0.md), and [hardware inventory](../hardware/INVENTORY.md) |
| Work on radio packets or delivery | [Secure-LoRa lifecycle/admission contract](security/SECURE_LORA_KEY_TRANSPORT_V0.md), [Protocol documents](protocol/), [generic quick-status payload](protocol/QUICK_STATUS_PAYLOAD_V0.md), and [immutable repeater decision](decisions/0004-immutable-first-release-forwarding.md) |
| Work on local quick-status selection | [Parent-page handoff](platform/QUICK_STATUS_PARENT_PAGE_COORDINATOR_V0.md), [revision-safe menu](platform/QUICK_STATUS_MENU_COORDINATOR_V0.md), [local interface](platform/LOCAL_INTERFACE_V0.md), and [payload contract](protocol/QUICK_STATUS_PAYLOAD_V0.md) |
| Review identity or security | [Secure-LoRa lifecycle/admission contract](security/SECURE_LORA_KEY_TRANSPORT_V0.md), [Decision 0035](decisions/0035-host-tested-secure-lora-key-transport-contract.md), [pre-crypto build baseline decision](decisions/0037-pre-crypto-build-baseline.md), [blocked candidate-readiness decision](decisions/0038-host-only-ot005-candidate-readiness-contract.md), [source-lock admission decision](decisions/0039-host-only-candidate-source-lock-admission-contract.md), [mbedTLS static-eligibility decision](decisions/0040-host-only-mbedtls-psa-static-eligibility.md), [license-aware source-lock decision](decisions/0041-license-aware-source-lock-admission-v1.md), [external candidate acquisition decision](decisions/0042-external-candidate-acquisition-static-inspection.md), [managed-import evidence decision](decisions/0043-libsodium-managed-import-evidence.md), [OT-099 host evidence](../tests/hardware/OT-099-2026-08-20.md), [benchmark evidence boundary](security/CRYPTO_BENCHMARK_EVIDENCE_V0.md), [threat model](security/THREAT_MODEL_V0.md), and [group lifecycle](security/GROUP_LIFECYCLE_V0.md) |
| Work on position sharing or optional breadcrumb archiving | [GPS abstraction](location/GPS_ABSTRACTION.md), [position scheduler](protocol/POSITION_BROADCAST_SCHEDULER_V0.md), [privacy control](platform/POSITION_SHARING_CONTROL_V0.md), [checked-time archive retry](location/BREADCRUMB_ARCHIVE_RETRY_V0.md), [privacy-safe archive presentation](location/BREADCRUMB_ARCHIVE_PRESENTATION_V0.md), [single-read archive snapshot](location/BREADCRUMB_ARCHIVE_SNAPSHOT_V0.md), [serialized snapshot adapter](location/BREADCRUMB_ARCHIVE_SNAPSHOT_ADAPTER_V0.md), [private runtime owner](location/BREADCRUMB_ARCHIVE_RUNTIME_OWNER_V0.md), [restart-safe session leases](persistence/BREADCRUMB_ARCHIVE_SESSION_LEASE_STORE_V0.md), [local archive consent](location/BREADCRUMB_ARCHIVE_LOCAL_CONSENT_V0.md), [complete local archive workflow](location/BREADCRUMB_ARCHIVE_WORKFLOW_COORDINATOR_V0.md), [durable workflow bootstrap](location/BREADCRUMB_ARCHIVE_WORKFLOW_BOOTSTRAP_V0.md), [exact-revision navigation](location/BREADCRUMB_ARCHIVE_NAVIGATION_COORDINATOR_V0.md), [optional parent page](location/BREADCRUMB_ARCHIVE_PARENT_PAGE_COORDINATOR_V0.md), and [passive single-owner archive UI](location/BREADCRUMB_ARCHIVE_UI_COORDINATOR_V0.md) |
| Work on offline maps | [Offline-map architecture](maps/OFFLINE_MAP_ARCHITECTURE_V0.md) and the [maps folder](maps/) |
| Review OpenGauge integration | [Critical-alert format](integration/OPENGAUGE_CRITICAL_ALERT_V0.md), [acknowledgement format](integration/OPENGAUGE_CRITICAL_ALERT_ACK_V0.md), and [responder](integration/CRITICAL_ALERT_ACK_RESPONDER_V0.md) |
| Plan or evaluate a field test | [Testing documents](testing/) and the [privacy-safe field evidence contract](testing/FIELD_TEST_LOG_V0.md) |

## Documentation areas

| Area | Contents | Key entry point |
| --- | --- | --- |
| [Architecture decisions](decisions/) | Recorded constraints that future work must preserve | [Limited Underground Trail working product family](decisions/0008-limited-underground-trail-working-product-family.md) |
| [Protocol](protocol/) | Packet v0, delivery, priority, position, repeater, packet sizing, and reassembly | [Experimental packet v0](protocol/EXPERIMENTAL_PACKET_V0.md) |
| [Platform](platform/) | Portable-client composition, BLE companion boundary, time, power, UI, position commands, and runtime ordering | [Portable-client composition](platform/PORTABLE_CLIENT_COMPOSITION_V0.md) |
| [Location and archive](location/) | GPS behavior and opt-in breadcrumb archive boundaries | [GPS abstraction](location/GPS_ABSTRACTION.md) |
| [Persistence](persistence/) | Configuration, duplicate state, archive leases, ACK sessions, and target-shaped storage adapters | [Persistent configuration](persistence/PERSISTENT_CONFIGURATION_V0.md) |
| [Security](security/) | Threat model, group lifecycle, entropy, crypto evaluation, counters, nonces, and key context | [Threat model](security/THREAT_MODEL_V0.md) |
| [Offline maps](maps/) | Package rights, manifest, activation, selector recovery, trusted history, and domain lifecycle | [Offline-map architecture](maps/OFFLINE_MAP_ARCHITECTURE_V0.md) |
| [Updates and recovery](update/) | Signed-update architecture, checkpoints, trusted floors, boot/save transitions, and presentation | [Update/recovery architecture](update/UPDATE_RECOVERY_ARCHITECTURE_V0.md) |
| [Diagnostics](diagnostics/) | Redacted events, RAM logging, and strict offline operator decoders | [Diagnostics overview](DIAGNOSTICS.md) |
| [OpenGauge integration](integration/) | Normalized critical alerts, acknowledgements, and responder behavior | [Critical-alert contract](integration/OPENGAUGE_CRITICAL_ALERT_V0.md) |
| [Testing](testing/) | Host simulation, release capacity, group-load planning, pilot plans/results, and privacy-safe public evidence | [Dual virtual-LCD simulator](testing/DUAL_VIRTUAL_LCD_SIMULATOR_V0.md) |
| [Funding preparation](funding/) | Paused, brand-neutral planning documents; not an active application or outreach program | [Funding packet status](funding/README.md) |

## Important project workflows

### V1 and V1.5 physical acceptance

1. Follow the [permanent V1/V1.5 scope](testing/V1_V1_5_ACCEPTANCE_SCOPE_V0.md).
2. Use the frozen, host-tested [OTBP0/v0 pairing/replacement contract](platform/BLE_PAIRING_REPLACEMENT_V0.md) as the authorization authority.
3. Use the frozen, host-tested algorithm-neutral [`OTSL0/v0`](security/SECURE_LORA_KEY_TRANSPORT_V0.md) lifecycle/admission contract as the secure-LoRa semantic authority.
4. Preserve the accepted [OT-093 pre-crypto build baseline](../tests/hardware/OT-093-2026-08-20.md), blocked [OT-094 candidate-readiness contract](../tests/hardware/OT-094-2026-08-20.md), historical non-admitting [OT-095 source-lock contract](../tests/hardware/OT-095-2026-08-20.md), bounded [OT-096 mbedTLS static assessment](../tests/hardware/OT-096-2026-08-20.md), and current license-aware [OT-097 source-lock admission v1](../tests/hardware/OT-097-2026-08-20.md).
5. Close all six readiness requirements, accept a new immutable executable benchmark plan, and run the exact candidate comparison under separate authority.
6. Explicitly accept the suite/library, handshake/KDF, and packet-v1 wire selection only after that benchmark passes.
7. Then implement and physically accept the frozen pairing/replacement and selected secure-LoRa paths only under separate authority.
8. Run one coherent two-phone/two-Heltec V1 acceptance only after its exact candidates and release artifact are frozen.
9. Defer four-supported-node interoperability and every relay claim to V1.5.

The historical [four-person standalone pilot](testing/FOUR_PERSON_PILOT_V0.md) and [result evaluator](testing/FOUR_PERSON_PILOT_RESULT_V0.md) remain preserved evidence contracts but no longer define V1 Companion completion.

### Hardware arrival and compatibility

- Begin with the [hardware inventory](../hardware/INVENTORY.md).
- Reconcile exact labels and operating authorization through the [hardware/regulatory inventory](../hardware/HARDWARE_REGULATORY_INVENTORY_2026-08-10.md).
- Review the [privacy-safe three-device GNSS snapshot](../tests/hardware/OT-003A-2026-08-12.md) for the current Heltec detection/activation and SenseCAP live-fix boundary.
- Continue the arrived Wio Tracker's [partially executed recovery-first bring-up procedure](../hardware/WIO_TRACKER_L1_PRO_BRINGUP.md), preserving its documented pre-write-state gap and the remaining no-transmit/recovery boundaries.
- Record physical results under [`tests/hardware/`](../tests/hardware/) with privacy-safe public summaries and local raw captures.

### Firmware and host validation

- Follow the [development guide](DEVELOPMENT.md).
- Keep reusable logic under `firmware/components/` and board/role composition under `firmware/targets/`.
- Use the [architecture](ARCHITECTURE.md) and backlog acceptance criteria before changing a protocol or target boundary.
- Run the complete repository validation gate before publishing a change.

## Evidence labels

- **Planned** means the design or test has not produced implementation evidence.
- **Host-tested** means deterministic software behavior passed on the development host; it does not prove an ESP32 target or physical radio.
- **Bench evidence** applies only to the exact hardware, firmware, setup, and bounded result in the linked record.
- **Supported** must not be used until repeatable target, recovery, power, radio, regulatory, and field acceptance evidence exists.

## Repository policies

- [Contribution guide](../CONTRIBUTING.md)
- [Security reporting](../SECURITY.md)
- [Apache License 2.0](../LICENSE)
