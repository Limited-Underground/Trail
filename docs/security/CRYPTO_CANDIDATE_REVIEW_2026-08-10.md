# Cryptographic Candidate Review

Review date: 2026-08-10

Status: **benchmark direction, not a production algorithm or library selection**

## Outcome

OpenTrail will benchmark the maintained Espressif `libsodium` managed component
first on the exact ESP32-S3 client target. The target's pinned ESP-IDF mbedTLS/
PSA build and Monocypher remain comparison candidates. Noise-C is useful as a
reference implementation and vector source but is not a shipping candidate.

The leading offline invitation-handshake prototype is
`Noise_XK_25519_ChaChaPoly_SHA256` only when the invitation pins the inviter's
Noise static public key and binds it to the authoritative device identity. If
that prerequisite is absent or fails, onboarding stops; it does not silently
downgrade to `XX`, plaintext group-key transfer, name matching, or trust-on-first
radio receipt.

Packet v0 remains unauthenticated test traffic. This review does not authorize
real group, location, message, or emergency data on packet v0.

## Why XK is the leading join prototype

The current invitation contract already requires the joining device to know and
verify the inviter identity before current group material is released. Noise XK
models that asymmetry:

```text
XK:
  <- s
  ...
  -> e, es
  <- e, ee
  -> s, se
```

The joiner knows the responder static key before message one; the responder
learns the joiner static key under encryption in message three. This is a three-
message interactive handshake with forward-secret ephemeral DH contributions.
The invitation ID, version, group, epoch, requested role, expiry, and nonce must
be transcript-bound payload/AAD, and both devices must show the same short
authentication value before group material is installed.

Noise [defines XK and the named 25519/ChaChaPoly/SHA256 suite](https://noiseprotocol.org/noise.pdf),
but it does not define OpenTrail's invitation format, UI confirmation, radio
fragmentation, retry policy, secret storage, or group-key lifecycle. Those
remain OpenTrail responsibilities.

## Separate signing and Noise keys

The working model uses two device key purposes:

- an Ed25519 signing identity whose public key anchors the authoritative device
  fingerprint and signs the invitation-to-Noise-key binding; and
- a separate X25519 static Noise key for the onboarding handshake.

The invitation carries the inviter's identity public key/fingerprint, Noise
static public key, and a signature binding that Noise key to the complete
invitation context. The joining X25519 key becomes bound to the joining identity
inside the confirmed transcript.

Although libsodium can
[convert Ed25519 keys to X25519](https://doc.libsodium.org/advanced/ed25519-curve25519),
its own guidance recommends distinct signing and key-exchange keys when the
cost is acceptable. OpenTrail therefore does not make key conversion the
default design.

## Candidate library matrix

| Candidate | Evidence in favor | Unresolved before selection | Current disposition |
| --- | --- | --- | --- |
| Espressif `espressif/libsodium` | The official component registry currently lists `1.0.22`, all ESP targets, an ISC license, and an IDF managed-component install path. Upstream supplies Ed25519, X25519, IETF ChaCha20-Poly1305, hashing, key derivation, secure comparison/wipe, and testable deterministic APIs. | Measure actual linked flash/RAM/stack, ESP entropy adapter, target concurrency, build flags, update cadence, exact APIs required by the handshake adapter, and SBOM/lock behavior. The 2.43 MB registry archive is not a linked-binary measurement. | **Primary target benchmark** |
| ESP-IDF mbedTLS/PSA | Already shipped with ESP-IDF; current ESP32-S3 configuration exposes Curve25519, ChaCha20/Poly1305, HKDF, SHA, AES, and platform acceleration options. ESP-IDF 6 moves to Mbed TLS 4 and a PSA-first model. | Exact APIs and enabled algorithms depend on the pinned IDF branch; Ed25519/Noise composition, constant-time behavior, binary size, and portability across the selected project framework must be proven. | **Built-in comparison** |
| Monocypher 4.0.3 | Small dependency-free C, CC0/BSD-2-Clause, embedded-friendly integration, explicit wipe, X25519 and ChaCha/Poly1305, and a published Cure53 audit. | It provides primitives rather than Noise state handling; its default EdDSA uses BLAKE2b rather than standard Ed25519 unless optional Ed25519 code is selected, and its manual puts input-length validation on the caller. Target integration and vectors remain unproved. | **Small-footprint comparison** |
| Noise-C | Implements the relevant patterns and algorithms with public vector tests and several crypto backends. | Its own documentation calls it work in progress, targets GNU/Linux/desktop first, uses Noise revision 30, and lists compile-time subsetting for constrained systems as TODO. | **Reference/vector source only** |

Point-in-time primary sources:

- [Espressif libsodium component](https://components.espressif.com/component/espressif/libsodium)
- [libsodium IETF ChaCha20-Poly1305](https://doc.libsodium.org/secret-key_cryptography/aead/chacha20-poly1305/ietf_chacha20-poly1305_construction)
- [libsodium key exchange](https://doc.libsodium.org/key_exchange)
- [libsodium Ed25519 signatures](https://doc.libsodium.org/public-key_cryptography/public-key_signatures)
- [Monocypher overview and integration](https://monocypher.org/)
- [Monocypher manual](https://monocypher.org/manual/)
- [Monocypher Cure53 audit](https://monocypher.org/quality-assurance/MON-01-report.pdf)
- [Noise-C documentation](https://rweather.github.io/noise-c/)

## Entropy is a hard gate

ESP-IDF states that after application startup the ESP32-S3 normally provides
only pseudorandom output unless Wi-Fi/Bluetooth or the internal entropy source
is enabled. The internal entropy source has ADC/RF concurrency restrictions;
ESP-IDF recommends a strong DRBG seeded from true hardware entropy when a
continuous source cannot remain enabled. See the
[ESP32-S3 random-number guidance](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/random.html).

Consequences:

- no identity, invitation, ephemeral, group, nonce-prefix, or recovery key is
  generated until the target entropy preconditions are proven;
- the crypto adapter owns an explicit `entropy_ready` state and fails closed;
- host deterministic RNG exists only for published vectors/tests and cannot be
  linked into production target composition; and
- entropy initialization, concurrent RF/ADC use, reboot, brownout, and repeated
  cold-start uniqueness require physical evidence.

## Group-traffic direction

The handshake only provisions membership and current epoch material. It is not
the routine LoRa packet transport. The packet-v1 prototype should separately
measure:

- one sender-specific traffic key derived from the epoch key and authoritative
  sender identity so different devices do not share an AEAD nonce domain;
- IETF ChaCha20-Poly1305 with the exact version/type/routing/group epoch/sender
  alias/message counter/ciphertext length authenticated as AAD;
- a 96-bit nonce construction with an explicit domain plus rollback-safe
  monotonic per-sender counter; and
- a 16-byte tag in every protected packet.

That sender-specific derivation prevents accidental cross-sender nonce-domain
reuse, but if every current member can derive every sender key from common epoch
material, it does not authenticate one member against another. Individual
sender claims for group broadcasts therefore require a separately benchmarked
source-authentication construction; the leading comparison is an Ed25519-style
signature/countersignature. Pairwise DH-derived AEAD is a unicast comparison,
not one broadcast to the whole group. See Decision 0004.

Libsodium warns that a nonce must never be reused under one IETF
ChaCha20-Poly1305 key and recommends incrementing nonces rather than repeatedly
choosing random values. OpenTrail therefore needs persistent counter leasing or
equivalent rollback-safe allocation before packet v1; a random nonce alone is
not the default plan.

## Production-device security and recovery

Secure Boot, flash encryption, NVS protection, key storage, packet cryptography,
and recovery are related but separate gates. Espressif recommends Secure Boot
and release-mode flash encryption for production, but those settings burn or
lock eFuses and can restrict update/recovery interfaces. In particular, the
ESP32-S3 Secure Boot v2 guidance says enabling Secure Boot or flash encryption
disables the ROM USB-OTG serial/DFU path.

No irreversible eFuse or release-mode operation is authorized on the current
bench radios. Before any production-security enablement, OpenTrail needs a
separate sacrificial-device procedure, alternate recovery interface, signed OTA
path, key backup/rotation policy, manufacturing record, and power-interruption
test. See Espressif's current
[security overview](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/security/security.html),
[Secure Boot v2 guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/security/secure-boot-v2.html),
and [flash-encryption guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/security/flash-encryption.html).

## Benchmark and selection acceptance

An exact library/suite becomes selected only after one reproducible target
report records:

1. exact client board/revision, ESP-IDF/toolchain, component versions and locks,
   configuration flags, compiler options, and license/SBOM inventory;
2. standard Ed25519, X25519, SHA256/HKDF or Noise KDF, and IETF
   ChaCha20-Poly1305 vectors plus negative/corruption cases;
3. XK transcript interoperability against an independent implementation and
   refusal of bad invitation binding, wrong role/epoch, replay, timeout, reorder,
   truncation, and user-code rejection;
4. 100 cold and 100 warm operations with min/median/p95/max timing, stack high-
   water, static/dynamic RAM, linked flash delta, and watchdog impact;
5. exact handshake byte/fragment/airtime/retry cost at the selected radio MTU;
6. entropy-ready/absent behavior, concurrent radio/ADC conditions, reboot and
   brownout uniqueness, temporary-secret wipe, and failure logging redaction;
7. rollback-safe message-counter reservation and interrupted persistence; and
8. two-device join, rename, revoke/rekey, configuration reset, factory reset,
   and recovery with verified cleanup.

Until those pass, the candidate names stay out of packet identifiers and public
security claims.
