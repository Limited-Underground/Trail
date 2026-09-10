# OT-187 security policy evaluation

This additive evaluation composes real signed invitations, Noise XK, directional
traffic keys, durable outbound counters and authenticated receive checkpoints.
It is host-tested evaluation code. It does not accept product provisioning,
human confirmation, session resumption, full rekey, physical persistence or LoRa
operation. The predecessor target and earlier accepted evidence remain frozen.

## Implemented boundary

The [invitation gate](../../firmware/components/security_evaluation/include/opentrail/evaluation_invitation.hpp)
verifies an Ed25519 signature against an externally supplied exact signer pin.
Its canonical versioned payload binds group, epoch, ordered full X25519 peer
identities, nonce, boot context and original issue/deadline times. SHA-256 over
the entire signed context becomes the Noise prologue. A gate consumes its single
attempt even on failure; malformed fields, wrong pins, reentry, rollback and
expiry refuse. The original window is at most 60 seconds. Confirmation compares
the actual handshake transcript exactly. These evaluation inputs do not implement
production trust-root enrollment, a human comparison UI or durable invitation
nonce custody across reconstructed objects.

The [policy session](../../firmware/components/security_evaluation/include/opentrail/evaluation_policy_session.hpp)
uses the admitted real-primitive Noise adapter and guarded random-source interface.
Both roles verify the actual remote static key before traffic-key activation.
Direction-specific context binds the signed invitation, handshake transcript,
ordered sender/recipient and split key. Fixed eight-byte test messages use the
existing network-order AEAD nonce encoder. The evaluation record is not a selected
product wire format. Plaintext is released only after authentication and exact
durable receive-state readback; outbound counters are reserved before sealing.

Each PolicySession owns its actual TX allocator/store and RX replay store. Before
its confirmation can mutate persistence, it reads both 64-byte slots of each
exact store and requires every byte to be blank (0xff). Retained, corrupt,
nonblank or unreadable state refuses without erasing or writing those stores.
This is **fresh-only per-role admission**: A may initialize before B discovers
retained state. There is no cross-role transaction or global no-mutation promise.
All retained state refuses even when a caller recreates identical keys/context;
there is no resumed-key path or automatic counter reset.

The [receive store](../../firmware/components/security_evaluation/include/opentrail/evaluation_replay_store.hpp)
persists a strict high-water counter with alternating committed records and exact
readback. It rejects duplicates and lower counters; it is not an out-of-order
sliding window. Authentication failures leave replay state and caller output
unchanged. Uncertain persistence closes the session. Logical retirement writes a
readback-verified tombstone, clears volatile secrets and permanently closes that
session. Store restart tests are separate evidence; they do not mean PolicySession
resumes keys. Physical erasure, rollback-resistant storage and full rekey remain
outside this increment. Invitation expiry ends at successful confirmation;
established traffic still enforces monotonic time and entropy readiness.

## Host evidence

The real-crypto runner passed 40 invitation and 31 composed-session groups and
checks the independent 26-group Noise proof before compiling these tests. It uses
hash-admitted libsodium 1.0.22 scalar primitive sources, including actual
Ed25519/SHA-512, with GCC/G++ 16.1.0. New C++ code compiles with warnings as errors.
Deterministic test randomness and injected persistence faults are test inputs;
this is not target entropy, SDK initialization or CPU-dispatch acceptance.

| Command (repository root) | Observed groups | Boundary |
| --- | ---: | --- |
| `python tests/host/security_policy_crypto_tests.py` | 40 invitation + 31 session + 26 independent Noise | Real cryptography; deterministic entropy and memory storage |
| `python tests/host/security_policy_replay_tests.py` | 15 | Actual replay-store restart, corruption and persistence faults |
| `python tests/host/security_policy_nvs_tests.py` | 23 | Actual NVS backend with SDK stubs; namespace isolation and failure behavior |
| `python tests/host/security_policy_control_tests.py` | 12 | Actual bounded control state |
| `python tests/host/security_policy_target_tests.py` | 10 | Actual console/control-loop source with SDK seams; does not execute app_main |
| `python tests/host/security_policy_capture_tests.py` | 9 | Host capture framing, freshness and deadline failures |

Security cases include wrong signature, signer, peer, boot context, group, epoch,
recipient, direction and tag; unconfirmed traffic refusal; expiry/rollback;
duplicate/lower counter output preservation; applied-then-failed persistence;
retirement failure; entropy revocation; and retained-TX/missing-RX plus the reverse.
Freshness tests count writes, erases and syncs, rather than allowing an unnoticed
automatic erase. No physical target or radio was executed for these host results.

The complete affected `Test-Host.ps1` run passed (exit 0), including these new
portable suites and the final 13/13 simulator UI checks. The real-crypto runner
above was additionally executed as its separate toolchain-dependent proof.

## Target and build gate

The [target](../../firmware/targets/heltec_v4_security_policy_eval/README.md)
is a same-chip two-role evaluation for the Heltec V4.2 ESP32-S3, 16 MB flash
profile. A consumed solicited command precedes NVS initialization and entropy
lifecycle operations. The runtime starts the guarded BLE-controller entropy
source, then calls `sodium_init`, exercises both roles, retires their keys and
stops entropy before submitting one bounded terminal receipt. It has no LoRa
operation or product BLE host/advertising behavior. SDK entropy/controller and
NVS calls remain potentially blocking; protocol deadlines do not cancel them.

Fresh a3/b3 builds passed with seven byte-identical artifact pairs and zero
compiler warnings. The application is 437,488 bytes, SHA-256
`9682f86877b8c8b5d20a6309dd80199f8729bd61e08bc6787bcbea626dc7b836`.
[OT-187 build evidence](../../tests/benchmarks/crypto/OT-187-SECURITY-POLICY-BUILD-2026-09-10.json)
records exact source/library pins, toolchain, all tuples and the separate Python
environment tooling notice. The decoded direct libc write calls route through
the policy wrapper; this scan does not prove arbitrary indirect calls or internal
ROM execution absent. VFS remains configured; CORE quarantine and retained ROM
wrappers are linked. Runtime exclusive ownership remains physically unproven,
and panic/reset terminates the admitted console session. Named NimBLE host
startup/advertising and USB driver-install definitions are absent; the controller
configuration remains enabled for entropy. These are build/link facts, not runtime
acceptance. No earlier target hash substitutes for this report.

## Porting checklist dispositions

These dispositions apply the [mandatory porting checklist](../firmware-porting-lessons.md).

| Gate | Disposition |
| --- | --- |
| Exact target boundary | ESP32-S3/16 MB 80 MHz candidate profile; sdkconfig selects QIO while generated flash arguments/bootstrap header use DIO; no physical flash-mode claim; application offset 0x10000 and accepted bench partition bytes. No region change or radio TX. Display, battery and GNSS are unlinked and untested because this is isolated security evaluation. |
| Reproducible bytes/builds | Explicit project version, pinned offline dependency and cache policy; two fresh builds and the seven matching tuples plus source/link audit are recorded in the linked report. Historical frozen inputs remain unchanged. |
| Boot/USB/reset | Solicited single command and bounded receipt; startup quarantine and direct-call writer routing are checked in the fresh build audit, with its indirect/ROM limitations. Capture limits and callback assumptions are documented separately. Fresh endpoint/reset and actual USB timing remain physical gates. |
| Concurrency/order | Single owner for sessions/control; serialized entropy guard. Request consumption precedes NVS/entropy work; confirmation precedes traffic. No NimBLE GATT callbacks, bonding or owner-replacement path is implemented here. SDK operations are not proven deadline-bounded. |
| Persistence/cleanup | Four isolated TX/RX namespaces, exact commit/readback, per-role fresh-only admission, logical retirement and no broad erase. Failure containment and restart records are host-tested. Physical power loss, reset and complete NVS restoration are untested. |
| Composed validation | Actual real-crypto session, store, control, target and capture tests supplement source/link audit. Complete affected host matrix passed; hardware authority is not supplied by either tests or builds. |
| Physical execution/recovery | Deferred: no new grant, device inventory/readback, write or radio execution in this increment. Earlier grants are consumed. Before any trial independently bind each device, image, offset, protected/application originals and the full NVS span to one-use authority and restoration. Cold-power enclosure disassembly remains owner-deferred. |

## Next admission boundary

[Capture preparation](OT-187-CAPTURE-PREPARATION.md) defines the exact challenge,
receipt and bounded collector. A future physical package must privately preserve
and independently restore/read back the entire ordinary NVS span (currently
0xd000, length 0x3000), including any pairing/owner data, as well as application
and protected regions. Existing application-only recovery is insufficient. Never
publish NVS bytes or substitute namespace deletion for whole-span restoration.
No flash, two-node communication, phone integration, power-cut behavior or product
security selection is accepted by this increment. Public website status is unchanged.
