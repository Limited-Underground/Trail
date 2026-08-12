# OpenTrail Documentation

This is the organized entry point for OpenTrail design, evidence, and engineering records. The root [README](../README.md) gives the short project overview; this page routes readers to the right level of detail.

## Start with these documents

| Document | Use it for |
| --- | --- |
| [Architecture](ARCHITECTURE.md) | System layers, roles, interfaces, protocols, and failure behavior |
| [Product boundaries](PRODUCT_BOUNDARIES_V0.md) | Required base-client behavior, optional additions, allowed data, and degraded operation |
| [Project status](PROJECT_STATUS.md) | Current evidence, assumptions, hardware, unresolved decisions, and the next checkpoint |
| [Progress log](PROGRESS_LOG.md) | Public chronology grouped by date, newest first |
| [Engineering backlog](../tasks/BACKLOG.md) | Work-item status, exact acceptance evidence, and recommended sequence |
| [Hardware inventory](../hardware/INVENTORY.md) | Available, ordered, missing, and unverified equipment |

## Find information by goal

| If you want to... | Start here |
| --- | --- |
| Understand the first release | [Capacity policy](testing/FIRST_RELEASE_CAPACITY_V0.md), [product boundaries](PRODUCT_BOUNDARIES_V0.md), and [four-person pilot](testing/FOUR_PERSON_PILOT_V0.md) |
| Follow recent work | [Progress log](PROGRESS_LOG.md) |
| See what is complete or still planned | [Engineering backlog](../tasks/BACKLOG.md) |
| Work on a portable client target | [Portable-client composition](platform/PORTABLE_CLIENT_COMPOSITION_V0.md), [development guide](DEVELOPMENT.md), and [hardware inventory](../hardware/INVENTORY.md) |
| Work on radio packets or delivery | [Protocol documents](protocol/) and [immutable repeater decision](decisions/0004-immutable-first-release-forwarding.md) |
| Review identity or security | [Threat model](security/THREAT_MODEL_V0.md), [group lifecycle](security/GROUP_LIFECYCLE_V0.md), and [crypto candidate review](security/CRYPTO_CANDIDATE_REVIEW_2026-08-10.md) |
| Work on position sharing or optional breadcrumb archiving | [GPS abstraction](location/GPS_ABSTRACTION.md), [position scheduler](protocol/POSITION_BROADCAST_SCHEDULER_V0.md), [privacy control](platform/POSITION_SHARING_CONTROL_V0.md), [checked-time archive retry](location/BREADCRUMB_ARCHIVE_RETRY_V0.md), [privacy-safe archive presentation](location/BREADCRUMB_ARCHIVE_PRESENTATION_V0.md), and [single-read archive snapshot](location/BREADCRUMB_ARCHIVE_SNAPSHOT_V0.md) |
| Work on offline maps | [Offline-map architecture](maps/OFFLINE_MAP_ARCHITECTURE_V0.md) and the [maps folder](maps/) |
| Review OpenGauge integration | [Critical-alert format](integration/OPENGAUGE_CRITICAL_ALERT_V0.md), [acknowledgement format](integration/OPENGAUGE_CRITICAL_ALERT_ACK_V0.md), and [responder](integration/CRITICAL_ALERT_ACK_RESPONDER_V0.md) |
| Plan or evaluate a field test | [Testing documents](testing/) and the [privacy-safe field evidence contract](testing/FIELD_TEST_LOG_V0.md) |

## Documentation areas

| Area | Contents | Key entry point |
| --- | --- | --- |
| [Architecture decisions](decisions/) | Recorded constraints that future work must preserve | [Immutable first-release forwarding](decisions/0004-immutable-first-release-forwarding.md) |
| [Protocol](protocol/) | Packet v0, delivery, priority, position, repeater, packet sizing, and reassembly | [Experimental packet v0](protocol/EXPERIMENTAL_PACKET_V0.md) |
| [Platform](platform/) | Portable-client composition, time, power, UI, position commands, and runtime ordering | [Portable-client composition](platform/PORTABLE_CLIENT_COMPOSITION_V0.md) |
| [Location and archive](location/) | GPS behavior and opt-in breadcrumb archive boundaries | [GPS abstraction](location/GPS_ABSTRACTION.md) |
| [Persistence](persistence/) | Configuration, duplicate state, ACK sessions, and target-shaped storage adapters | [Persistent configuration](persistence/PERSISTENT_CONFIGURATION_V0.md) |
| [Security](security/) | Threat model, group lifecycle, entropy, crypto evaluation, counters, nonces, and key context | [Threat model](security/THREAT_MODEL_V0.md) |
| [Offline maps](maps/) | Package rights, manifest, activation, selector recovery, trusted history, and domain lifecycle | [Offline-map architecture](maps/OFFLINE_MAP_ARCHITECTURE_V0.md) |
| [Updates and recovery](update/) | Signed-update architecture, checkpoints, trusted floors, boot/save transitions, and presentation | [Update/recovery architecture](update/UPDATE_RECOVERY_ARCHITECTURE_V0.md) |
| [Diagnostics](diagnostics/) | Redacted events, RAM logging, and strict offline operator decoders | [Diagnostics overview](DIAGNOSTICS.md) |
| [OpenGauge integration](integration/) | Normalized critical alerts, acknowledgements, and responder behavior | [Critical-alert contract](integration/OPENGAUGE_CRITICAL_ALERT_V0.md) |
| [Testing](testing/) | Release capacity, group-load planning, pilot plans/results, and privacy-safe public evidence | [First-release capacity](testing/FIRST_RELEASE_CAPACITY_V0.md) |
| [Funding preparation](funding/) | Paused, brand-neutral planning documents; not an active application or outreach program | [Funding packet status](funding/README.md) |

## Important project workflows

### First four-person pilot

1. Confirm the [first-release capacity policy](testing/FIRST_RELEASE_CAPACITY_V0.md).
2. Freeze one exact four-unit client configuration under the [pilot plan](testing/FOUR_PERSON_PILOT_V0.md).
3. Publish only evidence accepted by the [privacy-safe field log](testing/FIELD_TEST_LOG_V0.md).
4. Produce the deterministic verdict defined by the [pilot result evaluator](testing/FOUR_PERSON_PILOT_RESULT_V0.md).

The pilot is currently blocked at the hardware/firmware freeze. A complete plan and evaluator do not constitute a live pass.

### Hardware arrival and compatibility

- Begin with the [hardware inventory](../hardware/INVENTORY.md).
- Reconcile exact labels and operating authorization through the [hardware/regulatory inventory](../hardware/HARDWARE_REGULATORY_INVENTORY_2026-08-10.md).
- When the Wio Tracker arrives, follow the [recovery-first bring-up procedure](../hardware/WIO_TRACKER_L1_PRO_BRINGUP.md) before any firmware write.
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
