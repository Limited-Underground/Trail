# Decision 0003: Benchmark Before Cryptographic Selection

Status: accepted decision gate; implementation selection pending, 2026-08-10

## Decision

OpenTrail will not freeze packet-v1 cryptography or import a handshake library
until the exact ESP32-S3 client target and ESP-IDF/toolchain are selected and a
reproducible target benchmark passes the security, interoperability, resource,
entropy, persistence, recovery, license, and failure-injection gates in the
[dated candidate review](../security/CRYPTO_CANDIDATE_REVIEW_2026-08-10.md).

The first benchmark uses Espressif's maintained libsodium component. The pinned
ESP-IDF mbedTLS/PSA build and Monocypher are comparisons. Noise-C is reference
and vector material only.

The leading invitation prototype is `Noise_XK_25519_ChaChaPoly_SHA256` with a
separate Ed25519 identity key and X25519 Noise static key. XK is eligible only
when a signed invitation pins the inviter's Noise key. Failure never downgrades
to a weaker handshake or releases group material.

## Why

The project has algorithm-neutral lifecycle tests but no selected client target,
crypto component lock, measured resource cost, protected storage, entropy proof,
rollback-safe outbound counter, or physical onboarding evidence. Selecting from
desktop specifications alone would turn an architectural candidate into an
unsupported security claim and could conflict with recovery requirements.

## Consequences

- Packet v0 remains unauthenticated test traffic.
- Crypto is isolated behind a target adapter; lifecycle policy cannot manufacture
  authentication evidence from caller-supplied Booleans in production.
- Identity signing keys and Noise DH keys use separate purpose domains.
- Routine group traffic needs sender-specific keys and rollback-safe nonce/
  counter allocation before packet v1.
- Production eFuse, Secure Boot, and release flash-encryption work requires a
  sacrificial device and separately proven recovery path; current bench radios
  are not used for irreversible security enablement.
