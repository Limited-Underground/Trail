# Trail documentation

Use this index to find design contracts, accepted evidence, and contribution
guidance. The [project README](../README.md) is the product overview.

## Start here

| Document | Purpose |
| --- | --- |
| [Current status](PROJECT_STATUS.md) | Accepted capabilities, limitations, and next gates |
| [Architecture](ARCHITECTURE.md) | System boundaries, interfaces, and failure behavior |
| [Development guide](DEVELOPMENT.md) | Toolchains and repeatable validation |
| [Backlog](../tasks/BACKLOG.md) | Scoped engineering work and acceptance criteria |
| [V1 progress](V1_PROGRESS.json) | Canonical milestone evidence and completion |
| [Progress log](PROGRESS_LOG.md) | Dated engineering history |
| [Community guide](community/README.md) | Contribution routes and coordination |
| [iPhone contributor brief](community/IOS_CONTRIBUTOR_BRIEF.md) | Starting scope for an iOS companion |

## Product and implementation

| Area | Entry point |
| --- | --- |
| Product scope | [Product boundaries](PRODUCT_BOUNDARIES_V0.md), [V1 user experience](product/V1_USER_EXPERIENCE.md), [V1/V1.5 acceptance scope](testing/V1_V1_5_ACCEPTANCE_SCOPE_V0.md) |
| Android | [Client development](../android/README.md) |
| Firmware | [Porting lessons and preflight](firmware-porting-lessons.md), [components](../firmware/components/), [targets](../firmware/targets/) |
| Hardware | [Inventory](../hardware/INVENTORY.md), [hardware procedures](../hardware/) |
| Device setup and reset | [Factory reset contract](platform/DEVICE_FACTORY_RESET_V1.md), [decision](decisions/0103-adopt-ot168-v1-factory-reset-and-boot-pairing.md) |
| Display | [Heltec OLED V1 layout](product/HELTEC_OLED_V1.md) |
| Protocols | [Protocol contracts](protocol/) |
| Security | [Threat model](security/THREAT_MODEL_V0.md), [security contracts](security/) |
| Location and maps | [Location](location/), [maps](maps/) |
| Platform and persistence | [Platform contracts](platform/), [persistence](persistence/) |
| Integrations | [Integration contracts](integration/) |
| Future work | [Future concepts](FUTURE_CONCEPTS.md) |

Versioned contracts may describe historical experiments. Check current status and
the relevant [architecture decision](decisions/) before treating an older
document as implementation authority.

## Evidence and history

- [Testing documentation](testing/) describes validation contracts and results.
- [Hardware evidence](../tests/hardware/) records exact experimental setups and
  their limitations.
- [Host tests](../tests/host/) validate software behavior without establishing
  physical radio or field performance.
- [Architecture decisions](decisions/) preserve the reasoning behind constraints.
- [Progress log](PROGRESS_LOG.md) preserves dated task outcomes.
- [Funding preparation](funding/README.md) is separate from release readiness.

**Planned** behavior is a goal. **Host-tested** behavior has software evidence.
**Bench evidence** applies only to its recorded setup. **Supported** hardware
requires repeatable compatibility, recovery, power, radio, and field acceptance.
A build alone does not establish any of those outcomes.

## Repository policies

Read [CONTRIBUTING.md](../CONTRIBUTING.md) and [AGENTS.md](../AGENTS.md) before
implementation. Use [SECURITY.md](../SECURITY.md) for sensitive reports.
The project uses the [Apache License 2.0](../LICENSE).