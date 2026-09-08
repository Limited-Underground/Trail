# Limited Underground Trail

## About Trail

Trail is an open-source project for sharing messages, location, and status between
people exploring, working, or traveling together. The intended system pairs a
phone with a portable radio device and carries compact traffic over direct LoRa.

```text
Phone ← BLE → Trail device ← direct LoRa → Trail device ← BLE → Phone
```

This is the planned product topology. An internet service is not required by the
V1 design; optional maps and later repeater work have separate scope.

## Current status

Trail is an experimental project, not a production release. Android and Heltec
bench work demonstrates bounded secure pairing, saved-owner reconnection,
protected device configuration, and display-clock synchronization. Each result
applies only to the setup and limitations in its evidence record.

Secure radio experiments and host tests are separate from complete product
messaging. Coherent two-phone/two-device messaging, supported hardware, field
range, endurance, and release acceptance remain open. An iPhone app is not yet
implemented. Trail is a supplemental communication aid, not a guaranteed rescue
system.

See [current project status](docs/PROJECT_STATUS.md), the
[V1 progress record](docs/V1_PROGRESS.json), and the
[engineering backlog](tasks/BACKLOG.md) for accepted evidence and next gates.

## Get started

1. Read the [development guide](docs/DEVELOPMENT.md) for prerequisites and build
   instructions.
2. Explore the [Android client](android/README.md),
   [firmware components](firmware/components/), and
   [hardware inventory](hardware/INVENTORY.md).
3. Start with host validation; hardware installation has separate target,
   recovery, and operating requirements.

From the repository root on a configured Windows development machine:

```powershell
.\tools\Test-Host.ps1
```

For Android validation, follow the [Android guide](android/README.md).

## Contribute

We welcome focused contributions to clients, firmware, testing, and documentation.
An immediate opportunity is the
[iPhone companion app contributor brief](docs/community/IOS_CONTRIBUTOR_BRIEF.md).

Read [CONTRIBUTING.md](CONTRIBUTING.md), choose a bounded item from the
[backlog](tasks/BACKLOG.md), and discuss substantial changes before implementation.
The [community guide](docs/community/README.md) explains how to get involved.
Report sensitive issues through [SECURITY.md](SECURITY.md).

## Documentation

The [documentation index](docs/README.md) routes readers to architecture,
protocols, hardware evidence, and project records. Dated engineering updates live
in the [progress log](docs/PROGRESS_LOG.md). The
[future-concepts register](docs/FUTURE_CONCEPTS.md) separates longer-term ideas
from current release commitments.

## License

Trail is available under the [Apache License 2.0](LICENSE).
