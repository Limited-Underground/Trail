# Help explore an iPhone companion for Trail

We welcome a contributor who can develop and test on an Apple setup. Trail's
current companion implementation is Android. An iPhone client is not implemented,
and iOS is not part of the current Android V1 acceptance claim.

## A useful first contribution

Assess and prototype one foreground BLE connection to an approved Trail bench
device: discover its advertised setup label, use system-owned pairing, establish
the protected service, read protocol/device information, and report connection
or failure truthfully. Do not start with maps, a full interface redesign or App
Store publication.

The first pull request can be a short feasibility report and a minimal prototype
proposal. Agree the implementation location and dependencies before introducing
a new client tree. No iPhone minimum OS, Swift framework choice, background-mode
policy or store-distribution plan is frozen yet.

## Useful equipment and experience

- Access to a Mac with an appropriate Xcode installation and a physical iPhone.
- Swift/iOS development and willingness to investigate Core Bluetooth behavior.
- An approved compatible bench radio for physical testing, arranged with the
  maintainer before purchasing hardware. Hardware provision is not promised.
- Comfort documenting differences between Android and iOS rather than assuming
  their bonding, discovery or background behavior is identical.

There is useful protocol-review and documentation work before hardware testing.
Signing, device deployment and any later distribution requirements must be
verified against the Apple tools/account used for the contribution.

## Starting references

- [Current status and limits](../PROJECT_STATUS.md)
- [BLE service and protocol](../platform/BLE_COMPANION_GATT_V0.md)
- [Phone authorization](../platform/COMPANION_AUTHORIZATION_V0.md)
- [Current factory-reset and pairing contract](../platform/DEVICE_FACTORY_RESET_V1.md)
- [Android reference source](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/)
- [Development guide](../DEVELOPMENT.md) and [security policy](../../SECURITY.md)

The current pairing design uses a temporary matching setup label and a locally
displayed passkey. A name alone is never authorization. Do not capture PINs in
logs, bypass system security or treat raw BLE connectivity as an authenticated
companion session.

## First-checkpoint acceptance

Record the tested iPhone/iOS, Xcode and firmware versions. Demonstrate supported
discovery and protected readback, plus clear handling of cancellation, wrong or
expired authorization and disconnect/reopen. Report unsupported behaviors and
background limitations separately. A simulator-only build is not BLE hardware
acceptance, and this checkpoint would not prove secure radio messaging.

Introduce your available setup and interests in
[Discussions](https://github.com/Limited-Underground/Trail/discussions), or open a
[scoped contribution proposal](https://github.com/Limited-Underground/Trail/issues/new/choose).
Do not include private device identifiers. See [Contributing](../../CONTRIBUTING.md).
