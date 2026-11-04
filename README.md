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

**The November 2 product freeze remains, with a narrow November 3 host exception.**
The owner authorized OT-0237e admission work and Android OT-0305-1 isolated
host implementation; OT-0005i waits for accepted admission and exact production
source selection. See the [working contract](CONTRIBUTING.md#working-contract).
Physical work, signing, deployment, releases and unapproved publication remain
excluded. Other product development remains frozen.

Trail is an experimental project, not a production release. Android and Heltec
bench work demonstrates bounded secure pairing, saved-owner reconnection,
protected device configuration, and display-clock synchronization. Each result
applies only to the setup and limitations in its evidence record.

The November 2 fixed-status diagnostic run demonstrated both phone-to-phone
directions through BLE/direct LoRa/BLE, matching diagnostic ACKs and verified
closure. Its RF was unauthenticated. Coherent protected product messaging,
supported hardware, field range/endurance and release acceptance remain open.
Earlier secure-radio experiments retain their exact evidence limits. An iPhone app is not yet
implemented. Trail is a supplemental communication aid, not a guaranteed rescue
system.

See [current project status](docs/PROJECT_STATUS.md), the
[V1 progress record](docs/V1_PROGRESS.json), and the
[engineering backlog](tasks/BACKLOG.md) for accepted evidence and next gates.

## Resume work

Start with the [working contract](CONTRIBUTING.md#working-contract) and the
[current handoff](docs/PROJECT_STATUS.md#current-recovery-handoff).
[Trail Firmware Project #1](https://github.com/orgs/Limited-Underground/projects/1)
and [Trail Android App Project #8](https://github.com/orgs/Limited-Underground/projects/8)
own their current task scope, status and dependencies; repository records hold the
technical evidence. On the owner's recovery machine, use the maintainer-approved checkout;
historical copies are provenance, not active source. The October 23 bounded
local recovery acceptance remains historical; OT-0340 retains its exceptions.
The current freeze and its task-specific host exception control execution.
A board status
or these setup examples do not authorize device operations or publication.

## Get started

1. Read the [Trail Firmware V1 guide](docs/firmware/README.md) for its nine
   assessment areas, [build/recovery evidence](docs/firmware/BUILD_AND_RECOVERY.md)
   and [remaining V1 criteria](docs/firmware/V1_ACCEPTANCE.md).
2. Explore the [Android client](android/README.md),
   [firmware components](firmware/components/), and
   [hardware inventory](hardware/INVENTORY.md).
3. Match validation to the authorized change; hardware installation has separate
   target, recovery, and operating requirements.

For documentation validation, from the repository root:

```powershell
python -B tools/check_repository_docs.py
python -B tests/host/repository_docs_tests.py
```

For setup recipes use the [development guide](docs/DEVELOPMENT.md); Android
validation has its own [Android guide](android/README.md). The qualified host
firmware rebuild remains byte-different from the accepted diagnostic image.
Focused independent review now accounts for [every compared image difference](docs/firmware/BUILD_AND_RECOVERY.md#reviewed-attribution-of-the-image-differences),
with explicit symbol-range and ELF limits. It does not establish runtime
equivalence, authorize substitution, or satisfy protected product/release acceptance.

## Contribute

We welcome focused contributions to clients, firmware, testing, and documentation.
When ordinary development resumes, a contribution opportunity is the
[iPhone companion app contributor brief](docs/community/IOS_CONTRIBUTOR_BRIEF.md).

Read [CONTRIBUTING.md](CONTRIBUTING.md), reconcile the current Project item with
the [backlog](tasks/BACKLOG.md), and confirm scope before implementation.
The [community guide](docs/community/README.md) explains how to get involved.
Report sensitive issues through [SECURITY.md](SECURITY.md).

## Documentation

The [concise architecture](docs/architecture/README.md) and
[protocol selection index](docs/protocol/README.md) explain current boundaries
and evidence limits. The [documentation index](docs/README.md) routes readers to
hardware, build/recovery guidance and project records. Dated engineering updates live
in the [progress log](docs/PROGRESS_LOG.md). The
[future-concepts register](docs/FUTURE_CONCEPTS.md) separates longer-term ideas
from current release commitments.

## License

Trail is available under the [Apache License 2.0](LICENSE).
