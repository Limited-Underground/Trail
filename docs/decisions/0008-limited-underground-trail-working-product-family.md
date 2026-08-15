# Decision 0008: Limited Underground Trail Working Product Family

Status: owner-approved working naming architecture; professional clearance and
production adoption remain pending, 2026-08-14

## Decision

`Limited Underground` is the parent working identity. The customer-facing
working name for this repository's product family and Android application is
`Limited Underground Trail`.

The working product names are:

| Working name | Product boundary |
| --- | --- |
| `Limited Underground Trail` | Android application and umbrella field-communication family |
| `Limited Underground Trail Essential` | Screenless LoRa companion that requires one paired phone for normal user interaction |
| `Limited Underground Trail Gold` | Self-contained LoRa client with one touchscreen display |
| `Limited Underground Trail Platinum` | Self-contained LoRa client with two displays |
| `Limited Underground Trail Repeater` | Dedicated LoRa repeater running the applicable repeater firmware |
| `Limited Underground Firmware Loader` | Shared desktop firmware inspection, installation, update, and recovery utility |

The loader must display `Preview` and `Inspection only` until both firmware
writing and recovery have passed their applicable real-hardware acceptance
gates. Planning a writer, parsing a bundle, or enabling a control is not enough
to remove either qualifier.

All names in this decision are working and provisional pending comprehensive
clearance. They are not registered names, no `®` may be used, and permanent
hardware marking, packaging, sales, and store publication remain subject to
professional review.

## Stable engineering identity

This customer-facing decision does not rename or migrate any existing technical
identity. The following remain stable:

- the `OpenTrail` repository, folders, source namespaces, and build targets;
- `OT-*` work-item, test, inventory, and evidence identifiers;
- packet magic, protocol names, GATT UUIDs and characteristics;
- persistent schemas, compatibility identifiers, cryptographic domains, and
  key-derivation contexts; and
- device IDs, board identifiers, and signed firmware-bundle identities.

New customer-facing working names must enter through replaceable presentation
boundaries. They must not be copied into protocol or durable identity fields.

## Product relationship

The Android application is paired one-to-one with one Essential companion for
normal use. Gold and Platinum remain self-contained touchscreen products and do
not acquire a phone dependency. All three client forms reuse the versioned
protocol and hardware-independent application behavior described by
[Decision 0007](0007-shared-client-presentation-tracks.md), while retaining
separate platform and physical evidence gates.

The Repeater is optional infrastructure. Its absence must not make direct
client operation fail or create false delivery evidence.

The Firmware Loader is shared tooling rather than a Trail client. Messaging,
maps, group operation, and ordinary phone pairing do not belong in the loader.

## Consequences and remaining gates

- User-facing product matrices, package metadata, and store listings may use
  these working names only with the provisional-clearance boundary intact.
- Hardware and firmware support claims still require exact target, recovery,
  power, radio, regulatory, and field evidence.
- `Essential`, `Gold`, and `Platinum` describe presentation/hardware tiers; they
  do not select a wire protocol or cryptographic identity.
- A future rename remains possible without a protocol, persistence, security,
  or device-identity migration.
- Final clearance, approved marks, final packaging, and distribution remain
  open governance gates.
