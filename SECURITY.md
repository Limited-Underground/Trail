# Security Policy

OpenTrail is pre-release research software and is not suitable for emergency-response dependency, safety-critical control, or protection of sensitive information.

## Reporting a security concern

Do not include credentials, private keys, channel secrets, precise private locations, or exploit details in a public issue. Prefer a private GitHub Security Advisory when that feature is enabled. Otherwise, contact the repository owner privately before sharing sensitive details.

## Current security boundaries

- Emergency and priority features are supplemental aids, not guaranteed rescue services.
- The owner-approved V1 design uses practical physical-presence authorization: a normally closed Heltec opens one short window, displays a fresh random six-digit PIN locally, admits authenticated BLE Secure Connections pairing/bonding, and retains only one current controller phone. This is a requirement, not an implemented or accepted capability.
- V1 does not claim ownership rollback protection against factory reset, reflashing, invasive physical access, or restoration of old flash. A secure element or external monotonic component is not a V1 requirement.
- BLE phone authorization does not secure radio traffic. V1 LoRa authentication, encryption, sender/destination identity, unique IDs, integrity, replay/duplicate rejection, acknowledgement, bounded retry, and key provisioning/replacement remain separate implementation and physical-acceptance gates.
- Hardware test tools must redact node identities, keys, coordinates, PINs, and secrets.
- No firmware or hardware configuration should be described as secure or production-ready without documented threat modeling and validation.
