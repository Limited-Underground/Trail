# Security Policy

OpenTrail is pre-release research software and is not suitable for emergency-response dependency, safety-critical control, or protection of sensitive information.

## Reporting a security concern

Do not include credentials, private keys, channel secrets, precise private locations, or exploit details in a public issue. Prefer a private GitHub Security Advisory when that feature is enabled. Otherwise, contact the repository owner privately before sharing sensitive details.

## Current security boundaries

- Emergency and priority features are supplemental aids, not guaranteed rescue services.
- The owner-approved V1 design uses practical physical-presence authorization: holding the designated target-neutral local input for at least 3000 ms and releasing it opens one exact 30-second window; no GPIO/button mapping is selected. Each admitted window displays one fresh uniformly sampled six-decimal-digit passkey locally and permits one Bluetooth LE Secure Connections-only, MITM-authenticated passkey pairing attempt with bonding and an exact 16-byte/128-bit key. Legacy pairing, `Just Works`, and static/debug passkeys are denied. A replacement requires a second qualifying hold/release after the candidate secure bond and before the original deadline. OT-090 freezes and host-tests that pairing/reconnect/replacement contract; no target, Android, storage, pairing, protected-control, or physical capability is implemented or accepted.
- V1 does not claim ownership rollback protection against factory reset, reflashing, invasive physical access, or restoration of old flash. A secure element or external monotonic component is not a V1 requirement.
- BLE phone authorization does not secure radio traffic. OT-091 freezes and host-tests the algorithm-neutral `OTSL0/v0` two-node pairwise-unicast lifecycle/admission contract: a secret-free single-use invitation, mutual-authentication and matching-confirmation obligations, exact commit/activation, epoch-plus-one replacement with no old-epoch fallback, directional key/counter binding, replay-before-plaintext persistence, protected acknowledgements, and bounded retry/restart failure. OT-093 separately freezes an exact two-build pre-crypto baseline with no candidate or secure-LoRa adapter imported or executed. The historical `OTCB0/v0` plan remains `draft_blocked`; OT-116 instead accepts a successor review and freezes a phased fail-closed plan while explicitly withholding measurement authority at current source/API/import counts `3/1/0`. Decision 0003 still blocks every suite/library, handshake/KDF, and Packet V1 selection until the missing candidate preflights, exact target benchmark, and later explicit selection pass. No secure-LoRa implementation or physical acceptance exists.
- Packet v0, plaintext fallback, caller-supplied authentication Booleans, alias/name trust, random-nonce fallback, counter reuse, replay-state reset under the same key, unauthenticated acknowledgements, and post-activation old-epoch fallback are prohibited from a future protected-radio composition.
- Hardware test tools must redact node identities, keys, coordinates, PINs, and secrets.
- No firmware or hardware configuration should be described as secure or production-ready without documented threat modeling and validation.
