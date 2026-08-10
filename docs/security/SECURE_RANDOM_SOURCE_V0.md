# Secure Random Source Boundary v0

Status: deterministic host boundary and test fake, 2026-08-10. No ESP32
entropy adapter, production key generation, or target entropy evidence is
claimed.

## Purpose

OpenTrail must not generate an identity key, invitation token, ephemeral key,
group secret, nonce prefix, or recovery key until the target can provide
cryptographically strong randomness. This boundary makes that prerequisite
explicit without selecting a cryptographic library or pretending a host test
proves ESP32 entropy quality.

The production-facing interface lives in
`firmware/components/security/include/opentrail/secure_random.hpp`. The
deterministic source lives only under `test_support` and must never be included
in a deployable target.

## Contract

- `EntropyState::not_ready` means strong output is not currently available.
- `EntropyState::ready` is permitted only while the adapter can satisfy the
  target's documented strong-entropy or properly seeded strong-DRBG policy.
- `EntropyState::failed` is a persistent or operator-relevant source failure.
- One request must contain 1 through 64 bytes. The bound covers the immediate
  identity, key, salt, and token candidates while preventing unbounded adapter
  work. Larger derivations belong in the selected cryptographic library.
- A successful fill writes the complete request and reports the exact byte
  count.
- Invalid shape, oversized request, unavailable entropy, and source failure are
  distinct results.
- Every failure leaves the caller's output buffer unchanged and consumes no
  bytes. Partial or best-effort random output is forbidden.
- A prior readiness observation does not authorize a later fill. The adapter
  must recheck its usable state as part of every request.
- No implementation may silently fall back to a weaker pseudorandom source.
- Random output, seed material, keys, and derived secrets must never enter
  ordinary logs or public evidence.

## Deterministic fake

`FakeSecureRandomSource` provides a fixed 512-byte script, typed readiness
changes, one-shot failure injection, and attempt/success/consumption counters.
It copies bytes only after all checks pass. Script exhaustion and injected
failure preserve both the output buffer and the script cursor, so retry behavior
is deterministic.

The fake is published for tests and vectors only. Its predictable output is a
security failure if linked into any production target.

## Host evidence

Eight scenario groups cover:

1. default not-ready refusal and output preservation;
2. ordered single-use consumption while ready;
3. null, empty, and oversized request rejection without consumption;
4. script exhaustion without partial output;
5. one-shot source failure followed by exact retry;
6. ready/not-ready/ready transition without skipped bytes;
7. typed persistent failed state; and
8. the exact 64-byte request boundary.

The focused executable passes 100 consecutive repeats, and the complete 29
C++ executable host matrix plus the Python evidence suites pass locally. This
is interface/failure-ordering evidence only.

## Target acceptance still required

An ESP-IDF adapter cannot be accepted until the exact board, ESP-IDF version,
sdkconfig, radio state, and entropy mechanism are frozen. Evidence must then
cover startup before and after entropy readiness, concurrent Wi-Fi/Bluetooth
and ADC/RF conditions, repeated cold boots, reboot, brownout, source failure,
concurrent callers, watchdog/latency/resource cost, and an end-to-end redaction
review. The adapter also needs a reviewed strong-DRBG policy when continuous
true entropy cannot remain enabled.

Those target checks belong in the cryptographic benchmark gate. This v0
boundary does not select libsodium, mbedTLS/PSA, Monocypher, a DRBG, a key
format, a nonce construction, or a production packet protocol.
