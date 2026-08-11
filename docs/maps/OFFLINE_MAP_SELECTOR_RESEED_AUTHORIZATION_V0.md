# Offline Map Selector Reseed Authorization v0

Status: deterministic host-tested authorization handoff, 2026-08-11. No
credential verifier, target service session, or on-device authentication is
implemented.

This boundary prevents the destructive selector-reseed coordinator from
accepting five caller-created booleans as its authority. It inserts an injected
local-service verifier and a non-copyable, exact-operation, single-use permit
before the [reseed coordinator](OFFLINE_MAP_SELECTOR_RESEED_COORDINATOR_V0.md).

## Target-owned verification

A concrete target backend owns every security-sensitive mechanism:

- credential or cryptographic proof verification;
- administrator-role and device-service policy;
- challenge creation, expiry, replay state, and audit;
- binding to the current boot-local service session;
- the local confirmation screen and its committed revision; and
- atomic consumption of the opaque authorization handle.

`verify_and_consume()` must never return the same handle as authorized twice.
The common component passes no password, PIN, key, proof bytes, operator
identity, device identifier, address, path, or free text. It receives only a
redacted grant after the backend has made and consumed its decision.

No concrete backend exists yet. The interface does not make USB secure and
does not define a BLE, Wi-Fi, challenge, credential, key-storage, or audit
protocol.

## Exact grant binding

An authorized grant must echo all of the following exactly:

1. the opaque nonzero authorization handle;
2. the nonzero current boot-session identifier;
3. the `selector_reseed` scope;
4. either local USB service or an independently authenticated local-wireless
   service transport;
5. the complete activation policy;
6. the reviewed trusted-minimum selector generation;
7. every field of the proposed baseline package evidence;
8. all five service confirmations: explicit operator intent, temporary map
   unavailability, selector-only scope, package retention, and trusted-floor
   review; and
9. a nonzero committed local-confirmation revision.

Unknown transport and remote radio are refused. A future local-wireless
backend must authenticate its session independently; naming that transport in
the grant is not proof.

## Short-lived, single-use permit

The caller selects a nonzero maximum grant lifetime no greater than the hard
v0 ceiling of 300,000 ms. The authorizer requires:

- `expires_at_ms` strictly greater than `issued_at_ms`;
- lifetime no longer than both the caller policy and the hard ceiling;
- current checked boot-local time at or after issue; and
- current time strictly before expiry.

Exact expiry is rejected. A future timestamp, zero-length window, excessive
lifetime, wrong boot, scope, transport, handle, binding, confirmation, or
backend state produces no permit.

The resulting permit contains only the exact operation binding plus its
boot-session and issue/expiry bounds. It is
non-copyable and movable solely to transfer ownership. The reseed coordinator
consumes it before checking live state or reading selector storage and rechecks
the current boot session and checked boot-local use time. A binding, boot, or
time mismatch also burns the permit, and a second use is rejected before
storage access. Any later service or persistence failure therefore requires a
newly verified authorization.

## Failure and authority limits

- Invalid policy, request, or package binding never reaches the backend.
- Backend denial, temporary unavailability, and failure remain distinct.
- A malformed grant cannot mint a permit even if its backend state says
  `authorized`.
- Authorization does not bypass the mapless-owner, package, generation,
  selector-clear, commit-last, readback, or exclusive-ownership checks.
- The permit grants no access to package bytes, radio, messages, positions,
  alerts, vehicle integration, other NVS keys, update recovery, or factory
  reset.

The common boundary cannot prove who authenticated, whether a challenge was
cryptographically strong, whether local confirmation was physically observed,
whether the target supplied the checked current use time correctly, or whether
backend replay state survives reset. Those remain target security and physical-
evidence gates.

## Current evidence

Ten deterministic authorization groups cover exact USB and authenticated-
local-wireless grants, invalid preflight, backend denial/readiness/failure,
handle/scope/boot mismatch, complete operation binding, all five confirmations,
local confirmation, time-window boundaries, backend replay refusal, output
invalidation, and move-without-copy behavior.

The twelve reseed groups now require a fresh permit, burn binding-mismatched
permits, reject replay, and retain all selector recovery coverage. All ten map
suites pass 100/100 focused repeats in the complete 68-executable host matrix.
This is host authorization-handoff evidence only—not authentication, an ESP-IDF
target, a service UI, protected trust, persistence of replay state, or a
physical service result.
